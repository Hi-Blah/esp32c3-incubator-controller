"""Send exactly one explicitly requested test email. No database or queue access."""
import argparse
import json
import smtplib
import ssl
import time
import uuid
from email import policy
from email.message import EmailMessage
from email.utils import formatdate, getaddresses, make_msgid
from pathlib import Path

# BEGIN SHARED SMTP CORE
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
# END SHARED SMTP CORE


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--config", default="/app/smtp.json")
    p.add_argument("--send-test", action="store_true", help="Send one test to the locally configured recipient")
    a = p.parse_args(argv)
    if not a.send_test:
        print(json.dumps({"result": "NO_ACTION", "mail_sent": False, "message": "Use --send-test to explicitly send one test email"}))
        return 0
    test_id = uuid.uuid4().hex[:12]
    try:
        c = json.loads(Path(a.config).read_text(encoding="utf-8-sig"))
        result = submit_smtp_message(c, "[培养箱测试] SMTP单封投递检查 " + test_id,
                                     "这是一封用于核对培养箱报警邮件投递的测试邮件。\n测试编号：" + test_id +
                                     "\n它不是新的设备故障，也不会修改既有通知队列。",
                                     lambda item: print(json.dumps(item, ensure_ascii=False), flush=True))
        print(json.dumps({"result": "SMTP_ACCEPTED", "test_id": test_id, "database_modified": False, **result}, ensure_ascii=False))
        return 0
    except (OSError, smtplib.SMTPException, ValueError, UnicodeError, KeyError, TypeError) as exc:
        acceptance = getattr(exc, "incubator_smtp_acceptance", "not_submitted")
        print(json.dumps({"result": "UNKNOWN" if acceptance == "unknown" else "FAILED", "test_id": test_id,
                          "failed_stage": getattr(exc, "incubator_smtp_stage", "configuration"),
                          "smtp_acceptance": acceptance, "database_modified": False, **smtp_error_details(exc)}, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
