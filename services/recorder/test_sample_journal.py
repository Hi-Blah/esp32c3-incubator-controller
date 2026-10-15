import copy,csv,io,json,sqlite3,tempfile,unittest
from contextlib import closing
from pathlib import Path
from record_store import Store
import sample_journal as journal
NOW=1792000000000
DEVICE='a1b2c3'
def sample(order=1,seq=1,uptime=1000,trusted=False):
 return {'v':1,'boot_id':f'{DEVICE}-{order}-01234567','boot_order':order,'seq':str(seq),'uptime_ms':uptime,'epoch_ms':NOW+uptime if trusted else 0,'sensor_uptime_ms':max(0,uptime-500),'flags':7|(8 if trusted else 0),'cfg':1,'temperature':25.0,'humidity':50.0,'raw':[1200,2300],'frequency':[50,1000],'duty':[50,30],'mode':[0,2],'journal':{'pending':1,'capacity':2048,'covered':0,'failures':0}}
class JournalTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.path=str(Path(self.tmp.name)/'data.sqlite3');self.store=Store(self.path,DEVICE);self.db=self.store.db
 def tearDown(self):self.store.close();self.tmp.cleanup()
 def put(self,d):return journal.ingest(self.db,DEVICE,d,NOW+100000)
 def test_offline_unknown_then_ntp_repairs_same_boot(self):
  self.put(sample(uptime=1000));self.put(sample(seq=2,uptime=5000,trusted=True))
  rows=self.db.execute('SELECT epoch_ms,time_quality FROM offline_samples ORDER BY CAST(seq AS INTEGER)').fetchall()
  self.assertEqual(rows,[(NOW+1000,'backfilled-ntp'),(NOW+5000,'ntp')]);self.assertEqual(self.db.execute('SELECT COUNT(*) FROM sensor_samples WHERE trusted=1').fetchone()[0],2)
 def test_multiple_reboots_do_not_invent_power_off_durations(self):
  self.put(sample(order=1,uptime=5000));self.put(sample(order=2,uptime=1000));self.put(sample(order=3,uptime=2000,trusted=True))
  self.assertEqual(self.db.execute("SELECT COUNT(*) FROM offline_samples WHERE time_quality='unknown'").fetchone()[0],2)
  rows=list(csv.reader(io.StringIO(journal.csv_text(self.db,DEVICE))));self.assertEqual([r[1] for r in rows[1:]],['1','2','3']);self.assertEqual(rows[1][5],'')
 def test_later_boot_anchor_never_changes_earlier_boot(self):
  self.put(sample(order=1));self.put(sample(order=2,trusted=True))
  self.assertIsNone(self.db.execute('SELECT epoch_ms FROM offline_samples WHERE boot_order=1').fetchone()[0])
 def test_duplicate_after_lost_receipt_is_idempotent(self):
  d=sample(trusted=True);receipt=self.put(d);d['journal']['pending']=0;self.assertEqual(receipt,self.put(d));self.assertEqual(journal.status(self.db,DEVICE)['stored'],1)
 def test_receipt_only_after_commit_visible_to_other_connection(self):
  self.put(sample())
  with closing(sqlite3.connect(self.path)) as other:self.assertEqual(other.execute('SELECT COUNT(*) FROM offline_samples').fetchone()[0],1)
 def test_storage_failure_rolls_back_no_receipt(self):
  self.db.execute("CREATE TRIGGER fail_history BEFORE INSERT ON sensor_samples BEGIN SELECT RAISE(ABORT,'disk-full-simulation'); END")
  with self.assertRaises(sqlite3.Error):self.put(sample())
  self.assertEqual(self.db.execute('SELECT COUNT(*) FROM offline_samples').fetchone()[0],0)
 def test_conflicting_duplicate_rejected_without_overwrite(self):
  d=sample();self.put(d);d['raw'][0]+=1
  with self.assertRaises(ValueError):self.put(d)
  self.assertEqual(json.loads(self.db.execute('SELECT payload FROM offline_samples').fetchone()[0])['raw'][0],1200)
 def test_history_does_not_overwrite_live_or_send_alerts(self):
  self.put(sample(trusted=True))
  self.assertEqual(self.db.execute('SELECT COUNT(*) FROM nas_live').fetchone()[0],0)
  self.assertEqual(self.db.execute('SELECT COUNT(*) FROM notifications').fetchone()[0],0)
  self.assertEqual(self.db.execute("SELECT raw,status FROM light_samples WHERE channel=1").fetchone(),(1200,'HISTORICAL_RAW'))
 def test_invalid_sensor_and_photo_retained_as_invalid(self):
  d=sample();d['flags']=0;d['temperature']=d['humidity']=None;self.put(d)
  self.assertEqual(self.db.execute('SELECT valid,temperature FROM sensor_samples').fetchone(),(0,None));self.assertIsNone(self.db.execute('SELECT raw FROM light_samples LIMIT 1').fetchone()[0])
 def test_anchor_is_not_a_sensor_sample(self):
  d=sample(trusted=True);d['flags']|=512;self.put(d);self.assertEqual(self.db.execute('SELECT COUNT(*) FROM sensor_samples').fetchone()[0],0)
 def test_retained_is_not_a_fresh_import(self):
  with self.assertRaises(ValueError):journal.ingest(self.db,DEVICE,sample(),NOW,True)
 def test_invalid_identity_order_time_and_range(self):
  for key,value in [('boot_order',2),('seq','01'),('flags',2048),('sensor_uptime_ms',1001),('temperature',float('nan')),('epoch_ms',NOW),('raw',[5000,1])]:
   with self.subTest(key=key):
    d=sample();d[key]=value
    with self.assertRaises(ValueError):self.put(d)
 def test_csv_keeps_order_and_received_time_separate(self):
  self.put(sample(order=3,seq=1,uptime=9000));self.put(sample(order=2,seq=50,uptime=1000,trusted=True))
  rows=list(csv.reader(io.StringIO(journal.csv_text(self.db,DEVICE))));self.assertEqual([r[1] for r in rows[1:]],['2','3']);self.assertNotEqual(rows[1][5],rows[1][7])
 def test_nas_estimate_then_ntp_refines_same_boot(self):
  self.put(sample(uptime=1000));d=sample(seq=2,uptime=2000,trusted=True);d['flags']|=1024;d['epoch_ms']+=300;self.put(d)
  self.assertEqual(self.db.execute('SELECT time_quality FROM offline_samples ORDER BY CAST(seq AS INTEGER)').fetchall(),[('backfilled-nas',),('nas',)])
  self.put(sample(seq=3,uptime=5000,trusted=True))
  expected=[(NOW+1000,),(NOW+2000,),(NOW+5000,)]
  self.assertEqual(self.db.execute('SELECT epoch_ms FROM offline_samples ORDER BY CAST(seq AS INTEGER)').fetchall(),expected)
  self.assertEqual(self.db.execute('SELECT epoch_ms FROM sensor_samples ORDER BY sample_uptime').fetchall(),expected)
 def test_late_nas_replay_cannot_downgrade_ntp_anchor(self):
  self.put(sample(seq=2,uptime=5000,trusted=True));d=sample(seq=1,uptime=1000,trusted=True);d['flags']|=1024;d['epoch_ms']+=400;self.put(d)
  self.assertEqual(self.db.execute("SELECT epoch_ms,time_quality FROM offline_samples WHERE seq='1'").fetchone(),(NOW+1000,'backfilled-ntp'))

 def test_adaptive_phase_metadata_and_critical_flags_roundtrip(self):
  d=sample();d.update(outage_id='1',offline_elapsed_ms=900000,period_s=10);d['flags']|=2048|4096;d['journal']['thinned']=120
  self.put(d);saved=json.loads(self.db.execute('SELECT payload FROM offline_samples').fetchone()[0]);self.assertEqual(saved['period_s'],10);self.assertEqual(saved['offline_elapsed_ms'],900000)
  self.assertEqual(journal.status(self.db,DEVICE)['journal']['thinned'],120)
 def test_adaptive_metadata_invalid_does_not_commit(self):
  for delta in ({'outage_id':'1'},{'outage_id':'0','offline_elapsed_ms':900,'period_s':3},{'outage_id':'1','offline_elapsed_ms':900,'period_s':0}):
   d=sample();d.update(delta)
   with self.assertRaises(ValueError):self.put(d)
  self.assertEqual(journal.status(self.db,DEVICE)['stored'],0)

if __name__=="__main__":unittest.main()
