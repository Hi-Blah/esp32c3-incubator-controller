import copy,json,tempfile,unittest
from pathlib import Path
from record_store import Store
import nas_analysis as a
from test_record_store import NOW,OLD

def state(t=1000,mode=1,output=False,raw=2000):
 return {'boot_id':OLD,'firmware':'Connected-2.2.0','uptime_ms':t,'sampled_uptime_ms':t,'sample_epoch_ms':NOW+t,'sample_time_trusted':True,'sensor_valid':True,'temperature':25,'humidity':50,'armed':True,'l1':output,'l2':False,'enabled1':True,'enabled2':True,'mode1':mode,'mode2':0,'pwm1':False,'pwm2':False,'duty1':50,'duty2':50,'frequency1':1000,'frequency2':1000,'photo_epoch_ms':NOW+t,'photo_time_trusted':True,'photo':{'schema':'raw-v2','compiled':True,'sampled_ms':t,'automatic':{'active':False},'channels':[{'raw':raw,'valid':True},{'raw':100,'valid':True}]}}
class Tests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.s=Store(Path(self.tmp.name)/'a.db')
  with self.s.db:
   a.settings(self.s.db,'a1b2c3',1);self.s.db.execute('UPDATE optical_settings SET dark=100,bright=2100,enabled=1 WHERE channel=1')
 def tearDown(self):self.s.close();self.tmp.cleanup()
 def notice(self,category='photo'):return self.s.db.execute('SELECT COUNT(*) FROM notifications WHERE category LIKE ?',(category+'%',)).fetchone()[0]
 def send(self,d,retained=False):self.s.state(d,NOW+d['uptime_ms'],retained)
 def cache(self):return json.loads(self.s.db.execute('SELECT payload FROM nas_analysis_cache WHERE channel=1').fetchone()[0])
 def test_manual_off_light_does_not_alert(self):
  for t in (1000,5000,9000):self.send(state(t,mode=0))
  self.assertEqual(self.notice(),0);self.assertEqual(self.cache()['status'],10)
 def test_manual_on_dark_requires_hold_then_alerts_once(self):
  for t in (1000,3000):self.send(state(t,mode=0,output=True,raw=100))
  self.assertEqual(self.notice(),0)
  for t in (5000,6000,9000):self.send(state(t,mode=0,output=True,raw=100))
  self.assertEqual(self.cache()['status'],3);self.assertEqual(self.cache()['fault'],1)
  self.assertEqual(self.s.db.execute('SELECT category,channel,status FROM notifications').fetchall(),[('photo_MISSING_LIGHT',1,'pending')])
 def test_manual_dark_recovery_resets_and_can_alert_again(self):
  self.send(state(1000,mode=0,output=True,raw=100));self.send(state(5000,mode=0,output=True,raw=100))
  self.send(state(6000,mode=0,output=True,raw=2100));self.assertEqual(self.cache()['fault'],0)
  self.send(state(7000,mode=0,output=True,raw=100));self.send(state(11000,mode=0,output=True,raw=100));self.assertEqual(self.notice(),2)
 def test_manual_other_guards_do_not_alert(self):
  cases=({'armed':False},{'enabled1':False},{'pwm1':True,'duty1':1})
  for delta in cases:
   for t in (1000,5000):
    d=state(t,mode=0,output=True,raw=100);d.update(delta);self.send(d)
   self.assertEqual(self.notice(),0)
   self.s.db.execute('DELETE FROM nas_analysis_cache');self.s.db.execute('DELETE FROM nas_candidate')
 def test_manual_second_channel_and_pwm_missing_light(self):
  with self.s.db:
   a.settings(self.s.db,'a1b2c3',2);self.s.db.execute('UPDATE optical_settings SET dark=100,bright=2100,enabled=1 WHERE channel=2')
  for t in (1000,5000):
   d=state(t,mode=0,output=False,raw=100);d['l2']=True;d['pwm2']=True;d['duty2']=50;self.send(d)
  self.assertEqual(self.s.db.execute('SELECT category,channel FROM notifications').fetchall(),[('photo_MISSING_LIGHT',2)])
 def test_manual_dim_does_not_add_unrequested_alarm(self):
  for t in (1000,5000):self.send(state(t,mode=0,output=True,raw=900))
  self.assertEqual(self.cache()['status'],2);self.assertEqual(self.notice(),0)
 def test_manual_uncalibrated_disabled_invalid_or_calibration_do_not_alert(self):
  for setting in ('uncalibrated','disabled','invalid','automatic'):
   with self.subTest(setting=setting):
    self.s.db.execute('DELETE FROM nas_analysis_cache');self.s.db.execute('DELETE FROM nas_candidate')
    self.s.db.execute('UPDATE optical_settings SET dark=100,bright=2100,enabled=1 WHERE channel=1')
    if setting=='uncalibrated':self.s.db.execute('UPDATE optical_settings SET bright=NULL WHERE channel=1')
    if setting=='disabled':self.s.db.execute('UPDATE optical_settings SET enabled=0 WHERE channel=1')
    for t in (1000,5000):
     d=state(t,mode=0,output=True,raw=100)
     if setting=='invalid':d['photo']['channels'][0]['valid']=False
     if setting=='automatic':d['photo']['automatic']['active']=True
     self.send(d)
    self.assertEqual(self.notice(),0)
 def test_daily_off_light_alerts_after_hold(self):
  self.send(state(1000));self.assertEqual(self.notice(),0);self.send(state(5000));self.assertEqual(self.notice(),1);self.assertEqual(self.cache()['status'],5);self.assertEqual(self.cache()['fault'],1)
 def test_cycle_off_light_alerts(self):
  self.send(state(1000,mode=2));self.send(state(5000,mode=2));self.assertEqual(self.notice(),1)
 def test_stopped_channel_does_not_alert(self):
  for t in (1000,5000):
   d=state(t);d['enabled1']=False;self.send(d)
  self.assertEqual(self.notice(),0)
 def test_manual_mode_clears_previous_auto_fault(self):
  self.send(state(1000));self.send(state(5000));self.send(state(6000,mode=0));self.assertEqual(self.cache()['fault'],0);self.assertEqual(self.notice(),1)
 def test_on_missing_light_is_nas_decision(self):
  self.send(state(1000,output=True,raw=100));self.send(state(5000,output=True,raw=100));self.assertEqual(self.cache()['status'],3)
 def test_automatic_calibration_suppresses_alert(self):
  for t in (1000,5000):
   d=state(t);d['photo']['automatic']['active']=True;self.send(d)
  self.assertEqual(self.notice(),0);self.assertEqual(self.cache()['status'],7)
 def test_retained_and_delayed_never_trigger_current_alarm(self):
  self.send(state(),True);self.assertEqual(self.notice(),0)
  self.s.state(state(),NOW+60000);self.assertEqual(self.notice(),0)
 def test_duplicates_do_not_clear_fault_or_flood(self):
  self.send(state(1000));self.send(state(5000))
  for _ in range(10):self.send(state(5000))
  self.assertEqual(self.notice(),1);self.assertEqual(self.cache()['fault'],1)
 def test_two_point_positive_inverse_and_span_validation(self):
  self.assertEqual(a.normalize(1100,100,2100),500);self.assertEqual(a.normalize(1100,2100,100),500);self.assertIsNone(a.normalize(20,10,30))
 def test_low_duty_has_no_false_missing_alert(self):
  for t in (1000,5000):
   d=state(t,output=True,raw=100);d['pwm1']=True;d['duty1']=1;self.send(d)
  self.assertEqual(self.notice(),0);self.assertEqual(self.cache()['status'],8)
 def test_durable_raw_samples_and_retained_no_new_samples(self):
  self.send(state());self.send(state());self.assertEqual(self.s.db.execute('SELECT COUNT(*) FROM light_samples').fetchone()[0],2);self.s.close();self.s=Store(Path(self.tmp.name)/'a.db');self.assertEqual(self.s.db.execute('SELECT COUNT(*) FROM light_samples').fetchone()[0],2)
 def test_watchdog_notice_recovery_and_no_power_claim(self):
  self.send(state());with_db=self.s.db
  with with_db:a.watchdog(with_db,'a1b2c3',NOW+181001)
  self.assertEqual(self.notice('notice'),1);self.send(state(182000));self.assertEqual(self.notice('notice'),2)
 def test_no_photo_module_still_handshakes(self):
  d=state(100000);d['photo']['compiled']=False;d['photo']['sampled_ms']=0;self.send(d);self.assertIsNotNone(self.s.db.execute('SELECT boot FROM nas_live').fetchone())
 def test_nas_command_requires_fresh_boot_and_guarded_capture(self):
  self.send(state(raw=100));cmd={'v':1,'boot':OLD,'ch':1,'op':'dark','raw':100};
  with self.s.db:a.command(self.s.db,'a1b2c3',cmd,NOW+1001)
  cmd['op']='bright'
  with self.assertRaises(ValueError):a.command(self.s.db,'a1b2c3',cmd,NOW+1002)
  cmd['boot']='other'
  with self.assertRaises(ValueError):a.command(self.s.db,'a1b2c3',cmd,NOW+1003)
 def test_partial_grid_is_not_a_model_complete_grid_fits(self):
  points=[(0,0,100),(0,100,2100)]+[(f,d,100+d*20) for f in a.FREQUENCIES for d in a.DUTIES[1:-1]]
  for i,(hz,duty,raw) in enumerate(points):
   event={'event_id':OLD+'-'+str(i+1),'boot_id':OLD,'kind':'calibration_point','run':'a1b2c3-c12345678','channel':1,'frequency':hz,'duty':duty,'raw':raw,'cross_raw':100,'raw_sum':raw*20,'cross_sum':2000,'sample_count':20,'epoch_ms':NOW+i,'time_trusted':True}
   self.s.event('events',event,NOW+i)
   if i<85:self.assertIsNone(a.model_for(self.s.db,'a1b2c3',1))
  model=a.model_for(self.s.db,'a1b2c3',1);self.assertIsNotNone(model);self.assertEqual(a.interpolate(model,1800,30),300);self.assertEqual(self.s.db.execute('SELECT COUNT(*) FROM raw_calibration').fetchone()[0],86)
 def test_unknown_second_channel_rolls_back_transaction(self):
  d=state();d['photo']['channels'][1]['raw']=5000
  with self.assertRaises(ValueError):self.send(d)
  self.assertEqual(self.s.db.execute('SELECT COUNT(*) FROM light_samples').fetchone()[0],0)
 def test_existing_optical_baselines_import_without_deleting_history(self):
  self.s.db.execute('DELETE FROM optical_settings WHERE channel=1')
  legacy={'dark':200,'bright':2800,'onThreshold':25,'offThreshold':85,'dimRatio':700,'enabled':True}
  self.s.db.execute('INSERT INTO light_samples VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',(OLD,900,1,NOW,NOW,1500,500,1000,500,1,1,1,1,0,'OK',1,json.dumps(legacy)))
  cfg=a.settings(self.s.db,'a1b2c3',1);self.assertEqual((cfg['dark'],cfg['bright'],cfg['on_min'],cfg['off_max'],cfg['dim_ratio'],cfg['enabled']),(200,2800,25,85,700,1));self.assertEqual(self.s.db.execute('SELECT COUNT(*) FROM light_samples').fetchone()[0],1)
 def test_old_mean_point_does_not_claim_original_sample_count(self):
  event={'event_id':OLD+'-1','boot_id':OLD,'kind':'calibration_point','run':'a1b2c3-c12345678','channel':1,'frequency':0,'duty':0,'raw':100,'cross_raw':100,'epoch_ms':NOW,'time_trusted':True}
  self.s.event('events',event,NOW);self.assertIsNone(self.s.db.execute('SELECT sample_count FROM raw_calibration').fetchone()[0])
 def test_retired_boot_cannot_replace_new_live_state(self):
  self.send(state());d=state(2000);d['boot_id']='a1b2c3-15-b5b18c8a';self.send(d);self.send(state(3000));self.assertEqual(self.s.db.execute('SELECT boot FROM nas_live').fetchone()[0],d['boot_id'])

 def test_raw_capture_requires_valid_module(self):
  for compiled,valid in ((False,True),(True,False)):
   d=state(raw=100);d['photo']['compiled']=compiled;d['photo']['channels'][0]['valid']=valid;self.send(d)
   with self.assertRaisesRegex(ValueError,'valid ADC module'):
    a.command(self.s.db,'a1b2c3',{'v':1,'boot':OLD,'ch':1,'op':'dark','raw':100},NOW+1001)
 def test_raw_watchdog_does_not_duplicate_availability_notices(self):
  self.s.availability('incubator/a1b2c3/availability','online',NOW,True);self.send(state())
  self.s.availability('incubator/a1b2c3/availability','offline',NOW+5000)
  self.assertEqual(self.notice('notice'),0)
  with self.s.db:a.watchdog(self.s.db,'a1b2c3',NOW+182000)
  self.assertEqual(self.notice('notice'),1)
  self.s.availability('incubator/a1b2c3/availability','online',NOW+183000);self.send(state(184000))
  self.assertEqual(self.notice('notice'),2)

 def test_short_outage_updates_ui_without_email_or_recovery_email(self):
  self.send(state(1000))
  with self.s.db:self.assertFalse(a.watchdog(self.s.db,'a1b2c3',NOW+62000))
  self.assertEqual(self.s.db.execute('SELECT offline FROM nas_live').fetchone()[0],1);self.assertEqual(self.notice('notice'),0)
  self.send(state(120000));self.assertEqual(self.notice('notice'),0)
 def test_offline_email_strictly_after_three_minutes_once(self):
  self.send(state(1000))
  for t in (179000,180999,181000):
   with self.s.db:a.watchdog(self.s.db,'a1b2c3',NOW+t)
   self.assertEqual(self.notice('notice'),0)
  for t in (181001,200000,300000):
   with self.s.db:a.watchdog(self.s.db,'a1b2c3',NOW+t)
  self.assertEqual(self.notice('notice'),1);self.send(state(301000));self.assertEqual(self.notice('notice'),2)
