"""Optical samples, empirical calibration points and durable notifications.
Percentages are relative ADC response, never lux or linear light power."""
import json
import csv
import math

STATUS = ("UNCONFIGURED", "MONITORING_DISABLED", "OK", "MISSING_LIGHT",
          "DIM_LIGHT", "STUCK_ON", "PWM_MODEL_REQUIRED", "CALIBRATING", "LOW_DUTY_ESTIMATE")

def integer(v, lo=0, hi=2**63-1):
    if type(v) is not int or not lo <= v <= hi:
        raise ValueError("optical integer bounds")
    return v

def boolean(v):
    if type(v) is not bool:
        raise ValueError("optical boolean")
    return v

def initialize(db):
    db.executescript("""
    CREATE TABLE IF NOT EXISTS light_samples(
     boot TEXT, sample_uptime INTEGER, channel INTEGER, epoch_ms INTEGER, received_ms INTEGER,
     raw INTEGER, level INTEGER, expected INTEGER, ratio INTEGER, calibrated INTEGER,
     expected_valid INTEGER, enabled INTEGER, detected INTEGER, fault INTEGER, status TEXT,
     trusted INTEGER, payload TEXT, PRIMARY KEY(boot,sample_uptime,channel));
    CREATE TABLE IF NOT EXISTS calibration_points(
     event_id TEXT PRIMARY KEY, device TEXT, boot TEXT, run TEXT, channel INTEGER,
     frequency INTEGER, duty INTEGER, raw INTEGER, cross_raw INTEGER,
     epoch_ms INTEGER, received_ms INTEGER, trusted INTEGER);
    CREATE TABLE IF NOT EXISTS alert_state(
     key TEXT PRIMARY KEY, active INTEGER, status TEXT, changed_ms INTEGER);
    CREATE TABLE IF NOT EXISTS notifications(
     id INTEGER PRIMARY KEY, key TEXT UNIQUE, category TEXT, channel INTEGER, created_ms INTEGER,
     due_ms INTEGER, subject TEXT, body TEXT, status TEXT DEFAULT 'pending',
     attempts INTEGER DEFAULT 0, sent_ms INTEGER, last_error TEXT);
    CREATE TABLE IF NOT EXISTS device_boot(device TEXT PRIMARY KEY, boot TEXT);
    CREATE TABLE IF NOT EXISTS connection_state(
     device TEXT PRIMARY KEY, online INTEGER, changed_ms INTEGER);
    CREATE TABLE IF NOT EXISTS notification_cooldown(key TEXT PRIMARY KEY,sent_ms INTEGER);
    """)

def enqueue(db, key, category, ch, now, subject, body, delay=0):
    db.execute("INSERT OR IGNORE INTO notifications(key,category,channel,created_ms,due_ms,subject,body) VALUES(?,?,?,?,?,?,?)",
               (key, category, ch, now, now+delay, subject, body))

def observe_boot(db,device,identity,data,now,retained):
    if retained or data.get("time_source") not in ("NTP","NAS"):
        return
    if "epoch_ms" in data:
        stamp=data["epoch_ms"]
        if type(stamp)is not int or not 1704067200000<=stamp<=4102444799999:return
    else:
        # Legacy payloads used the original UTC+8 display string. New firmware sends UTC epoch.
        from datetime import datetime,timezone,timedelta
        try:
            stamp=int(datetime.strptime(data["time"],"%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone(timedelta(hours=8))).timestamp()*1000)
        except (ValueError,TypeError,KeyError):
            return
    if abs(now-stamp)>30000:
        return
    prior=db.execute("SELECT boot FROM device_boot WHERE device=?",(device,)).fetchone()
    if prior and prior[0]!=identity:
        enqueue(db,"boot:"+identity,"boot",0,now,"培养箱观察到重启",
                f"设备 {device}；新启动 {identity}；复位原因 {data.get('reset_reason','UNKNOWN')}。可能是断电、RST或其他复位，无法只凭复位原因确认意外断电。")
    db.execute("INSERT INTO device_boot VALUES(?,?) ON CONFLICT(device) DO UPDATE SET boot=excluded.boot",(device,identity))

def state(db, device, identity, data, now, retained):
    photo = data.get("photo")
    if not photo or retained:
        return
    if not isinstance(photo, dict):
        raise ValueError("optical object")
    if not boolean(photo.get("compiled")):
        return
    channels = photo.get("channels")
    if not isinstance(channels, list) or len(channels) != 2:
        raise ValueError("two optical channels required")
    sample = integer(photo.get("sampled_ms"))
    epoch = integer(data.get("photo_epoch_ms"), 0, 4102444799999)
    trusted = boolean(data.get("photo_time_trusted"))
    automatic = photo.get("automatic", {}).get("active", False)
    boolean(automatic)
    rows=[]
    for ch, c in enumerate(channels, 1):
        if not isinstance(c, dict):
            raise ValueError("optical channel object")
        raw=integer(c.get("raw"),0,4095)
        level=integer(c.get("level"),0,1000)
        expected=integer(c.get("expected"),0,1000)
        ratio=integer(c.get("ratio"),0,10000)
        calibrated=boolean(c.get("calibrated")); known=boolean(c.get("expected_valid"))
        enabled=boolean(c.get("enabled")); detected=boolean(c.get("on")); fault=boolean(c.get("fault"))
        status=STATUS[integer(c.get("status"),0,len(STATUS)-1)]
        rows.append((identity,sample,ch,epoch,now,raw,level,expected,ratio,int(calibrated),
                     int(known),int(enabled),int(detected),int(fault),status,int(trusted),
                     json.dumps(c,ensure_ascii=False,separators=(",",":"))))
    db.executemany("INSERT OR IGNORE INTO light_samples VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",rows)
    # Late durable history is useful, but cannot trigger a present-time fault.
    if not trusted or abs(now-epoch)>30000 or automatic:
        return
    for row in rows:
        ch=row[2]; status=row[14]; active=bool(row[13] and row[9] and row[11] and data.get("mode"+str(ch),1) in (1,2)); key=device+":photo"+str(ch)
        prior=db.execute("SELECT active,status FROM alert_state WHERE key=?",(key,)).fetchone()
        if active and (not prior or not prior[0] or prior[1]!=status):
            enqueue(db,key+":"+str(now), "photo",ch,now, "培养箱光源"+str(ch)+"反馈异常",
                    f"设备 {device}；光源 {ch}；状态 {status}；ADC {row[5]}；相对响应 {row[6]/10:.1f}%；预期 {row[7]/10:.1f}%；达标率 {row[8]/10:.1f}%。请检查24V供电、灯带及传感器遮挡/位置。读数不是照度。")
        if prior is None or bool(prior[0])!=active or prior[1]!=status:
            db.execute("INSERT INTO alert_state VALUES(?,?,?,?) ON CONFLICT(key) DO UPDATE SET active=excluded.active,status=excluded.status,changed_ms=excluded.changed_ms",(key,int(active),status,now))

def event(db, device, data, now, retained):
    if retained or data.get("kind")!="calibration_point":
        return
    from record_store import boot
    identity=boot(data.get("boot_id"),device)
    event_id=data.get("event_id")
    run=data.get("run")
    if not isinstance(event_id,str) or not event_id.startswith(identity+"-") or len(event_id)>90:
        raise ValueError("calibration event identity")
    import re
    if not isinstance(run,str) or not re.fullmatch(device+r"-c[0-9a-f]{8}",run):
        raise ValueError("calibration run identity")
    ch=integer(data.get("channel"),1,2);hz=integer(data.get("frequency"),0,20000)
    duty=integer(data.get("duty"),0,100);raw=integer(data.get("raw"),0,4095)
    cross=integer(data.get("cross_raw"),0,4095);epoch=integer(data.get("epoch_ms"),0,4102444799999)
    trusted=boolean(data.get("time_trusted"))
    db.execute("INSERT OR IGNORE INTO calibration_points VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
               (event_id,device,identity,run,ch,hz,duty,raw,cross,epoch,now,int(trusted)))

def availability(db, device, value, now, retained):
    if retained:
        if value=="online":
            db.execute("INSERT OR IGNORE INTO connection_state VALUES(?,?,?)",(device,1,now))
        return
    online=value=="online"
    prior=db.execute("SELECT online,changed_ms FROM connection_state WHERE device=?",(device,)).fetchone()
    if not prior or bool(prior[0])!=online:
        db.execute("INSERT INTO connection_state VALUES(?,?,?) ON CONFLICT(device) DO UPDATE SET online=excluded.online,changed_ms=excluded.changed_ms",(device,int(online),now))
        if prior and not online:
            enqueue(db,device+":offline:"+str(now),"notice",0,now,"[提示] 培养箱连接中断",
                    f"设备 {device} 的MQTT连接中断。无法仅据此判定停电，也可能是Wi-Fi、路由器、NAS或校园网络故障。",60000)
        elif prior and online:
            duration=now-prior[1]
            if duration<60000:
                db.execute("UPDATE notifications SET status='cancelled',last_error='connection recovered within60s' WHERE key=? AND status='pending'",(device+":offline:"+str(prior[1]),))
            else:
                enqueue(db,device+":online:"+str(now),"notice",0,now,"[提示] 培养箱连接恢复",
                        f"设备 {device} 的MQTT连接已恢复；观测连接中断约 {duration/1000:.1f}秒。这个时长不是准确停电时长。")

def export(db, directory):
    from pathlib import Path
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    exports={"light_samples.csv":"SELECT * FROM light_samples ORDER BY received_ms,boot,sample_uptime,channel",
             "calibration_points.csv":"SELECT * FROM calibration_points ORDER BY received_ms,event_id",
             "notifications.csv":"SELECT * FROM notifications ORDER BY created_ms,id",
             "raw_calibration.csv":"SELECT * FROM raw_calibration ORDER BY event_id"}
    for name,query in exports.items():
        cursor=db.execute(query)
        with (directory/name).open("w",encoding="utf-8-sig",newline="") as stream:
            writer=csv.writer(stream);writer.writerow([c[0] for c in cursor.description]);writer.writerows(cursor)
