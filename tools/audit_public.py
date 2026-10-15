"""Public-tree hygiene and asset checks. Does not inspect or print secret contents."""
from pathlib import Path
import gzip,re,sys,xml.etree.ElementTree as ET
from audit_archives import audit as audit_archives
ROOT=Path(__file__).resolve().parents[1]
SKIP={'.git','runtime','build','dist','__pycache__','.pytest_cache'}
def files():
 for p in ROOT.rglob('*'):
  if p.is_file() and not any(part in SKIP for part in p.relative_to(ROOT).parts):yield p

def main():
 errors=audit_archives(ROOT)
 for p in files():
  relative=p.relative_to(ROOT).as_posix()
  if (p.suffix.lower() in {'.bin','.elf','.map','.db','.sqlite3','.log'} or (p.suffix.lower()=='.zip' and not relative.startswith('archive/'))) or p.name in {'.env','smtp.json'} or p.name.endswith('_password.txt'):errors.append(relative+': private/build artifact')
  if p.suffix=='.svg':ET.parse(p)
  if relative.startswith(('third_party/','services/recorder/vendor/')):continue
  if p.suffix.lower() not in {'.py','.cpp','.h','.ino','.html','.md','.json','.yaml','.ps1','.txt'}:continue
  text=p.read_text(encoding='utf-8-sig')
  # Real tokens/keys and machine-specific absolute paths must never enter the source export.
  patterns=[r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',r'gh[pousr]_[A-Za-z0-9]{30,}',r'github_pat_[A-Za-z0-9_]{30,}',r'AKIA[0-9A-Z]{16}',r'[A-Za-z]:[\\/]+Users[\\/]+[^\\/\s]+',r'/vol[0-9]+/[0-9]+/']
  if p.name=='audit_public.py':patterns=patterns[:4] # regex examples are not actual paths
  for pattern in patterns:
   if re.search(pattern,text):errors.append(relative+': token/private path pattern')
  if p.suffix=='.md':
   for target in re.findall(r'!?\[[^\]]*\]\(([^)]+)\)',text):
    if target.startswith(('http:','https:','mailto:','#')):continue
    target=target.split('#')[0]
    if target and not (p.parent/target).exists():errors.append(relative+': broken relative link '+target)
 fw=ROOT/'firmware/IncubatorController';html=re.search(r'R"HTML\((.*?)\)HTML"',(fw/'web_ui.h').read_text(encoding='utf8'),re.S)[1].encode()
 compressed=bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',(fw/'web_ui_gzip.h').read_text()))
 if gzip.decompress(compressed)!=html:errors.append('web_ui_gzip.h: regenerated source differs')
 for p in (ROOT/'wiki').glob('*-zh-CN.md'):
  if not p.with_name(p.name.replace('-zh-CN','')).exists():errors.append(p.name+': missing English counterpart')
 if errors:
  print('\n'.join(sorted(set(errors))));return 1
 print('PASS public-tree hygiene, bilingual links, SVG XML and gzip parity');return 0
if __name__=='__main__':sys.exit(main())
