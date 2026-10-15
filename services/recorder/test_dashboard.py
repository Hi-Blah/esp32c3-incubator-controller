import base64,json,os,tempfile,unittest,urllib.request,urllib.error
from pathlib import Path
from unittest.mock import patch
from record_store import Store
import dashboard,notifier,optical_store
from test_nas_analysis import state
from test_record_store import NOW
class Tests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.path=str(Path(self.tmp.name)/'a.db');self.store=Store(self.path);self.store.state(state(),NOW+1000)
  with patch.dict(os.environ,{'DASHBOARD_PORT':'0'}):self.server=dashboard.start(self.path,'a1b2c3','test-only')
  self.url='http://127.0.0.1:'+str(self.server.server_address[1]);self.auth='Basic '+base64.b64encode(b'incubator:test-only').decode()
 def tearDown(self):self.server.shutdown();self.server.server_close();self.store.close();self.tmp.cleanup()
 def call(self,path,auth=True,data=None,origin=None):
  h={'Authorization':self.auth} if auth else {};body=None
  if data is not None:body=json.dumps(data).encode();h['Content-Type']='application/json'
  if origin:h['Origin']=origin
  with urllib.request.urlopen(urllib.request.Request(self.url+path,data=body,headers=h)) as r:return r.status,r.read()
 def test_requires_authentication(self):
  with self.assertRaises(urllib.error.HTTPError) as e:self.call('/api/state',False)
  self.assertEqual(e.exception.code,401)
 def test_history_and_current_raw(self):
  status,body=self.call('/api/state');d=json.loads(body);self.assertEqual(d['state']['photo']['channels'][0]['raw'],2000);self.assertEqual(len(d['channels']),2)
  self.assertEqual(self.call('/api/history?seconds=3600')[0],200)
 def test_dashboard_is_utf8_page(self):self.assertIn('NAS分析'.encode(),self.call('/')[1])
 def test_mail_status_authentication_and_no_secret_fields(self):
  with self.assertRaises(urllib.error.HTTPError) as e:self.call('/api/mail-status',False)
  self.assertEqual(e.exception.code,401)
  with self.store.db:optical_store.enqueue(self.store.db,'test','photo_MISSING_LIGHT',1,NOW,'alert','private body')
  d=json.loads(self.call('/api/mail-status')[1]);self.assertEqual(d['status'],'unreported');self.assertEqual(d['counts']['pending'],1)
  self.assertNotIn('private body',json.dumps(d));self.assertNotIn('body',d['recent'][0])
  notifier.heartbeat(self.store.db,'running',__import__('time').time_ns()//1000000)
  self.assertEqual(json.loads(self.call('/api/mail-status')[1])['status'],'running')
 def test_origin_mismatch_rejected(self):
  with self.assertRaises(urllib.error.HTTPError) as e:self.call('/api/config',data={'channel':1},origin='http://wrong-host')
  self.assertEqual(e.exception.code,403)
 def test_uncalibrated_cannot_enable_and_valid_update_preserves_history(self):
  with self.assertRaises(urllib.error.HTTPError):self.call('/api/config',data={'channel':1,'enabled':True})
  self.call('/api/config',data={'channel':1,'enabled':False,'hold_ms':5000});self.assertEqual(self.store.db.execute('SELECT hold_ms FROM optical_settings WHERE channel=1').fetchone()[0],5000);self.assertEqual(self.store.db.execute('SELECT COUNT(*) FROM sensor_samples').fetchone()[0],1)
if __name__=='__main__':unittest.main(verbosity=2)
