import copy, tempfile, unittest
from pathlib import Path
from record_store import Store
from test_record_store import NOW, OLD
import optical_store
def data():
 return {"boot_id":OLD,"uptime_ms":1000,"sampled_uptime_ms":1000,"sample_epoch_ms":NOW,"sample_time_trusted":True,"sensor_valid":True,"temperature":25,"humidity":50,"photo_epoch_ms":NOW,"photo_time_trusted":True,"photo":{"compiled":True,"sampled_ms":990,"automatic":{"active":False},"channels":[{"raw":2000,"level":500,"expected":1000,"ratio":500,"calibrated":True,"expected_valid":True,"enabled":True,"on":True,"fault":True,"status":4} for i in range(2)]}}
class Tests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.s=Store(Path(self.tmp.name)/"a.db")
 def tearDown(self):self.s.close();self.tmp.cleanup()
 def count(self,t):return self.s.db.execute("SELECT COUNT(*) FROM "+t).fetchone()[0]
 def test_photo_durable_dedupe(self):
  self.s.state(data(),NOW);self.s.state(data(),NOW+1);self.assertEqual(self.count("light_samples"),2);self.assertEqual(self.count("notifications"),2)
  self.s.close();self.s=Store(Path(self.tmp.name)/"a.db");self.assertEqual(self.count("light_samples"),2)
 def test_retained_not_sample_or_alert(self):
  self.s.state(data(),NOW,True);self.assertEqual(self.count("light_samples"),0);self.assertEqual(self.count("notifications"),0)
 def test_uncalibrated_alarm_disabled(self):
  d=data()
  for c in d["photo"]["channels"]:c["calibrated"]=False
  self.s.state(d,NOW);self.assertEqual(self.count("light_samples"),2);self.assertEqual(self.count("notifications"),0)
 def test_delayed_not_alarm(self):
  self.s.state(data(),NOW+30001);self.assertEqual(self.count("light_samples"),2);self.assertEqual(self.count("notifications"),0)
 def test_calibrating_not_alarm(self):
  d=data();d["photo"]["automatic"]["active"]=True;self.s.state(d,NOW);self.assertEqual(self.count("notifications"),0)
 def test_bad_second_channel_rolls_back(self):
  d=data();d["photo"]["channels"][1]["raw"]=4096
  with self.assertRaises(ValueError):self.s.state(d,NOW)
  self.assertEqual(self.count("light_samples"),0);self.assertEqual(self.count("messages"),0)
 def test_repeated_fault_no_flood(self):
  for i in range(20):
   d=data();d["photo"]["sampled_ms"]+=i;self.s.state(d,NOW+i*100)
  self.assertEqual(self.count("notifications"),2)
 def test_fault_returns_records_new_event(self):
  self.s.state(data(),NOW);d=data()
  for c in d["photo"]["channels"]:c["fault"]=False;c["status"]=2
  d["photo"]["sampled_ms"]+=1;self.s.state(d,NOW+1);d=data();d["photo"]["sampled_ms"]+=2;self.s.state(d,NOW+2);self.assertEqual(self.count("notifications"),4)
 def test_event_id_dedupe(self):
  d={"event_id":OLD+"-12","boot_id":OLD,"kind":"calibration_point","run":"a1b2c3-c12345678","channel":1,"frequency":1000,"duty":20,"raw":450,"cross_raw":10,"epoch_ms":NOW,"time_trusted":True}
  self.s.event("events",d,NOW);self.s.event("events",d,NOW+1);self.assertEqual(self.count("messages"),1);self.assertEqual(self.count("calibration_points"),1)
 def test_calibration_bounds_transactional(self):
  d={"event_id":OLD+"-12","boot_id":OLD,"kind":"calibration_point","run":"wrong","channel":1,"frequency":0,"duty":0,"raw":450,"cross_raw":10,"epoch_ms":NOW,"time_trusted":True}
  with self.assertRaises(ValueError):self.s.event("events",d,NOW)
  self.assertEqual(self.count("messages"),0)
 def test_new_boot_notice_once(self):
  from datetime import datetime, timezone, timedelta
  d=data();d.update(time_source="NTP",time=datetime.fromtimestamp(NOW/1000,timezone(timedelta(hours=8))).strftime("%Y-%m-%d %H:%M:%S"))
  self.s.state(d,NOW);d["boot_id"]="a1b2c3-24-8ed493ff";self.s.state(d,NOW+1000);self.s.state(d,NOW+2000)
  self.assertEqual(self.s.db.execute("SELECT COUNT(*) FROM notifications WHERE category='boot'").fetchone()[0],1)
 def test_retained_online_baseline_without_notification(self):
  self.s.availability("availability","online",NOW,True);self.assertEqual(self.count("notifications"),0)
  self.s.availability("availability","offline",NOW+1);self.assertEqual(self.count("notifications"),1)
 def test_short_offline_cancel(self):
  self.s.availability("availability","online",NOW);self.s.availability("availability","offline",NOW+100);self.s.availability("availability","online",NOW+59000);self.assertEqual(self.s.db.execute("SELECT status FROM notifications").fetchone()[0],"cancelled")
 def test_long_offline_online(self):
  self.s.availability("availability","online",NOW);self.s.availability("availability","offline",NOW+100);self.s.availability("availability","online",NOW+60100);self.assertEqual(self.count("notifications"),2)
 def test_csv(self):
  self.s.state(data(),NOW);optical_store.export(self.s.db,self.tmp.name);self.assertIn("raw",Path(self.tmp.name,"light_samples.csv").read_text(encoding="utf-8-sig"))


 def test_nas_clock_uses_epoch_independent_of_display_timezone(self):
  d=data();d.update(time_source="NAS",epoch_ms=NOW,time="2026-01-01 00:00:00")
  self.s.state(d,NOW);d["boot_id"]="a1b2c3-24-8ed493ff";self.s.state(d,NOW+1000)
  self.assertEqual(self.s.db.execute("SELECT COUNT(*) FROM notifications WHERE category='boot'").fetchone()[0],1)
 def test_invalid_absolute_clock_cannot_authorize_boot_notice(self):
  d=data();d.update(time_source="NAS",epoch_ms=0,time="2026-01-01 00:00:00")
  self.s.state(d,NOW);self.assertEqual(self.s.db.execute("SELECT COUNT(*) FROM device_boot").fetchone()[0],0)

if __name__=="__main__":unittest.main()
