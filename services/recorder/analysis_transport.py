"""NAS-derived HA entities and heartbeat. Never controls lamp outputs."""
import json
from nas_analysis import STATUS

def discovery(client,device):
 base='incubator/'+device
 for ch in (1,2):
  for obj,component,name,expression,field in [('level','sensor','相对响应','value_json.level / 10','calibrated'),('expected','sensor','预期响应','value_json.expected / 10','known'),('ratio','sensor','达标率','value_json.ratio / 10','known'),('on','binary_sensor','有光',"'ON' if value_json.on else 'OFF'",'calibrated'),('fault','binary_sensor','光反馈异常',"'ON' if value_json.fault else 'OFF'",'calibrated'),('status','sensor','光反馈状态',repr(list(STATUS))+'[value_json.status]',None)]:
   object_id=f'photo{ch}_{obj}';topic=base+f'/analysis/photo{ch}'
   config={'name':f'光源{ch} {name}','unique_id':f'incubator_{device}_{object_id}','state_topic':topic,'value_template':'{{ '+expression+' }}','availability_mode':'all','availability':[{'topic':base+'/logger/availability','payload_available':'online','payload_not_available':'offline'},{'topic':base+f'/analysis/photo{ch}/availability','payload_available':'online','payload_not_available':'offline'}],'device':{'identifiers':['incubator_'+device],'name':'光照培养箱','sw_version':'Connected-2.2.2 NAS analysis'}}
   if field:config['availability'].append({'topic':topic,'value_template':"{{ 'online' if value_json."+field+" else 'offline' }}",'payload_available':'online','payload_not_available':'offline'})
   if obj in ('level','expected','ratio'):config['unit_of_measurement']='%';config['state_class']='measurement'
   if component=='binary_sensor':config.update(payload_on='ON',payload_off='OFF')
   client.publish(f'homeassistant/{component}/incubator_{device}/{object_id}/config',json.dumps(config,ensure_ascii=False,separators=(',',':')),qos=1,retain=True)

def results(client,db,device,now):
 for ch,payload,received in db.execute('SELECT channel,payload,received_ms FROM nas_analysis_cache WHERE device=?',(device,)).fetchall():
  if now-received>15000:continue
  client.publish(f'incubator/{device}/analysis/photo{ch}',payload,qos=0,retain=False)
  client.publish(f'incubator/{device}/analysis/photo{ch}/availability','online',qos=0,retain=True)

def tick(client,db,device,now,connected):
 import nas_analysis
 alive=nas_analysis.watchdog(db,device,now)
 if not connected:return
 row=db.execute('SELECT boot,last_live_ms,offline,payload FROM nas_live WHERE device=?',(device,)).fetchone()
 if row and alive:client.publish(f'incubator/{device}/analysis/heartbeat',json.dumps({'v':1,'boot':row[0],'uptime_ms':json.loads(row[3])['uptime_ms'],'received_ms':row[1]},separators=(',',':')),qos=0,retain=False)
 for ch in (1,2):
  r=db.execute('SELECT received_ms FROM nas_analysis_cache WHERE device=? AND channel=?',(device,ch)).fetchone()
  if not r or now-r[0]>15000:client.publish(f'incubator/{device}/analysis/photo{ch}/availability','offline',qos=0,retain=True)
