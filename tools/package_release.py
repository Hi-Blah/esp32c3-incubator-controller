"""Create reviewed source/firmware/NAS ZIPs outside the repository; never include runtime secrets."""
from pathlib import Path
import argparse,hashlib,subprocess,sys,zipfile
ROOT=Path(__file__).resolve().parents[1]
SKIP={'.git','__pycache__','.pytest_cache','runtime','build','dist'}
def entries(folder):
 for p in sorted(folder.rglob('*')):
  if p.is_file() and not any(x in SKIP for x in p.relative_to(folder).parts):yield p,p.relative_to(folder).as_posix()
def write_zip(target,files,extra=None):
 if target.exists():raise SystemExit('Refusing to overwrite '+str(target))
 with zipfile.ZipFile(target,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
  for name,data in [*((name,p.read_bytes()) for p,name in files),*((extra or {}).items())]:
   info=zipfile.ZipInfo(name,(2026,10,15,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16;z.writestr(info,data)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path);a=p.parse_args();out=a.output.resolve()
 if out==ROOT or ROOT in out.parents:p.error('Choose an output folder outside the repository')
 subprocess.run([sys.executable,str(ROOT/'tools/audit_public.py')],check=True);out.mkdir(parents=True,exist_ok=True)
 source=out/'ESP32C3-Incubator-2.2.3-Source.zip'
 write_zip(source,[(f,'esp32c3-incubator-controller/'+n) for f,n in entries(ROOT)])
 fw=out/'Connected-2.2.3-Source.zip'
 files=[(f,'IncubatorController/'+n) for f,n in entries(ROOT/'firmware/IncubatorController')]
 files += [(f,'libraries/IncubatorNimBLE/'+n) for f,n in entries(ROOT/'third_party/IncubatorNimBLE')]
 files += [(ROOT/'firmware/dependencies.json','dependencies.json'),(ROOT/'LICENSE-STATUS.md','LICENSE-STATUS.md'),(ROOT/'THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md')]
 install='Candidate source; hardware acceptance pending. Open IncubatorController/IncubatorController.ino. Install ESP32 core 3.3.12 and the display libraries pinned in dependencies.json; copy libraries/IncubatorNimBLE to your Arduino libraries folder. Select ESP32C3 Dev Module, USB CDC On Boot Enabled, 4MB Flash, Huge APP. Set your NAS URL in web_entry.h; board_config.h holds pins, module_config.h holds modules/timezone/journal thresholds. Preserve NVS and the partition layout. Test locked with 24V disconnected. Full bilingual instructions are in the complete Source ZIP.\n'
 write_zip(fw,files,{'INSTALL.txt':install.encode(),'INSTALL.zh-CN.txt':('候选源码，待实机验收。打开IncubatorController内同名ino；核心3.3.12，依dependencies.json安装OLED库，并将libraries内BLE库放入Arduino库目录。选ESP32C3 Dev Module、USB CDC开启、4MB、Huge APP。web_entry.h设置NAS URL，board_config.h为针脚，module_config.h为模块/时区/缓存阶段。保留NVS和分区；先断24V锁定测试。完整中英文说明见总源码包。\n').encode()})
 nas=out/'NAS-Recorder-2.2.3.zip'
 files=[(f,'recorder/'+n) for f,n in entries(ROOT/'services/recorder')]+[(ROOT/'tools/upgrade_existing_compose.py','tools/upgrade_existing_compose.py')]
 install='Cumulative upgrade; preserve compose.local.yaml, .env, all secrets, SMTP files and records. Back up SQLite using its backup API or stop writers before copying. Extract to a NEW folder. Generate a side-by-side configuration: python3 tools/upgrade_existing_compose.py --file /path/to/existing/compose.local.yaml --source /path/to/extracted/recorder . The updater accepts JSON-format YAML; traditional YAML requires manual merging. Inspect the generated compose.local.2.2.3.yaml, then run docker compose -f THAT_FILE config --quiet and docker compose -f THAT_FILE up -d . Add ACL entries from ACL-ADD.txt to the correct existing user blocks and HUP the broker. Upgrade NAS before firmware. Do not erase history. Full first-install instructions are in the complete Source ZIP.\n'
 zh='累计升级包，不覆盖compose.local.yaml、.env、密码、SMTP配置和records，不回退Docker。先用SQLite备份API备份或停写入后复制。解压到新目录，逐条执行（将路径换为实际位置）：\n\npython3 tools/upgrade_existing_compose.py --file /原目录/compose.local.yaml --source /本包解压位置/recorder\n\n检查生成的并排compose.local.2.2.3.yaml；再以该文件执行docker compose -f 文件路径 config --quiet，以及docker compose -f 文件路径 up -d。传统YAML需手工合并挂载。ACL-ADD.txt中的权限分别加到原有对应用户块，再HUP broker。先升级NAS再刷固件，保留所有历史。首次部署请用总源码包的完整中英文说明。\n'
 acl='# Add within the existing incubator_device user block (replace DEVICE_ID):\ntopic write incubator/DEVICE_ID/samples\ntopic read incubator/DEVICE_ID/samples/ack\n# Add within incubator_logger user block:\ntopic read incubator/DEVICE_ID/samples\ntopic write incubator/DEVICE_ID/samples/ack\n'
 write_zip(nas,files,{'INSTALL.txt':install.encode(),'INSTALL.zh-CN.txt':zh.encode(),'ACL-ADD.txt':acl.encode()})
 lines=[hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.name for f in (source,fw,nas)];(out/'SHA256SUMS.txt').write_text('\n'.join(lines)+'\n',encoding='ascii');print('Prepared: '+str(out))
if __name__=='__main__':main()
