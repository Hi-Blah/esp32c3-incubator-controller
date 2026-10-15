import json,time,tempfile,unittest,base64,urllib.request,urllib.error
from pathlib import Path
from unittest.mock import patch
from types import SimpleNamespace
from urllib.parse import urlencode
from record_store import Store
from test_nas_analysis import state
import dashboard,device_gateway

class Client:
 def __init__(self):self.sent=[];self.gateway=None;self.answer=True
 def publish(self,topic,payload,qos,retain):
  d=json.loads(payload);self.sent.append((topic,d,qos,retain))
  if self.answer:self.gateway.reply({'v':1,'req':d['req'],'ok':True,'message':'OK MANUAL'})
  return SimpleNamespace(rc=0)

class Tests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.path=str(Path(self.tmp.name)/'a.db');self.store=Store(self.path);self.now=time.time_ns()//1000000
  self.data=state(100000);self.data['photo_epoch_ms']=self.now;self.data.update(config_revision=3,firmware='Connected-2.2.2',uptime_ms=100000,mode1=0,mode2=2,enabled1=True,enabled2=True)
  self.data['ui']={'heap':28000,'min':23000,'largest':18000,'ssid':'301','ip':'192.168.124.4','logic':True,'nvs':True,'dailyWaiting':[False,False]}
  self.store.state(self.data,self.now)
  cfg={'cfg':3,'channels':[{'mode':0,'repeats':0,'daily':[[25200,28800]],'steps':[[120000,120000]],'pwm':False,'frequency':50,'duty':50,'recovery':{'manualEnabled':True,'dailyEnabled':True}}, {'mode':2,'repeats':3,'daily':[[32400,46800]],'steps':[[500,300],[2000,1000]],'pwm':True,'frequency':2000,'duty':30,'recovery':{'enabled':True,'nasEnabled':True}}]}
  with self.store.db:self.store.db.execute('INSERT INTO device_configs VALUES(?,?,?,?)',('a1b2c3',3,json.dumps(cfg),self.now))
  self.client=Client();self.g=device_gateway.Gateway(self.client,'a1b2c3');self.client.gateway=self.g
  with patch.dict('os.environ',{'DASHBOARD_PORT':'0'}):self.server=dashboard.start(self.path,'a1b2c3','test-only',self.g)
  self.url='http://127.0.0.1:'+str(self.server.server_address[1]);self.auth='Basic '+base64.b64encode(b'incubator:test-only').decode()
 def tearDown(self):self.server.shutdown();self.server.server_close();self.store.close();self.tmp.cleanup()
 def get(self,path,auth=True):
  with urllib.request.urlopen(urllib.request.Request(self.url+path,headers={'Authorization':self.auth} if auth else {})) as r:return json.loads(r.read())
 def post(self,csrf,cmd,origin=None):
  h={'Authorization':self.auth,'X-CSRF-Token':csrf,'Content-Type':'application/x-www-form-urlencoded'}
  if origin:h['Origin']=origin
  with urllib.request.urlopen(urllib.request.Request(self.url+'/device/api/command',data=urlencode({'cmd':cmd}).encode(),headers=h)) as r:return json.loads(r.read())
 def test_mqtt_state_reused_without_device_http(self):
  d=self.get('/device/api/state');self.assertEqual(d['wifi']['ssid'],'301');self.assertEqual(d['diagnostics']['heapFree'],28000);self.assertEqual(d['lights'][1]['steps'],[[500,300],[2000,1000]]);self.assertTrue(d['lights'][0]['recovery']['manualEnabled']);self.assertEqual(self.client.sent,[])
 def test_public_ready_does_not_expose_state_or_control(self):
  self.assertTrue(self.get('/assets/ready',False)['device_ui'])
  with self.assertRaises(urllib.error.HTTPError) as e:self.get('/device/api/state',False)
  self.assertEqual(e.exception.code,401)
 def test_command_confirmed_with_unique_boot_expiry_envelope(self):
  d=self.get('/device/api/state');self.assertTrue(self.post(d['csrf'],'on 1')['ok']);topic,msg,qos,retain=self.client.sent[0];self.assertEqual(topic,'incubator/a1b2c3/analysis/device_command');self.assertEqual(msg['boot'],self.data['boot_id']);self.assertEqual(len(msg['req']),24);self.assertLessEqual(msg['until_ms'],self.data['uptime_ms']+10000);self.assertEqual(qos,1);self.assertFalse(retain)
 def test_no_csrf_or_wrong_origin_does_not_publish(self):
  for csrf,origin in [('wrong',None),(self.get('/device/api/state')['csrf'],'http://wrong-host')]:
   with self.assertRaises(urllib.error.HTTPError) as e:self.post(csrf,'arm',origin)
   self.assertEqual(e.exception.code,403)
  self.assertEqual(self.client.sent,[])
 def test_network_passwords_and_stale_control_not_relayed(self):
  for command in ['wifi key','mqtt on','on 1\narm']:
   with self.assertRaises(ValueError):self.g.command(command,self.data,self.now)
  with self.assertRaises(ValueError):self.g.command('on 1',self.data,self.now-16000)
  self.assertEqual(self.client.sent,[])
 def test_timeout_reports_unknown_does_not_auto_resend(self):
  self.client.answer=False
  with patch.object(device_gateway.threading.Event,'wait',return_value=False):out=self.g.command('on 1',self.data,self.now)
  self.assertFalse(out['ok']);self.assertIn('query current state',out['message']);self.assertEqual(len(self.client.sent),1);self.assertEqual(self.g.pending,{})
 def test_retained_reply_rejected(self):
  p={'event':device_gateway.threading.Event(),'result':None};self.g.pending['a']=p;self.g.reply({'v':1,'req':'a','ok':True},True);self.assertFalse(p['event'].is_set())
 def test_config_revision_mismatch_blocks_ui(self):
  with self.store.db:self.store.db.execute('DELETE FROM device_configs')
  with self.assertRaises(urllib.error.HTTPError):self.get('/device/api/state')

 def test_missing_configuration_reports_safe_specific_reason(self):
  with self.store.db:self.store.db.execute('DELETE FROM device_configs')
  with self.assertRaises(urllib.error.HTTPError) as e:self.get('/device/api/state')
  body=json.loads(e.exception.read());self.assertFalse(body['ok']);self.assertEqual(body['message'],'Waiting for current device configuration');self.assertNotIn('csrf',body)
 def test_stale_state_is_not_relaxed_by_error_reporting(self):
  with self.store.db:self.store.db.execute('UPDATE nas_live SET last_live_ms=?',(self.now-20000,))
  with self.assertRaises(urllib.error.HTTPError) as e:self.get('/device/api/state')
  body=json.loads(e.exception.read());self.assertIn('temporarily unavailable',body['message']);self.assertEqual(self.client.sent,[])

if __name__=='__main__':unittest.main(verbosity=2)
