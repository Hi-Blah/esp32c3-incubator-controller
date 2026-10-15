"""Authenticated LAN dashboard, calculations and long-term plots served from NAS."""
import base64,hmac,json,os,sqlite3,time,threading,secrets,logging
import sample_journal
import device_gateway
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse,parse_qs
import nas_analysis
import notifier
from contextlib import closing

def start(path,device,password,gateway=None):
 csrf=secrets.token_hex(24)
 class Handler(BaseHTTPRequestHandler):
  def log_message(self,*_):pass
  def authenticated(self):
   value=self.headers.get('Authorization','');wanted='Basic '+base64.b64encode(('incubator:'+password).encode()).decode()
   if not password or not hmac.compare_digest(value,wanted):self.send_response(401);self.send_header('WWW-Authenticate','Basic realm="Incubator NAS"');self.end_headers();return False
   return True
  def send_json(self,data,status=200):
   body=json.dumps(data,ensure_ascii=False,allow_nan=False).encode();self.send_response(status);self.send_header('Content-Type','application/json; charset=utf-8');self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
  def do_GET(self):
   route=urlparse(self.path);q=parse_qs(route.query)
   if route.path=="/assets/ready":
    data=b'{"version":"NAS-Recorder-2.2.3","device_ui":true}';self.send_response(200);self.send_header("Content-Type","application/json");self.send_header("Access-Control-Allow-Origin","*");self.send_header("Cache-Control","no-store");self.send_header("Content-Length",str(len(data)));self.end_headers();self.wfile.write(data);return
   if not self.authenticated():return
   if route.path=="/device/":
    data=Path(__file__).with_name("device.html").read_bytes();self.send_response(200);self.send_header("Content-Type","text/html; charset=utf-8");self.send_header("Content-Length",str(len(data)));self.end_headers();self.wfile.write(data);return
   if route.path=='/':
    data=Path(__file__).with_name('dashboard.html').read_bytes();self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data);return
   try:
    with closing(sqlite3.connect(path,timeout=3)) as db, db:
     if route.path=='/device/api/state':
      self.send_json(device_gateway.web_state(db,device,csrf,urlparse('//'+self.headers.get('Host','')).hostname or 'nas.example.local'))
     elif route.path=='/api/state':
      row=db.execute('SELECT payload,last_live_ms,offline FROM nas_live WHERE device=?',(device,)).fetchone();out={'state':json.loads(row[0]) if row else None,'received_ms':row[1] if row else 0,'offline':not row or bool(row[2]),'channels':[]}
      for ch in (1,2):
       cfg=nas_analysis.settings(db,device,ch);m=nas_analysis.model_for(db,device,ch);r=db.execute('SELECT payload FROM nas_analysis_cache WHERE device=? AND channel=?',(device,ch)).fetchone();out['channels'].append({'settings':cfg,'analysis':json.loads(r[0]) if r else None,'model':m})
      self.send_json(out)
     elif route.path=='/api/journal':self.send_json(sample_journal.status(db,device))
     elif route.path=='/api/samples.csv':
      self.send_response(200);self.send_header('Content-Type','text/csv; charset=utf-8');self.send_header('Content-Disposition','attachment; filename="offline-samples.csv"');self.send_header('Cache-Control','no-store');self.send_header('Connection','close');self.end_headers();self.close_connection=True;self.wfile.write(bytes([239,187,191]))
      for chunk in sample_journal.csv_iter(db,device):self.wfile.write(chunk.encode('utf8'))
     elif route.path=='/api/history':
      seconds=max(60,min(604800,int(q.get('seconds',['3600'])[0])));now=time.time_ns()//1000000;since=now-seconds*1000;step=max(1000,seconds*1000//1000)
      env=db.execute('SELECT (epoch_ms/?)*?,AVG(temperature),AVG(humidity) FROM sensor_samples WHERE epoch_ms>=? AND valid=1 AND trusted=1 GROUP BY epoch_ms/? ORDER BY epoch_ms/?',(step,step,since,step,step)).fetchall();light=db.execute('SELECT (epoch_ms/?)*?,channel,AVG(raw),AVG(level) FROM light_samples WHERE epoch_ms>=? AND trusted=1 GROUP BY epoch_ms/?,channel ORDER BY epoch_ms/?',(step,step,since,step,step)).fetchall();self.send_json({'environment':env,'light':light,'step_ms':step})
     elif route.path=='/api/notifications':self.send_json(db.execute('SELECT category,channel,created_ms,subject,status FROM notifications ORDER BY id DESC LIMIT 50').fetchall())
     elif route.path=='/api/mail-status':self.send_json(notifier.mail_status(db,time.time_ns()//1000000))
     else:self.send_json({'error':'Not found'},404)
   except ValueError as e:self.send_json({'ok':False,'error':'ValueError','message':str(e)},400)
   except sqlite3.Error:
    logging.exception('NAS dashboard database query failed')
    self.send_json({'ok':False,'error':'DatabaseError','message':'记录数据库查询失败，请查看记录器日志'},503)
   except (KeyError,TypeError,IndexError):
    logging.exception('NAS dashboard state/configuration shape mismatch')
    self.send_json({'ok':False,'error':'StateFormatError','message':'状态或程序配置格式不兼容，请核对记录器文件版本'},503)
  def do_POST(self):
   if not self.authenticated():return
   origin=self.headers.get('Origin');host=self.headers.get('Host')
   if origin and urlparse(origin).netloc!=host:self.send_json({'error':'Origin mismatch'},403);return
   if urlparse(self.path).path=="/device/api/command":
    if not gateway:self.send_json({"ok":False,"message":"Gateway unavailable"},503);return
    if not hmac.compare_digest(self.headers.get("X-CSRF-Token",""),csrf):self.send_json({"ok":False,"message":"CSRF mismatch"},403);return
    try:
     length=int(self.headers.get("Content-Length","0"))
     if not 1<=length<=2048:raise ValueError("Request too large")
     d=parse_qs(self.rfile.read(length).decode("ascii"),strict_parsing=True);cmd=d.get("cmd",[""])[0]
     with closing(sqlite3.connect(path,timeout=3)) as db:
      row=db.execute("SELECT payload,last_live_ms,offline FROM nas_live WHERE device=?",(device,)).fetchone()
     if not row or row[2]:raise ValueError("Device unavailable")
     self.send_json(gateway.command(cmd,json.loads(row[0]),row[1]))
    except (ValueError,sqlite3.Error,UnicodeError) as e:self.send_json({"ok":False,"message":str(e)},400)
    return
   if self.headers.get('Content-Type','').split(';')[0]!='application/json':self.send_json({'error':'JSON required'},415);return
   try:
    length=int(self.headers.get('Content-Length','0'))
    if not 1<=length<=4096:raise ValueError('length')
    d=json.loads(self.rfile.read(length));ch=nas_analysis.integer(d.get('channel'),1,2)
    with closing(sqlite3.connect(path,timeout=3)) as db, db:
     cfg=nas_analysis.settings(db,device,ch);cfg['on_min']=nas_analysis.integer(d.get('on_min',cfg['on_min']),1,1000);cfg['off_max']=nas_analysis.integer(d.get('off_max',cfg['off_max']),cfg['on_min'],1000);cfg['dim_ratio']=nas_analysis.integer(d.get('dim_ratio',cfg['dim_ratio']),1,1000);cfg['hold_ms']=nas_analysis.integer(d.get('hold_ms',cfg['hold_ms']),1000,60000);cfg['enabled']=int(d.get('enabled') is True)
     if cfg['enabled'] and nas_analysis.normalize(0,cfg['dark'],cfg['bright']) is None:raise ValueError('Capture baselines first')
     db.execute('UPDATE optical_settings SET on_min=?,off_max=?,dim_ratio=?,enabled=?,hold_ms=? WHERE device=? AND channel=?',(cfg['on_min'],cfg['off_max'],cfg['dim_ratio'],cfg['enabled'],cfg['hold_ms'],device,ch))
    self.send_json({'ok':True})
   except (ValueError,sqlite3.Error,TypeError) as e:self.send_json({'error':str(e)},400)
 server=ThreadingHTTPServer(('0.0.0.0',int(os.getenv('DASHBOARD_PORT','8080'))),Handler);threading.Thread(target=server.serve_forever,daemon=True).start();return server
