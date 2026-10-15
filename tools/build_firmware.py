"""Compile without uploading. Windows builds stage source under a short work path."""
import argparse,json,subprocess,tempfile,shutil,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cli',default='arduino-cli');p.add_argument('--config',type=Path);p.add_argument('--work',type=Path);p.add_argument('--install',action='store_true');a=p.parse_args()
 d=json.loads((ROOT/'firmware/dependencies.json').read_text());cli=[a.cli]+(['--config-file',str(a.config)] if a.config else [])
 if a.install:
  url='https://espressif.github.io/arduino-esp32/package_esp32_index.json'
  subprocess.run([*cli,'core','update-index','--additional-urls',url],check=True)
  subprocess.run([*cli,'core','install','esp32:esp32@'+d['arduinoEsp32'],'--additional-urls',url],check=True)
  subprocess.run([*cli,'lib','install',*[x['name']+'@'+x['version'] for x in d['libraries']]],check=True)
 work=a.work.resolve() if a.work else Path(tempfile.mkdtemp(prefix='incubator-build-'))
 work.mkdir(parents=True,exist_ok=True);sketch=work/'source/IncubatorController';sketch.mkdir(parents=True,exist_ok=True)
 for f in (ROOT/'firmware/IncubatorController').iterdir():
  if f.suffix in ('.h','.cpp','.ino'):shutil.copy2(f,sketch/f.name)
 library=work/'libraries/IncubatorNimBLE';shutil.copytree(ROOT/d['nimble']['path'],library,dirs_exist_ok=True)
 command=[*cli,'compile','--fqbn',d['fqbn'],'--libraries',str(work/'libraries'),'--build-path',str(work/'objects'),'--output-dir',str(work/'output'),'--jobs','2','--build-property','compiler.cpp.extra_flags=-fstack-usage',str(sketch)]
 subprocess.run(command,check=True);print('Build output: '+str(work/'output'))
if __name__=='__main__':main()
