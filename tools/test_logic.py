"""Native C++ logic tests, independent of Arduino or a connected device."""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
PURE=('connected_regression','daily_log','device_identity','dht_decode','entry_state','format_decimal','http_transfer','mqtt_reconnect','nas_web_command','network_budget','program_logic','recovery_protocol')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',default='c++');a=p.parse_args();fw=ROOT/'firmware/IncubatorController'
 for name in PURE:subprocess.run([a.compiler,'-std=c++17','-fsyntax-only','-I'+str(fw),str(fw/(name+'_tests.cpp'))],check=True)
 subprocess.run([a.compiler,'-std=c++17','-fsyntax-only','-I'+str(fw),str(ROOT/'tests/sample_policy_tests.cpp')],check=True)
 with tempfile.TemporaryDirectory(prefix='incubator-test-') as folder:
  w=Path(folder);exe=w/('store-test.exe' if __import__('os').name=='nt' else 'store-test')
  subprocess.run([a.compiler,'-std=c++17','-O1','-I'+str(fw),str(ROOT/'tests/sample_store_test.cpp'),'-o',str(exe)],check=True)
  subprocess.run([str(exe),str(w/'records')],check=True)
 print('PASS native logic and durable store checks')
if __name__=='__main__':main()
