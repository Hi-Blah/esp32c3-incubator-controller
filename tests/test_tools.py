import json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import setup_runtime,upgrade_existing_compose,export_wiki
class DeploymentTests(unittest.TestCase):
 def test_acl_gives_commit_receipts_only_to_logger(self):
  text=setup_runtime.acl('a1b2c3');blocks={part.splitlines()[0]:part for part in text.split('user ')[1:]}
  self.assertIn('topic write incubator/a1b2c3/samples',blocks['incubator_device'])
  self.assertIn('topic read incubator/a1b2c3/samples/ack',blocks['incubator_device'])
  self.assertNotIn('topic write incubator/a1b2c3/samples/ack',blocks['incubator_device'])
  self.assertIn('topic write incubator/a1b2c3/samples/ack',blocks['incubator_logger'])
 def test_compose_upgrade_preserves_environment_secrets_and_network(self):
  original={'services':{'recorder':{'image':'sha256:local-image','user':'2000:2000','environment':{'EXISTING':'kept'},'networks':['existing'],'volumes':['/private/records:/records','/private/key:/run/secrets/logger_password:ro','/old/recorder.py:/app/recorder.py:ro']},'mail':{'image':'local-image','volumes':[{'source':'/private/smtp.json','target':'/app/smtp.json','type':'bind'}]}}}
  result=upgrade_existing_compose.upgrade(original,Path('/new-source'))
  r=result['services']['recorder'];self.assertEqual(r['image'],'sha256:local-image');self.assertEqual(r['user'],'2000:2000');self.assertEqual(r['environment'],{'EXISTING':'kept'});self.assertEqual(r['networks'],['existing']);self.assertIn('/private/key:/run/secrets/logger_password:ro',r['volumes'])
  self.assertEqual(sum(upgrade_existing_compose.target(v)=='/app/recorder.py' for v in r['volumes']),1)
  self.assertIn({'source':'/private/smtp.json','target':'/app/smtp.json','type':'bind'},result['services']['mail']['volumes'])
 def test_wiki_export_rewrites_only_local_targets(self):
  text='[HW](Hardware.md) ![wire](../assets/wiring-power.svg) [README](../README.md) [external](https://example.invalid/a)'
  out=export_wiki.rewrite(text,'example-owner/example-repo','main')
  self.assertIn('[HW](Hardware)',out);self.assertIn('https://raw.githubusercontent.com/example-owner/example-repo/main/assets/wiring-power.svg',out);self.assertIn('/blob/main/README.md',out);self.assertIn('https://example.invalid/a',out)
 def test_compose_uses_bounded_nonroot_services_and_new_runtime(self):
  data=json.loads((ROOT/'compose.yaml').read_text())
  for s in data['services'].values():self.assertNotEqual(s['user'],'0:0');self.assertIn('ALL',s['cap_drop']);self.assertTrue(s['read_only'])
  self.assertIn('mail',data['services']['mail']['profiles'])
if __name__=='__main__':unittest.main()
