"""Create a reviewed side-by-side Compose upgrade. Existing secrets/data/images remain untouched."""
import argparse,json,re
from pathlib import Path
MODULES=['recorder.py','record_store.py','optical_store.py','nas_analysis.py','analysis_transport.py','device_gateway.py','dashboard.py','dashboard.html','device.html','notifier.py','sample_journal.py','export.py','Send-IncubatorTestMail.py','vendor']
def target(v):
 if isinstance(v,dict):return v.get('target')
 m=re.search(r':(/[^:]+)(?::(?:ro|rw))?$',v)
 return m[1] if m else None

def upgrade(data,source):
 services=data.get('services',{})
 if 'recorder' not in services:raise ValueError('Expected service key recorder; merge manually for a renamed service')
 for name in ['recorder','mail']:
  if name not in services:continue
  service=services[name];wanted={'/app/'+n for n in MODULES}
  service['volumes']=[v for v in service.get('volumes',[]) if target(v) not in wanted]
  service['volumes'] += [{'type':'bind','source':str(source/n),'target':'/app/'+n,'read_only':True} for n in MODULES]
 return data

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--file',type=Path,required=True);p.add_argument('--source',type=Path,required=True);a=p.parse_args();source=a.source.resolve()
 for n in MODULES:
  if not (source/n).exists():p.error('Missing source module '+n)
 try:data=json.loads(a.file.read_text(encoding='utf-8-sig'))
 except json.JSONDecodeError:p.error('Traditional YAML detected. Original is unchanged; merge module mounts and ACL manually')
 out=a.file.with_name(a.file.stem+'.2.2.3.yaml')
 if out.exists():p.error('Output already exists; inspect it rather than overwrite')
 data=upgrade(data,source);out.write_text(json.dumps(data,indent=2)+'\n',encoding='utf8',newline='\n')
 print('Created '+str(out));print('Original unchanged. Validate with docker compose -f '+str(out)+' config --quiet before use.')
if __name__=='__main__':main()
