"""Optional SMTP dispatcher. SMTP settings/password remain local; no NAS private API."""
import argparse
import json
import logging
import smtplib
import sqlite3
import ssl
import time
from email import policy
from email.message import EmailMessage
from email.utils import formatdate, getaddresses, make_msgid
from pathlib import Path
from contextlib import closing

logging.basicConfig(level=logging.INFO,format="%(asctime)s %(levelname)s %(message)s")
def configuration(path):
    c=json.loads(Path(path).read_text(encoding="utf-8"))
    if type(c.get("enabled")) is not bool:
        raise ValueError("enabled must be boolean")
    if not c["enabled"]:
        return c
    if c.get("security") not in ("ssl","starttls"):
        raise ValueError("security must be ssl or starttls")
    if type(c.get("port")) is not int or not 1<=c["port"]<=65535:
        raise ValueError("SMTP port")
    for k in ("host","from","to","username","password_file"):
        if not isinstance(c.get(k),str) or not c[k] or any(x in c[k] for x in "\r\n"):
            raise ValueError("missing or invalid SMTP field")
    if "@" not in c["from"] or "@" not in c["to"]:
        raise ValueError("email address")
    if type(c.get("cooldown_seconds",600)) is not int or not 0<=c.get("cooldown_seconds",600)<=86400:
        raise ValueError("cooldown_seconds must be 0..86400")
    return c

def error_code(exc):
    """Expose numeric diagnostics only; SMTP replies may contain addresses or tokens."""
    label=type(exc).__name__
    code=getattr(exc,"smtp_code",None)
    number=getattr(exc,"errno",None)
    if type(code) is int:label+=" SMTP="+str(code)
    elif type(number) is int:label+=" errno="+str(number)
    stage=getattr(exc,'incubator_smtp_stage',None)
    if stage in ('configuration','connect_tls_banner','connect_banner','ehlo','starttls','ehlo_after_tls','login','mail_from','rcpt_to','data'):
        label+=' stage='+stage
    acceptance=getattr(exc,'incubator_smtp_acceptance',None)
    if acceptance in ('not_submitted','unknown','rejected','accepted'):label+=' acceptance='+acceptance
    return label

def check_configuration(path):
    """Read local config and secret without networking or returning their contents."""
    try:
        c=configuration(path)
        if not c["enabled"]:return c,{"status":"disabled","error":None}
        password=Path(c["password_file"]).read_text(encoding="utf-8").rstrip("\r\n")
        if not password or "\r" in password or "\n" in password:
            raise ValueError("password must be one line")
        return c,{"status":"ready","error":None}
    except (OSError,ValueError,UnicodeError) as exc:
        return None,{"status":"configuration_error","error":error_code(exc)}

def initialize_runtime(db):
    db.execute("CREATE TABLE IF NOT EXISTS notification_runtime(worker TEXT PRIMARY KEY,status TEXT,updated_ms INTEGER,last_error TEXT)")

def heartbeat(db,status,now,error=None):
    with db:
        initialize_runtime(db)
        db.execute("INSERT INTO notification_runtime VALUES('mail',?,?,?) ON CONFLICT(worker) DO UPDATE SET status=excluded.status,updated_ms=excluded.updated_ms,last_error=excluded.last_error",(status,now,error))

def mail_status(db,now):
    """Read-only, safe overview. No bodies, addresses, usernames or passwords."""
    row=None
    if db.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='notification_runtime'").fetchone():
        row=db.execute("SELECT status,updated_ms,last_error FROM notification_runtime WHERE worker='mail'").fetchone()
    result={"status":"unreported","heartbeat_age_s":None,"error":None}
    if row:
        age=max(0,(now-row[1])/1000)
        result.update(status=row[0] if age<=45 else "stale",heartbeat_age_s=round(age,1),error=row[2])
    result["counts"]=dict(db.execute("SELECT status,COUNT(*) FROM notifications GROUP BY status").fetchall())
    result["pending_due"]=db.execute("SELECT COUNT(*) FROM notifications WHERE status='pending' AND due_ms<=?",(now,)).fetchone()[0]
    result["recent"]=[dict(zip(("id","category","channel","created_ms","subject","status","attempts","due_ms","sent_ms","last_error"),r)) for r in db.execute("SELECT id,category,channel,created_ms,subject,status,attempts,due_ms,sent_ms,last_error FROM notifications ORDER BY id DESC LIMIT 50")]
    return result

def smtp_error_details(exc):
    out = {"error": type(exc).__name__}
    for attr, label in (("smtp_code", "smtp_code"), ("errno", "errno")):
        value = getattr(exc, attr, None)
        if type(value) is int:
            out[label] = value
    return out


def single_address(value):
    if not isinstance(value, str) or any(ch in value for ch in "\r\n"):
        raise ValueError("Invalid mailbox field")
    addresses = getaddresses([value])
    if len(addresses) != 1 or "@" not in addresses[0][1]:
        raise ValueError("Exactly one mailbox required")
    address = addresses[0][1]
    address.encode("ascii")
    return address


def build_smtp_message(c, subject, body):
    sender, recipient = single_address(c["from"]), single_address(c["to"])
    m = EmailMessage(policy=policy.SMTP)
    m["From"] = c["from"]
    m["To"] = c["to"]
    m["Date"] = formatdate(usegmt=True)
    m["Message-ID"] = make_msgid(domain=sender.rsplit("@", 1)[1])
    m["Subject"] = subject
    m.set_content(body + "\n\n来自NAS培养箱记录服务。", charset="utf-8", cte="base64")
    wire = m.as_bytes(policy=policy.SMTP)
    if not wire.isascii():
        raise ValueError("SMTP wire must be ASCII MIME")
    return sender, recipient, wire


def submit_smtp_message(c, subject, body, report=None):
    """One transaction, no retries. Report only fixed stages and numeric errors.

    DATA disconnect without the final reply is uncertain, never asserted unsent.
    QUIT failure after confirmed DATA acceptance never changes acceptance.
    """
    started = time.monotonic()
    stage = "configuration"
    smtp = None
    acceptance = "not_submitted"

    def record(ok, **detail):
        if report is not None:
            item = {"stage": stage, "ok": ok, "elapsed_ms": round((time.monotonic() - started) * 1000)}
            item.update(detail)
            report(item)

    try:
        if c.get("enabled") is not True or c.get("security") not in ("ssl", "starttls"):
            raise ValueError("SMTP disabled or security invalid")
        password = Path(c["password_file"]).read_text(encoding="utf-8").rstrip("\r\n")
        if not password or "\r" in password or "\n" in password:
            raise ValueError("Password must be one line")
        sender, recipient, wire = build_smtp_message(c, subject, body)
        record(True, bytes=len(wire), mime_transfer="base64", date_header=True, message_id_header=True)
        stage = "connect_tls_banner" if c["security"] == "ssl" else "connect_banner"
        context = ssl.create_default_context()
        kwargs = {"host": c["host"], "port": c["port"], "timeout": 15}
        if c["security"] == "ssl":
            smtp = smtplib.SMTP_SSL(context=context, **kwargs)
        else:
            smtp = smtplib.SMTP(**kwargs)
        record(True, smtp_code=220)
        stage = "ehlo"
        code, _ = smtp.ehlo()
        if code != 250:
            raise smtplib.SMTPHeloError(code, b"")
        record(True, smtp_code=code)
        if c["security"] == "starttls":
            stage = "starttls"
            smtp.starttls(context=context)
            record(True)
            stage = "ehlo_after_tls"
            code, _ = smtp.ehlo()
            if code != 250:
                raise smtplib.SMTPHeloError(code, b"")
            record(True, smtp_code=code)
        stage = "login"
        code, _ = smtp.login(c["username"], password)
        record(True, smtp_code=code)
        stage = "mail_from"
        options = ["size=" + str(len(wire))] if smtp.has_extn("size") else []
        code, _ = smtp.mail(sender, options=options)
        if code != 250:
            raise smtplib.SMTPSenderRefused(code, b"", sender)
        record(True, smtp_code=code)
        stage = "rcpt_to"
        code, _ = smtp.rcpt(recipient)
        if code not in (250, 251):
            raise smtplib.SMTPResponseException(code, b"")
        record(True, smtp_code=code)
        stage = "data"
        acceptance = "unknown"
        try:
            code, _ = smtp.data(wire)
        except smtplib.SMTPDataError:
            acceptance = "rejected"
            raise
        if code != 250:
            acceptance = "rejected"
            raise smtplib.SMTPDataError(code, b"")
        acceptance = "accepted"
        record(True, smtp_code=code, smtp_acceptance=acceptance)
        return {"smtp_acceptance": acceptance, "recipient_receipt_verified": False}
    except (OSError, smtplib.SMTPException, ValueError, UnicodeError) as exc:
        exc.incubator_smtp_stage = stage
        exc.incubator_smtp_acceptance = acceptance
        record(False, smtp_acceptance=acceptance, **smtp_error_details(exc))
        raise
    finally:
        if smtp is not None:
            stage = "quit"
            # After an unsuccessful transaction close directly to preserve its error.
            if acceptance == "accepted":
                try:
                    code, _ = smtp.quit()
                    record(code == 221, smtp_code=code, smtp_acceptance=acceptance)
                except (OSError, smtplib.SMTPException) as exc:
                    record(False, smtp_acceptance=acceptance, **smtp_error_details(exc))
            try:
                smtp.close()
            except (OSError, smtplib.SMTPException) as exc:
                stage = "close"
                record(False, smtp_acceptance=acceptance, **smtp_error_details(exc))

def send(c, subject, body):
    def observe(item):
        if not item['ok'] and item['stage'] in ('quit','close'):
            logging.warning('SMTP cleanup after accepted message: stage=%s error=%s code=%s',item['stage'],item.get('error','SMTPResponse'),item.get('smtp_code',item.get('errno',0)))
    return submit_smtp_message(c,subject,body,observe)

def dispatch(db,c,now,deliver=send):
    if not c["enabled"]:
        return False
    row=db.execute("SELECT id,category,channel,subject,body,attempts,created_ms FROM notifications WHERE status='pending' AND due_ms<=? ORDER BY due_ms,id LIMIT1".replace("LIMIT1","LIMIT 1"),(now,)).fetchone()
    if not row:
        return False
    number,category,ch,subject,body,attempts,created=row
    from datetime import datetime,timezone,timedelta
    stamp=datetime.fromtimestamp(created/1000,timezone(timedelta(hours=8))).isoformat(timespec="seconds")
    body="观测时间（UTC+8）："+stamp+"\n"+body
    key=category+":"+str(ch)
    prior=db.execute("SELECT sent_ms FROM notification_cooldown WHERE key=?",(key,)).fetchone()
    cooldown=int(c.get("cooldown_seconds",600))*1000
    if prior and now-prior[0]<cooldown:
        with db:
            db.execute("UPDATE notifications SET status='suppressed',last_error='cooldown' WHERE id=?",(number,))
        return True
    try:
        deliver(c,subject,body)
    except (OSError,smtplib.SMTPException,ValueError) as exc:
        count=attempts+1
        with db:
            db.execute("UPDATE notifications SET status=?,attempts=?,due_ms=?,last_error=? WHERE id=?",
                       ("failed" if count>=12 else "pending",count,now+min(3600000,60000*2**min(count,6)),error_code(exc),number))
        logging.warning("Notification %s retry/failed (%s)",number,error_code(exc))
        return True
    with db:
        db.execute("UPDATE notifications SET status='sent',attempts=?,sent_ms=?,last_error=NULL WHERE id=?",(attempts+1,now,number))
        db.execute("INSERT INTO notification_cooldown VALUES(?,?) ON CONFLICT(key) DO UPDATE SET sent_ms=excluded.sent_ms",(key,now))
    logging.info("Notification %s sent",number)
    return True

def main():
    p=argparse.ArgumentParser();p.add_argument("--config",default="/app/smtp.json")
    p.add_argument("--db",default="/records/incubator.sqlite3");p.add_argument("--once",action="store_true")
    p.add_argument("--check",action="store_true",help="Read-only config/queue diagnostics; does not connect or send")
    a=p.parse_args()
    if a.check:
        _,check=check_configuration(a.config)
        report={"configuration":check,"smtp_connection_tested":False,"mail_sent":False}
        if Path(a.db).is_file():
            from urllib.parse import quote
            with closing(sqlite3.connect("file:"+quote(Path(a.db).resolve().as_posix(),safe="/:")+"?mode=ro",uri=True,timeout=3)) as db:
                report["worker"]=mail_status(db,time.time_ns()//1000000)
        print(json.dumps(report,ensure_ascii=False))
        return
    if not Path(a.db).is_file():
        raise RuntimeError("Start the updated recorder first")
    db=sqlite3.connect(a.db,timeout=3)
    previous=None
    try:
        while True:
            now=time.time_ns()//1000000
            c,check=check_configuration(a.config)
            status="running" if check["status"]=="ready" else check["status"]
            heartbeat(db,status,now,check["error"])
            change=(status,check["error"])
            if change!=previous:
                logging.info("MAIL status=%s error=%s",status,check["error"] or "none")
                previous=change
            if status=="running":dispatch(db,c,now)
            if a.once: break
            time.sleep(5)
    finally:
        heartbeat(db,"stopped",time.time_ns()//1000000)
        db.close()
if __name__=="__main__":main()
