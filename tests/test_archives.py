"""Publication checks for independently downloadable historical source packages."""
import importlib.util,json,sys,tempfile,unittest,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('audit_archives',ROOT/'tools/audit_archives.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)

class ArchiveTests(unittest.TestCase):
 def test_catalogued_archives_are_complete_and_consistent(self):
  self.assertEqual([],mod.audit(ROOT))
 def test_all_versions_have_two_reader_guides(self):
  c=json.loads((ROOT/'archive/catalog.json').read_text(encoding='utf8'))
  for e in c['entries']:
   with zipfile.ZipFile(ROOT/'archive'/e['path']) as z:
    for name in ('README.md','README.zh-CN.md'):
     t=z.read(name).decode();self.assertGreater(len(t),700)
    if e['kind']=='nas':
     self.assertIn('recorder/compose.archive.yaml',z.namelist())
     compose=json.loads(z.read('recorder/compose.archive.yaml'))
     self.assertIn('MQTT_HOST',compose['services']['recorder']['environment'])
     if 'recorder/notifier.py' in z.namelist():self.assertIn('mail',compose['services'])
 def test_unsafe_members_are_rejected(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'bad.zip'
   with zipfile.ZipFile(p,'w') as z:z.writestr('../outside.py','print(1)');z.writestr('nvs.bin',b'\0\0')
   errors=mod.inspect_zip(p)
   self.assertTrue(any('unsafe' in x for x in errors));self.assertTrue(any('runtime/build' in x for x in errors))

if __name__=='__main__':unittest.main()
