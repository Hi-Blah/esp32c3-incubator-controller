import copy
import json
import re
import sqlite3
import tempfile
import unittest
from pathlib import Path
from record_store import Store, decode, compact

NOW=1791871200000
OLD="a1b2c3-23-855a6638"
NEW="a1b2c3-24-8ed493ff"
REQ="0123456789abcdef"
def progress(**changes):
    d={"v":1,"boot":OLD,"cfg":7,"seq":10000,"epoch_ms":NOW,"trusted":True,"waiting":False,"armed":True,"running":3,"elapsed1_ms":"70000","elapsed2_ms":"60000","recovery_source":"NONE","recovery_req":"","estimated_outage_ms":0,"startup_wait_ms":0,"reset_reason":"USB_RESET"}
    d.update(changes)
    return d

def request(**changes):
    d={"v":1,"req":REQ,"boot":NEW,"previous":OLD,"cfg":7,"min_epoch_ms":NOW-10000}
    d.update(changes)
    return d

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.path=Path(self.tmp.name)/"records.sqlite3"
        self.s=Store(self.path)
    def tearDown(self):
        self.s.close()
        self.tmp.cleanup()
    def saved(self, **changes):
        self.s.progress(progress(**changes),NOW)
    def response(self, **changes):
        return decode(self.s.response(request(**changes),NOW+100000).encode())
    def test_durable_reopen(self):
        self.saved()
        self.s.close();self.s=Store(self.path)
        self.assertTrue(self.response()["found"])
    def test_new_boot_does_not_overwrite_previous(self):
        self.saved();self.s.progress(progress(boot=NEW,seq=1,armed=False,running=0),NOW)
        self.assertEqual(self.response()["elapsed1_ms"],"70000")
    def test_dedupe_and_out_of_order(self):
        self.saved();self.saved(seq=9999,elapsed1_ms="1");self.saved(seq=10000,elapsed1_ms="2")
        self.assertEqual(self.response()["elapsed1_ms"],"70000")
    def test_time_regression_ignored(self):
        self.saved();self.s.progress(progress(seq=11000,epoch_ms=NOW-1,elapsed1_ms="1"),NOW)
        self.assertEqual(self.response()["elapsed1_ms"],"70000")
    def test_retained_backup_rejected(self):
        self.s.progress(progress(),NOW,True)
        self.assertFalse(self.response()["found"])
    def test_untrusted_or_waiting_not_backup(self):
        for change in ({"trusted":False},{"waiting":True},{"epoch_ms":0}):
            self.s.progress(progress(**change),NOW)
        self.assertFalse(self.response()["found"])
    def test_delayed_more_than30s_not_backup(self):
        self.s.progress(progress(),NOW+30001)
        self.assertFalse(self.response()["found"])
    def test_integer_precision_server_to_firmware(self):
        self.saved(elapsed1_ms="18000000000000001",elapsed2_ms="70000")
        encoded=self.s.response(request(),NOW+100000)
        self.assertEqual(decode(encoded.encode())["elapsed1_ms"],"18000000000000001")
        cpp=Path(__file__).resolve().parents[2]/"firmware/IncubatorConnected/recovery_protocol_tests.cpp"
        if cpp.is_file():
            golden=re.search(r'VALID=R"JSON\((.*?)\)JSON"',cpp.read_text(encoding="utf-8"),re.S).group(1)
            self.assertEqual(encoded,golden)
    def test_replay_idempotent(self):
        self.saved();a=self.s.response(request(),NOW+100000)
        self.s.progress(progress(seq=20000,elapsed1_ms="90000"),NOW)
        self.assertEqual(a,self.s.response(request(),NOW+110000))
    def test_conflicting_nonce_rejected(self):
        self.saved();self.response()
        with self.assertRaises(ValueError):
            self.response(cfg=8)
    def test_program_revision_mismatch(self):
        self.saved();self.assertFalse(self.response(cfg=8)["found"])
    def test_older_than_local_falls_back(self):
        self.saved();self.assertFalse(self.response(min_epoch_ms=NOW+1)["found"])
    def test_same_boot_not_recovery(self):
        with self.assertRaises(ValueError):
            self.response(boot=OLD)
    def test_retained_request_rejected(self):
        with self.assertRaises(ValueError):
            self.s.response(request(),NOW+100000,True)
    def test_snapshot_can_cancel_old_running_permission(self):
        self.saved(armed=False,running=0)
        r=self.response();self.assertTrue(r["found"]);self.assertFalse(r["armed"]);self.assertEqual(r["running"],0)
    def test_invalid_types_and_overflow(self):
        for change in ({"cfg":True},{"elapsed1_ms":70000},{"elapsed1_ms":"-1"},{"elapsed1_ms":"18446744073709551616"},{"running":4},{"armed":False},{"boot":"another-device"}):
            with self.subTest(change=change),self.assertRaises(ValueError):
                self.saved(**change)
    def test_reply_order_and_size(self):
        self.saved();r=self.s.response(request(),NOW+100000)
        self.assertLess(len(r.encode()),512)
        self.assertEqual(list(decode(r.encode())),["v","req","boot","previous","cfg","found","epoch_ms","elapsed1_ms","elapsed2_ms","running","armed"])
    def test_connection_status_not_used_as_power_timestamp(self):
        self.saved();self.s.availability("incubator/a1b2c3/availability","offline",NOW+65000)
        self.assertEqual(self.response()["epoch_ms"],NOW)
    def test_transaction_rolls_back_snapshot_on_decision_write_failure(self):
        self.s.db.execute("CREATE TRIGGER deny_decision BEFORE INSERT ON recovery_decisions BEGIN SELECT RAISE(ABORT,'disk error'); END;")
        with self.assertRaises(sqlite3.Error):
            self.saved(recovery_source="NAS",recovery_req=REQ)
        self.assertEqual(self.s.db.execute("SELECT COUNT(*) FROM snapshots").fetchone()[0],0)
    def test_duplicate_json_rejected(self):
        with self.assertRaises(ValueError):
            decode(b'{"cfg":1,"cfg":2}')
    def test_state_samples_deduplicate_and_export(self):
        d={"boot_id":OLD,"uptime_ms":10000,"sampled_uptime_ms":9990,"sample_epoch_ms":NOW,"sample_time_trusted":True,"sensor_valid":True,"temperature":27.1,"humidity":44,"armed":False,"l1":False,"l2":False}
        self.s.state(d,NOW);self.s.state(d,NOW+2000)
        self.assertEqual(self.s.db.execute("SELECT COUNT(*) FROM sensor_samples").fetchone()[0],1)
        out=Path(self.tmp.name)/"samples.csv";self.s.export_samples(out)
        self.assertIn("27.1",out.read_text(encoding="utf-8-sig"))
        self.assertIn("+08:00",out.read_text(encoding="utf-8-sig"))
    def test_retained_state_not_new_sample(self):
        d={"boot_id":OLD,"uptime_ms":10000,"armed":False}
        self.s.state(d,NOW,True)
        self.assertEqual(self.s.db.execute("SELECT COUNT(*) FROM sensor_samples").fetchone()[0],0)
    def test_multiple_boot_requests(self):
        self.saved()
        for i in range(24,28):
            r=self.response(boot=f"a1b2c3-{i}-8ed493ff",req=f"{i:016x}")
            self.assertEqual(r["previous"],OLD)
        self.assertEqual(self.s.db.execute("SELECT COUNT(*) FROM recovery_requests").fetchone()[0],4)

if __name__=="__main__":
    unittest.main(verbosity=2)
