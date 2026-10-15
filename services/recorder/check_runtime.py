"""Check only the recorder's private dependency and standard library."""
import sqlite3
import sys
from importlib.metadata import version
from pathlib import Path
import paho.mqtt.client as mqtt

vendor = Path(__file__).resolve().parent / "vendor"
library = Path(mqtt.__file__).resolve()
print("Python:", sys.version.split()[0])
print("SQLite:", sqlite3.sqlite_version)
print("paho-mqtt:", version("paho-mqtt"))
print("Library:", library)
if not library.is_relative_to(vendor):
    raise RuntimeError("MQTT library must come from this recorder's vendor folder")
if version("paho-mqtt") != "2.1.0":
    raise RuntimeError("Expected paho-mqtt 2.1.0")
if sqlite3.sqlite_version_info < (3, 24, 0):
    raise RuntimeError("SQLite is too old for recorder UPSERT")
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                     client_id="recorder-check", manual_ack=True)
with sqlite3.connect(":memory:") as db:
    db.execute("CREATE TABLE probe(k TEXT PRIMARY KEY, v INTEGER)")
    db.execute("INSERT INTO probe VALUES('ready',1) ON CONFLICT(k) DO UPDATE SET v=excluded.v")
    assert db.execute("SELECT v FROM probe").fetchone()[0] == 1
print("READY")
