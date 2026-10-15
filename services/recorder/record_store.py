"""Durable progress backups and experimental records. No lamp command privileges."""
import csv
import hashlib
import json
import math
import re
import sqlite3
from pathlib import Path
from datetime import datetime, timezone, timedelta
import sample_journal
import optical_store
import nas_analysis

MIN_EPOCH = 1704067200000
MAX_EPOCH = 4102444799999
MAX_ELAPSED = 19353600000000000
BOOT = re.compile(r"^[0-9a-f]{6}-[1-9][0-9]{0,9}-[0-9a-f]{8}$")
NONCE = re.compile(r"^[0-9a-f]{16}$")

def integer(value, minimum=0, maximum=2**63-1):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError("integer outside protocol bounds")
    return value

def decimal(value):
    if not isinstance(value, str) or not re.fullmatch(r"0|[1-9][0-9]{0,19}", value):
        raise ValueError("elapsed must be an exact decimal string")
    return integer(int(value), 0, MAX_ELAPSED)

def boolean(value):
    if type(value) is not bool:
        raise ValueError("boolean required")
    return value

def boot(value, device):
    if not isinstance(value, str) or not BOOT.fullmatch(value) or len(value) >= 40 or not value.startswith(device + "-"):
        raise ValueError("boot identity mismatch")
    return value

def decode(payload):
    if len(payload) > 4096:
        raise ValueError("payload exceeds bounded protocol size")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result
    value = json.loads(payload, object_pairs_hook=pairs, parse_constant=lambda _: (_ for _ in ()).throw(ValueError("nonfinite JSON")))
    if not isinstance(value, dict):
        raise ValueError("object required")
    return value

def compact(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)

class Store:
    def __init__(self, path, device="a1b2c3"):
        if not re.fullmatch(r"[0-9a-f]{6}", device):
            raise ValueError("invalid device")
        self.device = device
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(path, timeout=3)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.executescript("""
        CREATE TABLE IF NOT EXISTS snapshots(
          device TEXT, boot TEXT, cfg INTEGER, seq INTEGER, epoch_ms INTEGER,
          received_ms INTEGER, elapsed1 TEXT, elapsed2 TEXT, running INTEGER, armed INTEGER,
          payload TEXT, PRIMARY KEY(device,boot,cfg));
        CREATE TABLE IF NOT EXISTS messages(
          id INTEGER PRIMARY KEY, topic TEXT, received_ms INTEGER, retained INTEGER,
          record_key TEXT UNIQUE, payload TEXT);
        CREATE TABLE IF NOT EXISTS sensor_samples(
          boot TEXT, sample_uptime INTEGER, epoch_ms INTEGER, received_ms INTEGER,
          temperature REAL, humidity REAL, valid INTEGER, trusted INTEGER,
          PRIMARY KEY(boot,sample_uptime));
        CREATE TABLE IF NOT EXISTS recovery_requests(
          device TEXT, boot TEXT, req TEXT, received_ms INTEGER, request TEXT, reply TEXT,
          PRIMARY KEY(device,boot,req));
        CREATE TABLE IF NOT EXISTS recovery_decisions(
          device TEXT, boot TEXT PRIMARY KEY, received_ms INTEGER, source TEXT,
          estimated_outage_ms INTEGER, startup_wait_ms INTEGER, req TEXT);
        CREATE TABLE IF NOT EXISTS state_throttle(
          boot TEXT PRIMARY KEY, received_ms INTEGER, signature TEXT);
        """)
        optical_store.initialize(self.db)
        nas_analysis.initialize(self.db)
        sample_journal.initialize(self.db)

    def close(self):
        self.db.close()

    def message(self, topic, data, received_ms, retained=False, key=None):
        if key is None:
            key = topic + ":" + str(received_ms) + ":" + hashlib.sha256(compact(data).encode()).hexdigest()
        self.db.execute("INSERT OR IGNORE INTO messages(topic,received_ms,retained,record_key,payload) VALUES(?,?,?,?,?)",
                        (topic, received_ms, int(retained), key, compact(data)))

    def progress(self, data, received_ms, retained=False):
        if retained:
            return "ignored retained progress"
        if integer(data.get("v"), 1, 1) != 1:
            raise ValueError("version")
        identity = boot(data.get("boot"), self.device)
        cfg = integer(data.get("cfg"), 1, 2**32-1)
        seq = integer(data.get("seq"))
        trusted = boolean(data.get("trusted"))
        waiting = boolean(data.get("waiting"))
        armed = boolean(data.get("armed"))
        running = integer(data.get("running"), 0, 3)
        elapsed = [decimal(data.get("elapsed1_ms")), decimal(data.get("elapsed2_ms"))]
        epoch = integer(data.get("epoch_ms"), 0, MAX_EPOCH)
        if running and not armed:
            raise ValueError("running without permission")
        if not trusted or waiting or epoch < MIN_EPOCH or abs(received_ms-epoch) > 30000:
            return "ignored untrusted/waiting/delayed progress"
        with self.db:
            self.db.execute("""INSERT INTO snapshots VALUES(?,?,?,?,?,?,?,?,?,?,?)
              ON CONFLICT(device,boot,cfg) DO UPDATE SET seq=excluded.seq,epoch_ms=excluded.epoch_ms,
              received_ms=excluded.received_ms,elapsed1=excluded.elapsed1,elapsed2=excluded.elapsed2,
              running=excluded.running,armed=excluded.armed,payload=excluded.payload
              WHERE excluded.seq>snapshots.seq AND excluded.epoch_ms>=snapshots.epoch_ms""",
              (self.device,identity,cfg,seq,epoch,received_ms,str(elapsed[0]),str(elapsed[1]),running,int(armed),compact(data)))
            source = data.get("recovery_source")
            req = data.get("recovery_req", "")
            if source in ("NAS", "LOCAL", "MIXED"):
                if req and (not isinstance(req, str) or not NONCE.fullmatch(req)):
                    raise ValueError("invalid decision request")
                outage = integer(data.get("estimated_outage_ms"), 0, MAX_EPOCH)
                startup = integer(data.get("startup_wait_ms"))
                decision = self.db.execute("INSERT OR IGNORE INTO recovery_decisions VALUES(?,?,?,?,?,?,?)",
                                (self.device,identity,received_ms,source,outage,startup,req))
                if decision.rowcount:
                    optical_store.enqueue(self.db,"boot:"+identity,"recovery",0,received_ms,"培养箱重启恢复判定",f"设备 {self.device}；来源 {source}；估算断电间隔 {outage/1000:.1f}秒；上电等待 {startup/1000:.1f}秒；复位原因 {data.get('reset_reason','UNKNOWN')}。检查点间隔含测量误差，复位原因不能单独证明停电。")
        return "saved progress"

    def response(self, request, received_ms, retained=False):
        if retained:
            raise ValueError("retained recovery request rejected")
        expected = {"v", "req", "boot", "previous", "cfg", "min_epoch_ms"}
        if set(request) != expected:
            raise ValueError("recovery request schema")
        integer(request["v"], 1, 1)
        current = boot(request["boot"], self.device)
        previous = boot(request["previous"], self.device)
        if current == previous:
            raise ValueError("same boot is a reconnect, not reboot recovery")
        req = request["req"]
        if not isinstance(req, str) or not NONCE.fullmatch(req):
            raise ValueError("invalid nonce")
        cfg = integer(request["cfg"], 1, 2**32-1)
        minimum = integer(request["min_epoch_ms"], MIN_EPOCH, MAX_EPOCH)
        original = compact(request)
        with self.db:
            cached = self.db.execute("SELECT request,reply FROM recovery_requests WHERE device=? AND boot=? AND req=?",
                                     (self.device,current,req)).fetchone()
            if cached:
                if cached[0] != original:
                    raise ValueError("conflicting replay of recovery request")
                return cached[1]
            row = self.db.execute("SELECT epoch_ms,elapsed1,elapsed2,running,armed FROM snapshots WHERE device=? AND boot=? AND cfg=?",
                                  (self.device,previous,cfg)).fetchone()
            found = bool(row and minimum <= row[0] <= received_ms)
            # Insertion order is the firmware's bounded flat JSON wire contract.
            reply = {"v":1,"req":req,"boot":current,"previous":previous,"cfg":cfg,"found":found,
                     "epoch_ms":row[0] if found else 0,"elapsed1_ms":row[1] if found else "0",
                     "elapsed2_ms":row[2] if found else "0","running":row[3] if found else 0,
                     "armed":bool(row[4]) if found else False}
            encoded = compact(reply)
            if len(encoded.encode()) >= 512:
                raise ValueError("reply too large")
            self.db.execute("INSERT INTO recovery_requests VALUES(?,?,?,?,?,?)",
                            (self.device,current,req,received_ms,original,encoded))
            return encoded

    def state(self, data, received_ms, retained=False):
        identity = boot(data.get("boot_id"), self.device)
        if data.get("photo",{}).get("schema")=="raw-v2" and self.db.execute("SELECT 1 FROM nas_retired_boots WHERE device=? AND boot=?",(self.device,identity)).fetchone():
            return "ignored retired boot"
        signature = compact({key:data.get(key) for key in ("armed","l1","l2","mode1","mode2","pwm1","pwm2","frequency1","frequency2","duty1","duty2","sensor_valid","temperature","humidity","transitions1","transitions2","last_switch1_ms","last_switch2_ms")})
        with self.db:
            prior = self.db.execute("SELECT received_ms,signature FROM state_throttle WHERE boot=?",(identity,)).fetchone()
            if not prior or signature != prior[1] or received_ms-prior[0] >= 10000:
                self.message("incubator/"+self.device+"/state",data,received_ms,retained,
                             "state:"+identity+":"+str(integer(data.get("uptime_ms"))))
                self.db.execute("INSERT INTO state_throttle VALUES(?,?,?) ON CONFLICT(boot) DO UPDATE SET received_ms=excluded.received_ms,signature=excluded.signature",(identity,received_ms,signature))
            optical_store.observe_boot(self.db,self.device,identity,data,received_ms,retained)
            if data.get("photo",{}).get("schema")=="raw-v2":
                nas_analysis.state(self.db,self.device,identity,data,received_ms,retained)
            else:
                optical_store.state(self.db,self.device,identity,data,received_ms,retained)
            # Retained state is useful to show history, but is not a new sensor sample or recovery backup.
            if not retained:
                sample = integer(data.get("sampled_uptime_ms"))
                trusted = boolean(data.get("sample_time_trusted"))
                epoch = integer(data.get("sample_epoch_ms"), 0, MAX_EPOCH)
                valid = boolean(data.get("sensor_valid"))
                values = [data.get("temperature"),data.get("humidity")]
                if valid:
                    for value in values:
                        if type(value) not in (int,float) or not math.isfinite(value):
                            raise ValueError("nonfinite sensor value")
                    if not -100 <= values[0] <= 200 or not 0 <= values[1] <= 100:
                        raise ValueError("sensor value outside bounds")
                else:
                    values = [None,None]
                self.db.execute("INSERT OR IGNORE INTO sensor_samples VALUES(?,?,?,?,?,?,?,?)",
                                (identity,sample,epoch,received_ms,*values,int(valid),int(trusted)))

    def event(self, topic, data, received_ms, retained=False):
        event_id = data.get("event_id", data.get("id"))
        key = topic+":"+event_id if isinstance(event_id,str) and len(event_id)<100 else None
        with self.db:
            self.message(topic,data,received_ms,retained,key)
            optical_store.event(self.db, self.device, data, received_ms, retained)
            if not retained:nas_analysis.calibration(self.db,self.device,data,received_ms)

    def availability(self, topic, value, received_ms, retained=False):
        if value not in ("online","offline"):
            raise ValueError("availability value")
        with self.db:
            self.message(topic,{"availability":value,"classification":"connection-status-only"},received_ms,retained)
            # raw-v2 uses the fresh-data watchdog, avoiding two notices for one outage.
            if not self.db.execute("SELECT 1 FROM nas_live WHERE device=?",(self.device,)).fetchone():
                optical_store.availability(self.db, self.device, value, received_ms, retained)

    def export_samples(self, filename):
        with open(filename,"w",encoding="utf-8-sig",newline="") as out:
            writer=csv.writer(out)
            writer.writerow(["sample_time_UTC8","received_time_UTC8","boot_id","sample_uptime_ms","source_epoch_ms","received_epoch_ms","temperature_C","humidity_percent","sensor_valid","time_trusted"])
            origin=datetime(1970,1,1,tzinfo=timezone.utc)
            def text(ms):
                return (origin+timedelta(milliseconds=ms)).astimezone(timezone(timedelta(hours=8))).isoformat(timespec="milliseconds")
            for row in self.db.execute("SELECT * FROM sensor_samples ORDER BY received_ms,boot,sample_uptime"):
                writer.writerow([text(row[2]) if row[7] and row[2]>=MIN_EPOCH else "",text(row[3]),*row])
