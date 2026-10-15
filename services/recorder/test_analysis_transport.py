import json,tempfile,unittest
from pathlib import Path
from record_store import Store
from test_nas_analysis import state
from test_record_store import NOW,OLD
import analysis_transport as t
class Client:
 def __init__(self):self.sent=[]
 def publish(self,topic,payload,qos=0,retain=False):self.sent.append((topic,payload,qos,retain))
class Tests(unittest.TestCase):
 def test_heartbeat_echoes_recent_board_sample(self):
  with tempfile.TemporaryDirectory() as d:
   s=Store(Path(d)/'a.db');s.state(state(1000),NOW+1000);c=Client()
   with s.db:t.tick(c,s.db,'a1b2c3',NOW+2000,True)
   m=[m for m in c.sent if m[0].endswith('/heartbeat')];self.assertEqual(len(m),1)
   self.assertEqual(json.loads(m[0][1]),{'v':1,'boot':OLD,'uptime_ms':1000,'received_ms':NOW+1000});self.assertFalse(m[0][3])
   with s.db:t.tick(c,s.db,'a1b2c3',NOW+62000,True)
   self.assertEqual(len([m for m in c.sent if m[0].endswith('/heartbeat')]),1);s.close()
 def test_discovery_preserves_twelve_derived_ids(self):
  c=Client();t.discovery(c,'a1b2c3');self.assertEqual(len(c.sent),12)
  for topic,payload,qos,retained in c.sent:
   p=json.loads(payload);self.assertTrue(p['unique_id'].startswith('incubator_a1b2c3_photo'));self.assertEqual(qos,1);self.assertTrue(retained);self.assertEqual(p['availability_mode'],'all')
 def test_flat_analysis_wire_fits_board_buffer(self):
  with tempfile.TemporaryDirectory() as d:
   s=Store(Path(d)/'a.db');s.state(state(1000),NOW+1000);c=Client();t.results(c,s.db,'a1b2c3',NOW+1001)
   messages=[m for m in c.sent if m[0].endswith(('photo1','photo2'))];self.assertEqual(len(messages),2)
   for _,payload,_,retain in messages:self.assertLess(len(payload.encode()),512);self.assertFalse(retain);self.assertEqual(json.loads(payload)['boot'],OLD)
   s.close()
