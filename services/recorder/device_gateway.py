"""Authenticated NAS UI reuses MQTT state; bounded, expiring command round trips."""
import copy,json,secrets,threading,time
import nas_analysis

ALLOWED=set('on off arm disarm daily cycle pwm pwmset manualresume dailyresume resume nasresume photocal photo clock errors status diag show test'.split())

class Gateway:
 def __init__(self,client,device):
  self.client,self.device=client,device
  self.serial=threading.Lock();self.lock=threading.Lock();self.pending={}
 def reply(self,data,retained=False):
  if retained or data.get('v')!=1:return
  with self.lock:
   p=self.pending.get(data.get('req'))
   if p:p['result']=data;p['event'].set()
 def command(self,text,state,received_ms,now=None):
  now=time.time_ns()//1000000 if now is None else now
  if not isinstance(text,str) or not 0<len(text)<=360 or any(ord(c)<32 or ord(c)>126 or c in '"\\' for c in text):raise ValueError('Invalid command')
  if text.split()[0] not in ALLOWED:raise ValueError('Network and credential settings use the local device page')
  age=now-received_ms
  if not 0<=age<=15000 or not state or not state.get('boot_id'):raise ValueError('Device state is stale; query before controlling')
  if not self.serial.acquire(blocking=False):raise ValueError('Another command is awaiting confirmation')
  req=secrets.token_hex(12);p={'event':threading.Event(),'result':None}
  try:
   with self.lock:self.pending[req]=p
   body={'v':1,'req':req,'boot':state['boot_id'],'until_ms':int(state['uptime_ms'])+age+6000,'cmd':text}
   payload=json.dumps(body,separators=(',',':'),ensure_ascii=True)
   if len(payload)>=512:raise ValueError('Command envelope too long')
   info=self.client.publish('incubator/'+self.device+'/analysis/device_command',payload,qos=1,retain=False)
   if info.rc!=0:raise ValueError('MQTT transport unavailable')
   if not p['event'].wait(5):return {'ok':False,'message':'No device confirmation; query current state before repeating'}
   return {'ok':p['result'].get('ok') is True,'message':str(p['result'].get('message',''))[:240]}
  finally:
   with self.lock:self.pending.pop(req,None)
   self.serial.release()

def web_state(db,device,csrf,host,now=None):
 now=time.time_ns()//1000000 if now is None else now
 row=db.execute('SELECT payload,last_live_ms,offline FROM nas_live WHERE device=?',(device,)).fetchone()
 if not row or row[2] or not 0<=now-row[1]<=15000:raise ValueError('Device temporarily unavailable; local scheduling continues')
 d=json.loads(row[0]);rev=d.get('config_revision',0)
 cfgrow=db.execute('SELECT payload FROM device_configs WHERE device=? AND cfg=?',(device,rev)).fetchone()
 if not cfgrow:raise ValueError('Waiting for current device configuration')
 cfg=json.loads(cfgrow[0]);ui=d.get('ui',{});rec=ui.get('rec',{})
 lights=[]
 for i,c in enumerate(cfg['channels']):
  n=str(i+1);r=copy.deepcopy(c.get('recovery',{}))
  r.update(dailyWaiting=(ui.get('dailyWaiting') or [False,False])[i],source=(rec.get('sources') or [rec.get('source','NONE')]*2)[i],gapMs=(rec.get('gaps') or [d.get('estimated_gap_ms',0)]*2)[i],estimatedOutageMs=(rec.get('outages') or [rec.get('outage',0)]*2)[i],startupWaitMs=(rec.get('startups') or [rec.get('startup',0)]*2)[i])
  for k,v in {'enabled':False,'manualEnabled':False,'dailyEnabled':True,'nasEnabled':False,'countOutage':True,'restartAfterSeconds':0,'checkpointSeconds':60}.items():r.setdefault(k,v)
  lights.append(dict(c,enabled=bool(d.get('enabled'+n)),output=bool(d.get('l'+n)),requested=bool(d.get('requested'+n,d.get('l'+n))),mode=['MANUAL','DAILY','CYCLE'][d.get('mode'+n,0)],finished=bool(d.get('finished'+n,False)),row=max(0,d.get('step'+n,1)-1),completed=d.get('rounds'+n,0),remainingMs=d.get('left'+n+'_ms',0),actualFrequency=d.get('actual_frequency'+n,c.get('frequency',0)),recovery=r))
 photo=copy.deepcopy(d.get('photo',{'compiled':False,'channels':[{},{}]}));photo['sampledMs']=photo.get('sampled_ms',0)
 for i,raw in enumerate(photo['channels']):
  ch=i+1;settings=nas_analysis.settings(db,device,ch)
  row_analysis=db.execute('SELECT payload,received_ms FROM nas_analysis_cache WHERE device=? AND channel=?',(device,ch)).fetchone()
  analysis=json.loads(row_analysis[0]) if row_analysis and now-row_analysis[1]<=15000 else {}
  raw.update(enabled=bool(settings['enabled']),calibrated=bool(analysis.get('calibrated')),dark=settings['dark'] or 0,bright=settings['bright'] or 0,level=analysis.get('level',0),expected=analysis.get('expected',0),expected_valid=bool(analysis.get('known')),ratio=analysis.get('ratio',0),on=bool(analysis.get('on')),fault=bool(analysis.get('fault')),status=analysis.get('status',9),status_text=nas_analysis.STATUS[analysis.get('status',9)],model_ready=bool(analysis.get('model_ready')),model_reliable=bool(analysis.get('reliable')),onThreshold=settings['on_min'],offThreshold=settings['off_max'],dimRatio=settings['dim_ratio'])
 auto=photo.setdefault('automatic',{})
 for k,v in {'active':False,'completed':0,'total':0,'stage':0,'channel':0,'frequency':0,'duty':0,'message':''}.items():auto.setdefault(k,v)
 r={'enabled':any(c['recovery']['enabled'] for c in lights),'manualEnabled':any(c['recovery']['manualEnabled'] for c in lights),'nasEnabled':any(c['recovery']['nasEnabled'] for c in lights),'waiting':bool(rec.get('waiting')),'nasWaiting':bool(rec.get('nasWaiting')),'source':rec.get('source','NONE'),'countOutage':True,'restartAfterSeconds':0,'checkpointSeconds':60,'checkpointEpochMs':d.get('last_saved_epoch_ms',0),'gapMs':d.get('estimated_gap_ms',0),'estimatedOutageMs':rec.get('outage',0),'startupWaitMs':rec.get('startup',0)}
 return {'ok':True,'firmware':d.get('firmware',''),'csrf':csrf,'clock':d.get('time') or 'UNSYNC','clockSource':d.get('time_source','UNSYNC'),'armed':bool(d.get('armed')),'limit':d.get('limit',2),'logicPassed':bool(ui.get('logic',d.get('logic_passed',False))),'storageReady':bool(ui.get('nvs',d.get('nvs_ready',False))),'temperature':d.get('temperature'),'humidity':d.get('humidity'),'uptimeSeconds':d.get('uptime_ms',0)//1000,'uptimeMs':d.get('uptime_ms',0),'epochMs':d.get('epoch_ms',d.get('sample_epoch_ms',0)),'sampledAtMs':d.get('sampled_uptime_ms',0),'sampleEpochMs':d.get('sample_epoch_ms',0),'sampleTimeTrusted':bool(d.get('sample_time_trusted')),'health':ui.get('health',{'active':0,'latched':0,'lastError':''}),'photo':photo,'wifi':{'connected':True,'ssid':ui.get('ssid',''),'ip':ui.get('ip',''),'apActive':False,'apIp':'','profiles':[]},'ble':ui.get('ble',{'ready':False,'connected':False,'secure':False,'advertisingPaused':False,'name':''}),'recovery':r,'lights':lights,'mqtt':{'enabled':True,'connected':True,'nasAlive':True,'credentialsSaved':True,'host':host,'port':1883,'username':'incubator_device','lastReply':'参数由NAS MQTT缓存提供','lastError':0},'diagnostics':{'bootId':d.get('boot_id',''),'resetReason':d.get('reset_reason',''),'bootCount':d.get('boot_count',0),'logReady':bool(d.get('log_ready')),'pendingEvents':d.get('pending_events',0),'droppedEvents':d.get('dropped_events',0),'writeFailures':d.get('log_write_failures',0),'heapFree':ui.get('heap',0),'heapMin':ui.get('min',0),'heapLargest':ui.get('largest',0),'logFailureStage':'','logFailureAtMs':0,'logFailureErrno':0}}
