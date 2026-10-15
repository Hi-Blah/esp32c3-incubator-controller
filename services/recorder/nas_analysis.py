"""NAS-only optical analysis. No light-command privileges; no mail sent here."""
import json, math, time
from optical_store import enqueue
FREQUENCIES=(40,50,60,100,200,500,1000,2000,5000,10000,15000,20000)
DUTIES=(0,10,20,40,50,60,80,90,100)
STATUS=('UNCONFIGURED','MONITORING_DISABLED','OK','MISSING_LIGHT','DIM_LIGHT','STUCK_ON','PWM_MODEL_REQUIRED','CALIBRATING','LOW_DUTY_ESTIMATE','NAS_UNAVAILABLE','MANUAL_OR_STOPPED')
def initialize(db):
 db.executescript('''
 CREATE TABLE IF NOT EXISTS optical_settings(device TEXT,channel INTEGER,dark INTEGER,bright INTEGER,on_min INTEGER DEFAULT 10,off_max INTEGER DEFAULT 50,dim_ratio INTEGER DEFAULT 600,enabled INTEGER DEFAULT 0,hold_ms INTEGER DEFAULT 3000,PRIMARY KEY(device,channel));
 CREATE TABLE IF NOT EXISTS optical_models(device TEXT,channel INTEGER,run TEXT,model TEXT,updated_ms INTEGER,PRIMARY KEY(device,channel));
 CREATE TABLE IF NOT EXISTS raw_calibration(event_id TEXT PRIMARY KEY,device TEXT,run TEXT,channel INTEGER,raw_sum INTEGER,cross_sum INTEGER,sample_count INTEGER,payload TEXT);
 CREATE TABLE IF NOT EXISTS nas_analysis_cache(device TEXT,channel INTEGER,payload TEXT,received_ms INTEGER,PRIMARY KEY(device,channel));
 CREATE TABLE IF NOT EXISTS nas_outage_notice(device TEXT PRIMARY KEY,last_live_ms INTEGER,notified INTEGER);
 CREATE TABLE IF NOT EXISTS nas_live(device TEXT PRIMARY KEY,boot TEXT,last_live_ms INTEGER,offline INTEGER,payload TEXT);
 CREATE TABLE IF NOT EXISTS nas_candidate(device TEXT,channel INTEGER,boot TEXT,status INTEGER,since_ms INTEGER,last_sample INTEGER,PRIMARY KEY(device,channel));
 CREATE TABLE IF NOT EXISTS nas_retired_boots(device TEXT,boot TEXT,PRIMARY KEY(device,boot));
 CREATE TABLE IF NOT EXISTS device_configs(device TEXT,cfg INTEGER,payload TEXT,received_ms INTEGER,PRIMARY KEY(device,cfg));
 ''')
def integer(x,lo,hi):
 if type(x)is not int or not lo<=x<=hi:raise ValueError('analysis integer bounds')
 return x
def settings(db,device,ch):
 fresh=not db.execute('SELECT 1 FROM optical_settings WHERE device=? AND channel=?',(device,ch)).fetchone()
 db.execute('INSERT OR IGNORE INTO optical_settings(device,channel) VALUES(?,?)',(device,ch))
 if fresh:
  for payload, in db.execute('SELECT payload FROM light_samples WHERE channel=? AND calibrated=1 ORDER BY received_ms DESC LIMIT 20',(ch,)):
   try:
    old=json.loads(payload);dark,bright=old.get('dark'),old.get('bright')
    if type(dark)is int and type(bright)is int and 0<=dark<=4095 and 0<=bright<=4095 and abs(bright-dark)>=64:
     on=integer(old.get('onThreshold',10),1,1000);off=integer(old.get('offThreshold',50),on,1000);dim=integer(old.get('dimRatio',600),1,1000)
     db.execute('UPDATE optical_settings SET dark=?,bright=?,on_min=?,off_max=?,dim_ratio=?,enabled=? WHERE device=? AND channel=?',(dark,bright,on,off,dim,int(old.get('enabled') is True),device,ch));break
   except (ValueError,TypeError):continue
  runs=db.execute('SELECT run,MAX(received_ms) FROM calibration_points WHERE device=? AND channel=? GROUP BY run ORDER BY MAX(received_ms) DESC LIMIT 10',(device,ch)).fetchall()
  for run,stamp in runs:
   if build_model(db,device,ch,run,stamp):break
 row=db.execute('SELECT dark,bright,on_min,off_max,dim_ratio,enabled,hold_ms FROM optical_settings WHERE device=? AND channel=?',(device,ch)).fetchone()
 return dict(zip(('dark','bright','on_min','off_max','dim_ratio','enabled','hold_ms'),row))
def normalize(raw,dark,bright):
 if dark is None or bright is None or abs(bright-dark)<64:return None
 return max(0,min(1000,round((raw-dark)*1000/(bright-dark))))
def interpolate(model,hz,duty):
 if not model or not FREQUENCIES[0]<=hz<=FREQUENCIES[-1] or not 0<=duty<=100:return None
 def bracket(xs,x):
  i=max(i for i,v in enumerate(xs) if v<=x);return i,min(i+1,len(xs)-1)
 i,k=bracket(FREQUENCIES,hz);j,l=bracket(DUTIES,duty)
 def mix(a,b,x,width):return a+(b-a)*x/width if width else a
 grid=model['adc'];low=mix(grid[i][j],grid[i][l],duty-DUTIES[j],DUTIES[l]-DUTIES[j]);high=mix(grid[k][j],grid[k][l],duty-DUTIES[j],DUTIES[l]-DUTIES[j]);raw=mix(low,high,hz-FREQUENCIES[i],FREQUENCIES[k]-FREQUENCIES[i])
 return normalize(raw,model['dark'],model['bright'])
def model_for(db,device,ch):
 row=db.execute('SELECT model FROM optical_models WHERE device=? AND channel=?',(device,ch)).fetchone();return json.loads(row[0]) if row else None
def live(db,device,data,now):
 prior=db.execute('SELECT boot,last_live_ms,offline FROM nas_live WHERE device=?',(device,)).fetchone()
 if prior and prior[0]!=data['boot_id']:db.execute('INSERT OR IGNORE INTO nas_retired_boots VALUES(?,?)',(device,prior[0]))
 notice=db.execute('SELECT last_live_ms,notified FROM nas_outage_notice WHERE device=?',(device,)).fetchone()
 if prior and notice and notice[0]==prior[1] and notice[1]:enqueue(db,device+':restored:'+str(now),'notice_online',0,now,'[提示] 培养箱连接恢复',f'设备{device}恢复实时上报；距最后实时数据约{(now-prior[1])/1000:.1f}秒。如Flash缓存可用，会按容量和降采样规则补传；不是准确停电时长。')
 db.execute('DELETE FROM nas_outage_notice WHERE device=?',(device,))
 db.execute('INSERT INTO nas_live VALUES(?,?,?,?,?) ON CONFLICT(device) DO UPDATE SET boot=excluded.boot,last_live_ms=excluded.last_live_ms,offline=0,payload=excluded.payload',(device,data['boot_id'],now,0,json.dumps(data,separators=(',',':'))))
def watchdog(db,device,now,threshold=60000,notice_after=180000):
 # UI/control freshness remains separate from email policy. Backfill never calls live().
 row=db.execute('SELECT boot,last_live_ms,offline FROM nas_live WHERE device=?',(device,)).fetchone()
 if not row:return False
 age=now-row[1]
 if age>=threshold:db.execute('UPDATE nas_live SET offline=1 WHERE device=?',(device,))
 if age>notice_after:
  notice=db.execute('SELECT last_live_ms,notified FROM nas_outage_notice WHERE device=?',(device,)).fetchone()
  if not notice or notice[0]!=row[1] or not notice[1]:
   enqueue(db,device+':missing:'+str(row[1]),'notice_offline',0,now,'[提示] 培养箱实时上报中断',f'设备{device}超过{notice_after/1000:.0f}秒未上报新数据。可能是供电、Wi-Fi、路由、MQTT或NAS接收问题；不能单凭此确认停电。如开发板Flash缓存已启用且可用，重连后会按容量与降采样规则补传历史。')
   db.execute('INSERT OR REPLACE INTO nas_outage_notice VALUES(?,?,1)',(device,row[1]))
 return age<threshold
def state(db,device,identity,data,now,retained):
 p=data.get('photo',{})
 if retained:return
 if db.execute('SELECT 1 FROM nas_retired_boots WHERE device=? AND boot=?',(device,identity)).fetchone():return
 age=data.get('uptime_ms',0)-p.get('sampled_ms',0)
 if p.get('compiled') and (age<0 or age>5000):return
 if p.get('compiled') and data.get('photo_time_trusted') and abs(now-data.get('photo_epoch_ms',0))>30000:return
 live(db,device,data,now)
 if not p.get('compiled'):return
 channels=p.get('channels');sample=integer(p.get('sampled_ms'),0,2**63-1)
 if not isinstance(channels,list) or len(channels)!=2:raise ValueError('two ADC channels required')
 auto=bool(p.get('automatic',{}).get('active'))
 for ch,c in enumerate(channels,1):
  cache=db.execute('SELECT payload FROM nas_analysis_cache WHERE device=? AND channel=?',(device,ch)).fetchone()
  if cache:
   cached=json.loads(cache[0])
   if cached['boot']==identity and sample<=cached['sample_ms']:continue
  raw=integer(c.get('raw'),0,4095);valid=c.get('valid') is True;cfg=settings(db,device,ch);level=normalize(raw,cfg['dark'],cfg['bright']) if valid else None;model=model_for(db,device,ch)
  output=data.get('l'+str(ch)) is True;mode=integer(data.get('mode'+str(ch)),0,2);pwm=data.get('pwm'+str(ch)) is True;duty=integer(data.get('duty'+str(ch)),1,99);hz=integer(data.get('frequency'+str(ch)),40,20000)
  expected=(interpolate(model,hz,duty) if pwm else 1000) if output else 0;known=expected is not None and level is not None;reliable=not pwm or duty>=10
  ratio=min(10000,round(level*1000/expected)) if known and expected else 0;detected=level is not None and level>cfg['off_max']
  status=2;candidate=2;fault=False
  permitted=data.get('armed') is True and data.get('enabled'+str(ch),True) is True
  scheduled=mode in (1,2)
  active=permitted and (scheduled or (mode==0 and output))
  if level is None:status=0
  elif not cfg['enabled']:status=1
  elif auto:status=7
  elif not active:status=10
  elif not output and level>cfg['off_max']:candidate=5
  elif output and not reliable:status=8
  elif output and level<cfg['on_min']:candidate=3
  elif scheduled and output and not known:status=6
  elif scheduled and output and ratio<cfg['dim_ratio']:candidate=4
  if candidate!=2 and status==2:
   previous=db.execute('SELECT boot,status,since_ms,last_sample FROM nas_candidate WHERE device=? AND channel=?',(device,ch)).fetchone()
   if previous and previous[0]==identity and previous[1]==candidate:
    since=previous[2];fault=sample!=previous[3] and now-since>=cfg['hold_ms'];status=candidate if fault else 2
   else:since=now
   db.execute('INSERT INTO nas_candidate VALUES(?,?,?,?,?,?) ON CONFLICT(device,channel) DO UPDATE SET boot=excluded.boot,status=excluded.status,since_ms=excluded.since_ms,last_sample=excluded.last_sample',(device,ch,identity,candidate,since,sample))
  else:db.execute('DELETE FROM nas_candidate WHERE device=? AND channel=?',(device,ch))
  alert=device+':photo'+str(ch);prior=db.execute('SELECT active,status FROM alert_state WHERE key=?',(alert,)).fetchone()
  if fault and (not prior or not prior[0] or prior[1]!=STATUS[status]):enqueue(db,alert+':'+str(now),'photo_'+STATUS[status],ch,now,'[警报] 培养箱光源'+str(ch)+'反馈异常',f'模式{mode}；状态{STATUS[status]}；开关命令{output}；ADC{raw}；相对响应{(level or 0)/10:.1f}%；预期{(expected or 0)/10:.1f}%；达标率{ratio/10:.1f}%。请检查灯带供电、遮挡和传感器。不是照度。')
  db.execute('INSERT INTO alert_state VALUES(?,?,?,?) ON CONFLICT(key) DO UPDATE SET active=excluded.active,status=excluded.status,changed_ms=CASE WHEN active!=excluded.active OR status!=excluded.status THEN excluded.changed_ms ELSE changed_ms END',(alert,int(fault),STATUS[status],now))
  wire={'v':1,'boot':identity,'sample_ms':sample,'channel':ch,'enabled':int(bool(cfg['enabled'])),'calibrated':int(level is not None),'on':int(detected),'fault':int(fault),'level':level or 0,'expected':expected or 0,'ratio':ratio,'status':status,'dark':cfg['dark'] or 0,'bright':cfg['bright'] or 0,'on_min':cfg['on_min'],'off_max':cfg['off_max'],'dim_ratio':cfg['dim_ratio'],'model_ready':int(model is not None),'reliable':int(reliable),'known':int(known)}
  payload=json.dumps(wire,separators=(',',':'));db.execute('INSERT INTO nas_analysis_cache VALUES(?,?,?,?) ON CONFLICT(device,channel) DO UPDATE SET payload=excluded.payload,received_ms=excluded.received_ms',(device,ch,payload,now))
  db.execute('INSERT OR IGNORE INTO light_samples VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',(identity,sample,ch,data.get('photo_epoch_ms',0),now,raw,level,expected,ratio,int(level is not None),int(known),int(bool(cfg['enabled'])),int(detected),int(fault),STATUS[status],int(bool(data.get('photo_time_trusted'))),json.dumps(c,separators=(',',':'))))
def command(db,device,data,now):
 if data.get('v')!=1:raise ValueError('analysis command version')
 ch=integer(data.get('ch'),1,2);row=db.execute('SELECT boot,last_live_ms,payload FROM nas_live WHERE device=?',(device,)).fetchone()
 if not row or row[0]!=data.get('boot') or now-row[1]>15000:raise ValueError('fresh board handshake required')
 cfg=settings(db,device,ch);op=data.get('op');st=json.loads(row[2]);changed=False
 if op in ('dark','bright'):
  p=st.get('photo',{});channels=p.get('channels',[])
  if not p.get('compiled') or len(channels)!=2 or channels[ch-1].get('valid') is not True:raise ValueError('valid ADC module required')
  if p.get('automatic',{}).get('active'):raise ValueError('automatic calibration active')
  if op=='dark' and (st.get('l1') or st.get('l2')):raise ValueError('dark needs both outputs off')
  if op=='bright' and (not st.get('l'+str(ch)) or st.get('pwm'+str(ch)) or st.get('l'+str(3-ch))):raise ValueError('bright needs one full DC output')
  cfg[op]=integer(data.get('raw'),0,4095);cfg['enabled']=0;changed=True
 elif op=='on':
  if normalize(0,cfg['dark'],cfg['bright']) is None:raise ValueError('capture valid dark/full baselines first')
  cfg['enabled']=1
 elif op=='off':cfg['enabled']=0
 elif op=='thresholds':cfg['on_min']=integer(data.get('a'),1,1000);cfg['off_max']=integer(data.get('b'),cfg['on_min'],1000)
 elif op=='dim':cfg['dim_ratio']=integer(data.get('a'),1,1000)
 else:raise ValueError('unknown NAS analysis operation')
 db.execute('UPDATE optical_settings SET dark=?,bright=?,on_min=?,off_max=?,dim_ratio=?,enabled=?,hold_ms=? WHERE device=? AND channel=?',(*cfg.values(),device,ch))
 if changed:db.execute('DELETE FROM optical_models WHERE device=? AND channel=?',(device,ch))
 return {'ok':True,'channel':ch,'operation':op}
def calibration(db,device,data,now):
 if data.get('kind')!='calibration_point':return
 count=integer(data.get('sample_count',1),1,10000);rawsum=integer(data.get('raw_sum',data.get('raw',0)*count),0,4095*count);crosssum=integer(data.get('cross_sum',data.get('cross_raw',0)*count),0,4095*count)
 ch=integer(data.get('channel'),1,2);run=data['run'];full='sample_count' in data;db.execute('INSERT OR IGNORE INTO raw_calibration VALUES(?,?,?,?,?,?,?,?)',(data['event_id'],device,run,ch,rawsum if full else None,crosssum if full else None,count if full else None,json.dumps(data,separators=(',',':'))));db.execute('UPDATE calibration_points SET raw=?,cross_raw=? WHERE event_id=?',(rawsum/count,crosssum/count,data['event_id']))
 build_model(db,device,ch,run,now)

def build_model(db,device,ch,run,now):
 rows=db.execute('SELECT frequency,duty,raw FROM calibration_points WHERE device=? AND run=? AND channel=?',(device,run,ch)).fetchall();points={(r[0],r[1]):r[2] for r in rows}
 keys={(0,0),(0,100)}|{(f,d) for f in FREQUENCIES for d in DUTIES[1:-1]}
 if not keys<=points.keys():return False
 dark,bright=points[0,0],points[0,100]
 if normalize(0,dark,bright)is None:return False
 model={'run':run,'dark':dark,'bright':bright,'frequency':FREQUENCIES,'duty':DUTIES,'adc':[[dark if d==0 else bright if d==100 else points[f,d] for d in DUTIES] for f in FREQUENCIES]}
 db.execute('INSERT OR IGNORE INTO optical_settings(device,channel) VALUES(?,?)',(device,ch));db.execute('UPDATE optical_settings SET dark=?,bright=? WHERE device=? AND channel=?',(round(dark),round(bright),device,ch));db.execute('INSERT INTO optical_models VALUES(?,?,?,?,?) ON CONFLICT(device,channel) DO UPDATE SET run=excluded.run,model=excluded.model,updated_ms=excluded.updated_ms',(device,ch,run,json.dumps(model,separators=(',',':')),now))
 return True
