"""Fresh deployment only. No existing file is overwritten and no secret is printed."""
from pathlib import Path
import argparse,getpass,json,os,re,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1]
def acl(device):
 b='incubator/'+device;n='incubator_'+device
 ha=['read homeassistant/#','readwrite homeassistant/status',f'read {b}/#',f'write {b}/command/#']
 dev=['read homeassistant/status',f'read {b}/command/#',f'write homeassistant/+/{n}/+/config']
 dev += [f'write {b}/{t}' for t in ['availability','state','config','telemetry','events','progress','recovery/request','analysis/command','analysis/device_reply','samples']]
 dev += [f'read {b}/{t}' for t in ['recovery/reply','analysis/#','samples/ack']]
 logger=[f'read {b}/{t}' for t in ['state','config','telemetry','events','availability','progress','recovery/request','analysis/command','analysis/device_reply','samples']]
 logger += [f'write {b}/{t}' for t in ['logger/availability','analysis/#','recovery/reply','samples/ack']]+[f'write homeassistant/+/{n}/+/config']
 return '\n\n'.join('user '+u+'\n'+'\n'.join('topic '+t for t in rules) for u,rules in [('incubator_ha',ha),('incubator_device',dev),('incubator_logger',logger)])+'\n'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--device',required=True);p.add_argument('--uid',type=int,default=int(os.getenv('SUDO_UID',os.getuid() if hasattr(os,'getuid') else 1000)));p.add_argument('--gid',type=int,default=int(os.getenv('SUDO_GID',os.getgid() if hasattr(os,'getgid') else 1000)));p.add_argument('--image',default='eclipse-mosquitto:2.1.2-alpine');a=p.parse_args()
 if not re.fullmatch('[0-9a-f]{6}',a.device):p.error('Use your own six-hex-digit device info ID')
 if not 0<a.uid<2**31 or not 0<a.gid<2**31:p.error('Choose non-root container uid/gid, usually 1000')
 if not shutil.which('docker'):p.error('Docker CLI must be installed and usable by this account')
 if (ROOT/'.env').exists() or (ROOT/'runtime').exists():p.error('Existing .env/runtime found; use the documented upgrade route')
 subprocess.run(['docker','info'],check=True,stdout=subprocess.DEVNULL)
 if subprocess.run(['docker','image','inspect',a.image],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:subprocess.run(['docker','pull',a.image],check=True)
 secrets={}
 for role in ['incubator_device','incubator_ha','incubator_logger']:
  secret=getpass.getpass(role+' password (8+ characters): ')
  if len(secret)<8 or any(c in secret for c in '\r\n\0') or secret!=getpass.getpass('Repeat: '):p.error('Password length, line format or confirmation mismatch')
  secrets[role]=secret
 runtime=ROOT/'runtime';runtime.mkdir(mode=0o700)
 for name in ['mqtt','mqtt-data','records','secrets']:(runtime/name).mkdir(mode=0o700)
 def write(path,text):path.write_text(text,encoding='utf8',newline='\n');path.chmod(0o600)
 write(runtime/'mqtt/acl',acl(a.device))
 write(runtime/'mqtt/mosquitto.conf','listener 1883\nallow_anonymous false\npassword_file /mosquitto/config/passwords\nacl_file /mosquitto/config/acl\npersistence true\npersistence_location /mosquitto/data/\nlog_dest stdout\n')
 write(runtime/'secrets/logger_password.txt',secrets['incubator_logger']+'\n')
 write(runtime/'secrets/smtp_password.txt','')
 write(runtime/'secrets/smtp.json',json.dumps({'enabled':False,'host':'smtp.qq.com','port':465,'security':'ssl','username':'sender@example.invalid','from':'sender@example.invalid','to':'recipient@example.invalid','password_file':'/run/secrets/smtp_password','cooldown_seconds':600},indent=2)+'\n')
 pending=runtime/'mqtt/passwords.pending';write(pending,''.join(u+':'+s+'\n' for u,s in secrets.items()))
 def ownership():
  if hasattr(os,'chown'):
   for item in [runtime,*runtime.rglob('*')]:os.chown(item,a.uid,a.gid)
 ownership()
 try:
  # Official converter reads a private local file; no password is put in argv/history.
  subprocess.run(['docker','run','--rm','--user',f'{a.uid}:{a.gid}','--mount',f'type=bind,source={runtime / "mqtt"},target=/work','--entrypoint','mosquitto_passwd',a.image,'-H','sha512-pbkdf2','-U','/work/passwords.pending'],check=True)
  if any(not line.split(':',1)[1].startswith('$') for line in pending.read_text().splitlines()):raise RuntimeError('Password conversion did not produce hashes')
  pending.rename(runtime/'mqtt/passwords')
 finally:
  if pending.exists():pending.unlink()
 write(ROOT/'.env',f'DEVICE_ID={a.device}\nAPP_UID={a.uid}\nAPP_GID={a.gid}\nMOSQUITTO_IMAGE={a.image}\n')
 ownership()
 if hasattr(os,'chown'):os.chown(ROOT/'.env',a.uid,a.gid)
 print('Prepared. Keep runtime/ and .env private. Run: docker compose config --quiet; then docker compose up -d --build')
if __name__=='__main__':main()
