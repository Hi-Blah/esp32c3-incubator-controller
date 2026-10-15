#include "sample_journal.h"
#include "device_identity.h"
#include "mqtt_service.h"
#include "network_budget.h"
#include "event_log.h"
#include "format_decimal.h"
#include "mqtt_raw_tcp.h"
#include "mqtt_publish_flow.h"
#include "light_feedback.h"
#include "health_service.h"
#include "nas_web_command.h"
#include "json_stream.h"
#include "daq_json.h"
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mqtt_client.h>
#include <Preferences.h>
#include <esp_timer.h>

#include <atomic>
#include <math.h>
#include <WiFi.h>
#include <NetworkClient.h>
namespace {
constexpr uint32_t SCHEMA=0xC3064001;
struct Config { uint32_t schema; bool enabled; uint16_t port; char host[97],username[33],password[128]; };
Config stored={},active={};
portMUX_TYPE configMux=portMUX_INITIALIZER_UNLOCKED,viewMux=portMUX_INITIALIZER_UNLOCKED;
MqttView view={};
MqttLifecycle lifecycle={0,0,0,"none"};
std::atomic<bool> changed(false),connected(false),announce(false),probeRequested(false);
esp_mqtt_client_handle_t client=nullptr;
std::atomic<int> lastAck(0);
std::atomic<uint32_t> connectionEpoch(0);
mqttflow::Flow flow;
mqttflow::RoutineScheduler routine;
std::atomic<uint32_t> clientRetryAt(0);uint32_t networkGeneration=0,disconnectedAt=0;
char suffix[7]={},base[40]={},stateTopic[64]={},availability[64]={},commandsTopic[64]={},clientId[40]={},progressTopic[64]={},requestTopic[64]={},replyTopic[64]={};
constexpr uint8_t DISCOVERY_COUNT=13;
uint8_t discoveryIndex=0;
uint32_t nextCommand=0,lastState=0,lastAttempt=0,lastProgress=0,lastRecoveryRequest=0;

uint32_t signature=0,lastConfig=0;uint64_t lastConfigState=UINT64_MAX;uint8_t lastMask=255;QueueHandle_t analysisQueue=nullptr;std::atomic<uint32_t> nasSeen(0);
struct AnalysisCommand{char json[256];};
bool announced=false;
uint32_t remotePendingId=0;char remoteReq[25]={},remoteSeen[8][25]={},remoteText[241]={};uint8_t remoteCursor=0;bool remoteOk=false;std::atomic<bool> remoteReady(false);
// Discovery and state publishing run serially on Arduino loopTask; enqueue copies the payload.
char publishBuffer[2048]={},discoveryOptions[768]={};
uint64_t monoMs(){return static_cast<uint64_t>(esp_timer_get_time())/1000;}
void waitFor(const char *reason,uint32_t heap=0,uint32_t largest=0){portENTER_CRITICAL(&viewMux);snprintf(view.waitReason,sizeof(view.waitReason),"%s",reason);view.waitHeap=heap;view.waitLargest=largest;portEXIT_CRITICAL(&viewMux);}
void setReply(const char *text){portENTER_CRITICAL(&viewMux);snprintf(view.lastReply,sizeof(view.lastReply),"%s",text);portEXIT_CRITICAL(&viewMux);}
bool valid(const Config &c){
  if(c.schema!=SCHEMA||!c.port||!memchr(c.host,0,sizeof(c.host))||!memchr(c.username,0,sizeof(c.username))||!memchr(c.password,0,sizeof(c.password))||!c.host[0])return false;
  for(const char *p=c.host;*p;++p)if(!isalnum(static_cast<unsigned char>(*p))&&*p!='.'&&*p!='-')return false;
  for(const char *p=c.username;*p;++p)if(static_cast<unsigned char>(*p)<32||*p==127)return false;
  return !c.enabled || (c.username[0]&&c.password[0]);
}
void remoteCommand(const char *payload){
 ControlCommand c={};char req[25]={},boot[40]={};unsigned long long until=0;int end=0;
 const int n=sscanf(payload,"{\"v\":1,\"req\":\"%24[^\"]\",\"boot\":\"%39[^\"]\",\"until_ms\":%llu,\"cmd\":\"%360[^\"]\"}%n",req,boot,&until,c.text,&end);
 if(n!=4||payload[end]||!nasweb::token(req)||!nasweb::command(c.text)||!nasweb::fresh(monoMs(),until)||strcmp(boot,eventLogView().bootId)){setReply("ERR NAS web request stale or invalid; query current state");return;}
 char op[24]={};sscanf(c.text,"%23s",op);const char *allowed[]={"on","off","arm","disarm","daily","cycle","pwm","pwmset","manualresume","dailyresume","resume","nasresume","photocal","photo","clock","errors","status","diag","show","test"};bool permitted=false;for(auto name:allowed)if(!strcmp(name,op))permitted=true;if(!permitted){setReply("ERR use local device page for network or secret settings");return;}
 portENTER_CRITICAL(&viewMux);bool duplicate=false;for(auto &seen:remoteSeen)if(!strcmp(seen,req))duplicate=true;const bool occupied=remotePendingId!=0||remoteReady.load();if(!duplicate&&!occupied){remotePendingId=c.id=++nextCommand;snprintf(remoteReq,sizeof(remoteReq),"%s",req);snprintf(remoteSeen[remoteCursor++%8],25,"%s",req);}portEXIT_CRITICAL(&viewMux);if(duplicate||occupied)return;
 c.origin=Origin::Mqtt;c.expiresMs=until;if(!controlEnqueue(c)){portENTER_CRITICAL(&viewMux);remoteOk=false;snprintf(remoteText,sizeof(remoteText),"ERR controller queue busy");remoteReady.store(true);portEXIT_CRITICAL(&viewMux);}
}
bool publishRemoteReply(){if(!remoteReady.load())return false;char req[25],text[241];bool ok;portENTER_CRITICAL(&viewMux);snprintf(req,sizeof(req),"%s",remoteReq);snprintf(text,sizeof(text),"%s",remoteText);ok=remoteOk;portEXIT_CRITICAL(&viewMux);struct Sink{char *data;size_t used=0;bool write(const char*p,size_t n){if(used+n>=2048)return false;memcpy(data+used,p,n);used+=n;data[used]=0;return true;}} sink={publishBuffer};jsonstream::Writer<Sink> j(sink);j.raw("{\"v\":1,\"req\":").quoted(req).raw(",\"ok\":").boolean(ok).raw(",\"message\":").quoted(text).raw("}");char topic[64];snprintf(topic,sizeof(topic),"%s/analysis/device_reply",base);if(j.ok()&&esp_mqtt_client_enqueue(client,topic,publishBuffer,0,0,false,true)>=0){portENTER_CRITICAL(&viewMux);remoteReady.store(false);remotePendingId=0;portEXIT_CRITICAL(&viewMux);return true;}return false;}
void incoming(esp_mqtt_event_handle_t event){
  static char topic[96]={},payload[512]={};static size_t received=0,total=0;static bool retained=false;
  if(event->current_data_offset==0){
    received=total=0;
    if(!event->topic||event->topic_len<=0||event->topic_len>=static_cast<int>(sizeof(topic))||event->total_data_len<=0||event->total_data_len>=static_cast<int>(sizeof(payload)))return;
    memcpy(topic,event->topic,event->topic_len);topic[event->topic_len]=0;total=event->total_data_len;retained=event->retain;
  }
  if(!total||event->current_data_offset!=static_cast<int>(received)||event->data_len<0||received+event->data_len>total){total=0;return;}
  memcpy(payload+received,event->data,event->data_len);received+=event->data_len;if(received!=total)return;payload[total]=0;total=0;
  char analysisBase[64];snprintf(analysisBase,sizeof(analysisBase),"%s/analysis/",base);if(!strncmp(topic,analysisBase,strlen(analysisBase))){if(!retained&&!strcmp(topic+strlen(analysisBase),"device_command")){remoteCommand(payload);return;}if(!retained&&strstr(topic,"/photo"))photoAcceptAnalysis(payload);if(!retained&&strstr(topic,"/ack"))setReply(payload);if(!retained&&strstr(topic,"/heartbeat")){char boot[40]={};unsigned long long echo=0,receivedMs=0;int end=0;const int n=sscanf(payload,"{\"v\":1,\"boot\":\"%39[^\"]\",\"uptime_ms\":%llu,\"received_ms\":%llu}%n",boot,&echo,&receivedMs,&end);const auto id=eventLogView();const uint64_t localNow=monoMs();if(n==3&&!payload[end]&&!strcmp(boot,id.bootId)&&localNow>=echo&&localNow-echo<=15000&&receivedMs>=1704067200000ULL&&receivedMs<=4102444799999ULL){nasSeen.store(millis());controlNasTime(echo,receivedMs);}}return;}
  if(!strcmp(topic,"homeassistant/status")){if(!strcmp(payload,"online"))announce.store(true);return;}
  if(!strcmp(topic,replyTopic)){
    if(retained){setReply("ERR retained recovery response ignored");return;}
    ControlCommand c={};c.origin=Origin::Recovery;c.id=++nextCommand;c.expiresMs=monoMs()+2000;snprintf(c.text,sizeof(c.text),"%s",payload);if(!controlEnqueue(c))setReply("ERR recovery queue busy");return;
  }
  char sampleAck[64];snprintf(sampleAck,sizeof(sampleAck),"%s/samples/ack",base);if(!strcmp(topic,sampleAck)){sampleJournalReceipt(payload,retained);return;}
  const size_t baseLength=strlen(base);if(strncmp(topic,base,baseLength)||strncmp(topic+baseLength,"/command/",9))return;
  if(retained){portENTER_CRITICAL(&viewMux);++view.retainedCommandsRejected;portEXIT_CRITICAL(&viewMux);setReply("ERR retained command ignored");return;}
  const char *target=topic+baseLength+9;ControlCommand c={};c.origin=Origin::Mqtt;c.id=++nextCommand;c.expiresMs=monoMs()+2000;
  const bool binary=!strcmp(payload,"ON")||!strcmp(payload,"OFF");
  if(!strcmp(target,"light1")||!strcmp(target,"light2")){if(!binary){setReply("ERR ON|OFF required");return;}snprintf(c.text,sizeof(c.text),"%s %u",!strcmp(payload,"ON")?"on":"off",target[5]=='1'?1:2);}
  else if(!strcmp(target,"arm")){if(!binary){setReply("ERR ON|OFF required");return;}snprintf(c.text,sizeof(c.text),"%s",!strcmp(payload,"ON")?"arm":"disarm");}
  else if(!strcmp(target,"pwm1")||!strcmp(target,"pwm2")){if(!binary){setReply("ERR PWM ON|OFF required");return;}snprintf(c.text,sizeof(c.text),"pwmset %u enable %s",target[3]=='1'?1:2,!strcmp(payload,"ON")?"ON":"OFF");}
  else{
    const bool duty=!strcmp(target,"duty1")||!strcmp(target,"duty2"),frequency=!strcmp(target,"frequency1")||!strcmp(target,"frequency2");
    if(!duty&&!frequency)return;uint32_t value=0;for(const char*p=payload;*p;++p){if(*p<'0'||*p>'9'||value>20000){setReply("ERR PWM integer required");return;}value=value*10+(*p-'0');}
    if((duty&&(value<1||value>99))||(frequency&&(value<40||value>20000))){setReply("ERR PWM value out of range");return;}
    snprintf(c.text,sizeof(c.text),"pwmset %u %s %lu",target[strlen(target)-1]=='1'?1:2,duty?"duty":"frequency",static_cast<unsigned long>(value));
  }
  if(!controlEnqueue(c))setReply("ERR controller queue busy");
}
void callback(void *,esp_event_base_t,int32_t id,void *data){
  auto e=static_cast<esp_mqtt_event_handle_t>(data);
  uint32_t stack=uxTaskGetStackHighWaterMark(nullptr),heapAtEvent=ESP.getFreeHeap();portENTER_CRITICAL(&viewMux);if(!view.stackMinimum||stack<view.stackMinimum)view.stackMinimum=stack;portEXIT_CRITICAL(&viewMux);
  if(id==MQTT_EVENT_BEFORE_CONNECT){portENTER_CRITICAL(&viewMux);++view.connectionAttempts;portEXIT_CRITICAL(&viewMux);}
  else if(id==MQTT_EVENT_CONNECTED){connected.store(true);waitFor("none");announce.store(true);connectionEpoch.fetch_add(1);portENTER_CRITICAL(&viewMux);view.heapAfterConnect=heapAtEvent;view.lastError=view.errorType=view.transportError=view.socketError=0;portEXIT_CRITICAL(&viewMux);char sampleTopic[64];snprintf(sampleTopic,sizeof(sampleTopic),"%s/samples/ack",base);esp_mqtt_client_subscribe(client,sampleTopic,1);esp_mqtt_client_subscribe(client,commandsTopic,1);esp_mqtt_client_subscribe(client,"homeassistant/status",1);esp_mqtt_client_subscribe(client,replyTopic,1);char t[64];snprintf(t,sizeof(t),"%s/analysis/#",base);esp_mqtt_client_subscribe(client,t,0);snprintf(t,sizeof(t),"%s/analysis/device_command",base);esp_mqtt_client_subscribe(client,t,1);setReply("OK MQTT connected; publishing discovery");}
  else if(id==MQTT_EVENT_DISCONNECTED)connected.store(false);
  else if(id==MQTT_EVENT_DATA)incoming(e);
  else if(id==MQTT_EVENT_PUBLISHED){lastAck.store(e->msg_id);}
  else if(id==MQTT_EVENT_ERROR){int error=-1,type=-1,tls=0,socket=0;if(e->error_handle){auto h=e->error_handle;type=h->error_type;tls=h->esp_tls_last_esp_err;socket=rawmqtt::error()?rawmqtt::error():h->esp_transport_sock_errno;if(type==MQTT_ERROR_TYPE_CONNECTION_REFUSED)error=h->connect_return_code;else if(socket)error=socket;else if(tls)error=tls;else if(h->esp_tls_stack_err)error=h->esp_tls_stack_err;}if(!error)error=-1;portENTER_CRITICAL(&viewMux);view.lastError=error;view.errorType=type;view.transportError=tls;view.socketError=socket;portEXIT_CRITICAL(&viewMux);char reply[96];snprintf(reply,sizeof(reply),"ERR MQTT code=%d type=%d transport=%d errno=%d",error,type,tls,socket);setReply(reply);}
}
int enqueue(const char *topic,const char *payload,mqttflow::Kind kind,bool retain,uint64_t ordinal=0){
 if(!client||!connected.load())return -1;uint32_t now=millis();int outbox=esp_mqtt_client_get_outbox_size(client);
 if(!flow.ready(now,ESP.getFreeHeap(),ESP.getMaxAllocHeap(),outbox)){flow.nextAt=now+500;portENTER_CRITICAL(&viewMux);++view.memoryDeferrals;portEXIT_CRITICAL(&viewMux);return -1;}
 int id=esp_mqtt_client_enqueue(client,topic,payload,0,1,retain,true);
 if(id>0){flow.accepted(id,kind,ordinal,now,connectionEpoch.load());portENTER_CRITICAL(&viewMux);view.inflightId=id;portEXIT_CRITICAL(&viewMux);}
 else{flow.failed(now);portENTER_CRITICAL(&viewMux);++view.publishFailures;portEXIT_CRITICAL(&viewMux);}
 return id;
}
// Keep the packet buffer in a separate frame; snapshots and packets need not overlap on the loop stack.
__attribute__((noinline)) int publishRecord(uint32_t now){
 lastAttempt=now;uint64_t ordinal=0;if(!eventPeekJson(publishBuffer,sizeof(publishBuffer),ordinal))return 0;
 char topic[64];snprintf(topic,sizeof(topic),"%s/%s",base,strstr(publishBuffer,R"JSON("kind":"environment")JSON")?"telemetry":"events");
 return enqueue(topic,publishBuffer,mqttflow::Kind::Record,false,ordinal);
}

bool discover(unsigned index){
  if(index>=11){const unsigned ch=index-10;char topic[96];snprintf(topic,sizeof(topic),"homeassistant/sensor/incubator_%s/photo%u_raw/config",suffix,ch);const int n=snprintf(publishBuffer,sizeof(publishBuffer),R"JSON({"name":"光源%u ADC","unique_id":"incubator_%s_photo%u_raw","state_topic":"%s","value_template":"{{ value_json.photo.channels[%u].raw }}","state_class":"measurement","availability_mode":"all","availability":[{"topic":"%s","payload_available":"online","payload_not_available":"offline"},{"topic":"%s","value_template":"{{ 'online' if value_json.photo.compiled and value_json.photo.channels[%u].valid else 'offline' }}","payload_available":"online","payload_not_available":"offline"}],"device":{"identifiers":["incubator_%s"],"name":"光照培养箱","sw_version":"Connected-2.2.3"}})JSON",ch,suffix,ch,stateTopic,ch-1,availability,stateTopic,ch-1,suffix);return n>0&&n<int(sizeof(publishBuffer))&&enqueue(topic,publishBuffer,mqttflow::Kind::Discovery,true)>0;}
  const char *components[]={"light","light","sensor","sensor","switch","switch","number","number","switch","number","number"};
  const char *names[]={"光源1","光源2","温度","湿度","运行许可","光源1 PWM","光源1 占空比","光源1 PWM频率","光源2 PWM","光源2 占空比","光源2 PWM频率"};
  const char *objects[]={"light1","light2","temperature","humidity","arm","pwm1","duty1","frequency1","pwm2","duty2","frequency2"};
  char topic[96];auto &options=discoveryOptions;auto &payload=publishBuffer;options[0]=0;
  snprintf(topic,sizeof(topic),"homeassistant/%s/incubator_%s/%s/config",components[index],suffix,objects[index]);
  if(index<2||index==4||index==5||index==8){
    const char *field=index==4?"armed":index==0?"l1":index==1?"l2":objects[index];
    snprintf(options,sizeof(options),R"JSON("command_topic":"%s/command/%s","state_topic":"%s","%s":"{{ 'ON' if value_json.%s else 'OFF' }}","payload_on":"ON","payload_off":"OFF","qos":1,"retain":false,"availability_topic":"%s","payload_available":"online","payload_not_available":"offline")JSON",base,objects[index],stateTopic,index<2?"state_value_template":"value_template",field,availability);
  }else if(index==2||index==3){
    snprintf(options,sizeof(options),R"JSON("state_topic":"%s","value_template":"{{ value_json.%s }}","device_class":"%s","state_class":"measurement","unit_of_measurement":"%s","availability_mode":"all","availability":[{"topic":"%s","payload_available":"online","payload_not_available":"offline"},{"topic":"%s","value_template":"{{ 'online' if value_json.sensor_valid else 'offline' }}","payload_available":"online","payload_not_available":"offline"}])JSON",stateTopic,objects[index],objects[index],index==2?"°C":"%",availability,stateTopic);
  }
  if(index==6||index==7||index==9||index==10){const bool duty=index==6||index==9;
    snprintf(options,sizeof(options),R"JSON("min":%u,"max":%u,"step":1,"mode":"%s","unit_of_measurement":"%s","command_topic":"%s/command/%s","command_template":"{{ value | int }}","state_topic":"%s","value_template":"{{ value_json.%s }}","qos":1,"retain":false,"availability_topic":"%s","payload_available":"online","payload_not_available":"offline")JSON",duty?1:40,duty?99:20000,duty?"slider":"box",duty?"%":"Hz",base,objects[index],stateTopic,objects[index],availability);
  }
  int n=snprintf(payload,sizeof(payload),R"JSON({"name":"%s","unique_id":"incubator_%s_%s",%s,"device":{"identifiers":["incubator_%s"],"name":"光照培养箱","manufacturer":"DIY","model":"ESP32-C3 SuperMini","sw_version":"Connected-2.2.3"}})JSON",names[index],suffix,objects[index],options,suffix);
  return n>0&&static_cast<size_t>(n)<sizeof(payload)&&enqueue(topic,payload,mqttflow::Kind::Discovery,true)>0;
}
bool publishState(const ControlSnapshot&s){struct Sink{char*data;size_t used=0;bool write(const char*p,size_t n){if(used+n>=2048)return false;memcpy(data+used,p,n);used+=n;data[used]=0;return true;}} sink={publishBuffer};char t[20],h[20];decimal::sample(t,sizeof(t),s.temperature,s.sensorValid);decimal::sample(h,sizeof(h),s.humidity,s.sensorValid);jsonstream::Writer<Sink> j(sink);daqjson::emit(j,s,photoView(),eventLogView(),t,h,monoMs());if(j.ok()){const size_t baseSize=sink.used;sink.data[--sink.used]=0;jsonstream::Writer<Sink> u(sink);const auto n=networkView();const auto b=bleView();const auto h=healthView();const auto sj=sampleJournalView();u.raw(",\"sample_journal\":{\"ready\":").boolean(sj.ready).raw(",\"pending\":").number(sj.pending).raw(",\"capacity\":").number(sj.capacity).raw(",\"covered\":").number(sj.covered).raw(",\"thinned\":").number(sj.thinned).raw(",\"interval_ms\":").number(sj.intervalMs).raw(",\"failures\":").number(sj.failures).raw("}").raw(",\"ui\":{\"logic\":").boolean(s.logicPassed).raw(",\"nvs\":").boolean(s.storageReady).raw(",\"heap\":").number(ESP.getFreeHeap()).raw(",\"min\":").number(ESP.getMinFreeHeap()).raw(",\"largest\":").number(ESP.getMaxAllocHeap()).raw(",\"ssid\":").quoted(n.ssid).raw(",\"ip\":").quoted(n.ip).raw(",\"ble\":{\"ready\":").boolean(b.ready).raw(",\"connected\":").boolean(b.connected).raw(",\"secure\":").boolean(b.secure).raw(",\"advertisingPaused\":").boolean(b.advertisingPaused).raw(",\"name\":").quoted(b.name).raw("},\"health\":{\"active\":").number(h.active).raw(",\"latched\":").number(h.latched).raw(",\"lastError\":").quoted(h.lastCommandError).raw("},\"rec\":{\"waiting\":").boolean(s.resumeWaiting).raw(",\"nasWaiting\":").boolean(s.nasWaiting).raw(",\"source\":").quoted(s.recoverySource).raw(",\"outage\":").number(s.estimatedOutageMs).raw(",\"startup\":").number(s.startupWaitMs).raw(",\"sources\":[").quoted(s.channelRecoverySource[0]).raw(",").quoted(s.channelRecoverySource[1]).raw("],\"gaps\":[").number(s.channelGapMs[0]).raw(",").number(s.channelGapMs[1]).raw("],\"outages\":[").number(s.channelOutageMs[0]).raw(",").number(s.channelOutageMs[1]).raw("],\"startups\":[").number(s.channelStartupMs[0]).raw(",").number(s.channelStartupMs[1]).raw("]},\"dailyWaiting\":[").boolean(s.channelDailyWaiting[0]).raw(",").boolean(s.channelDailyWaiting[1]).raw("]}}");if(!u.ok()){sink.used=baseSize;sink.data[baseSize-1]='}';sink.data[baseSize]=0;}}if(!j.ok()||esp_mqtt_client_get_outbox_size(client)>768)return false;// QoS0 requires store=true with ESP-MQTT enqueue; otherwise SDK ignores it.
return esp_mqtt_client_enqueue(client,stateTopic,publishBuffer,0,0,true,true)>=0;}
__attribute__((noinline)) bool publishProgress(const ControlSnapshot&s){
 auto log=eventLogView();const uint8_t mask=(s.cycleRunning[0]?1:0)|(s.cycleRunning[1]?2:0);auto&payload=publishBuffer;
 const int n=snprintf(payload,sizeof(payload),R"JSON({"v":1,"boot":"%s","cfg":%lu,"seq":%llu,"epoch_ms":%llu,"trusted":%s,"waiting":%s,"armed":%s,"running":%u,"elapsed1_ms":"%llu","elapsed2_ms":"%llu","recovery_source":"%s","recovery_req":"%s","estimated_outage_ms":%llu,"startup_wait_ms":%llu,"reset_reason":"%s"})JSON",log.bootId,static_cast<unsigned long>(s.configRevision),s.uptimeMs,s.epochMs,networkClock(s.clockSource)?"true":"false",s.resumeWaiting?"true":"false",s.armed?"true":"false",mask,s.progress[0].elapsedMs,s.progress[1].elapsedMs,s.recoverySource,s.recoveryReq,s.estimatedOutageMs,s.startupWaitMs,log.resetReason);
 return n>0&&static_cast<size_t>(n)<sizeof(payload)&&enqueue(progressTopic,payload,mqttflow::Kind::Progress,false)>0;
}
bool requestRecovery(const ControlSnapshot&s){auto log=eventLogView();auto&payload=publishBuffer;
 const int n=snprintf(payload,sizeof(payload),R"JSON({"v":1,"req":"%s","boot":"%s","previous":"%s","cfg":%lu,"min_epoch_ms":%llu})JSON",s.recoveryReq,log.bootId,s.previousBoot,static_cast<unsigned long>(s.configRevision),s.checkpointEpochMs);
 return n>0&&static_cast<size_t>(n)<sizeof(payload)&&enqueue(requestTopic,payload,mqttflow::Kind::RecoveryRequest,false)>0;
}
bool publishConfig(const ControlSnapshot&s){struct Sink{char*data;size_t used=0;bool write(const char*p,size_t n){if(used+n>=2048)return false;memcpy(data+used,p,n);used+=n;data[used]=0;return true;}} sink={publishBuffer};jsonstream::Writer<Sink> j(sink);j.raw("{\"v\":1,\"cfg\":").number(s.configRevision).raw(",\"channels\":[");for(unsigned ch=0;ch<2;ch++){if(ch)j.raw(",");const auto&c=s.config[ch];j.raw("{\"mode\":").number(static_cast<unsigned>(c.mode)).raw(",\"repeats\":").number(c.repeats).raw(",\"daily\":[");for(unsigned i=0;i<c.dailyCount;i++){if(i)j.raw(",");j.raw("[").number(c.daily[i].on).raw(",").number(c.daily[i].off).raw("]");}j.raw("],\"steps\":[");for(unsigned i=0;i<c.stepCount;i++){if(i)j.raw(",");j.raw("[").number(c.steps[i].onMs).raw(",").number(c.steps[i].offMs).raw("]");}j.raw("]").raw(",\"recovery\":{\"enabled\":").boolean(s.channelRecovery[ch].enabled).raw(",\"manualEnabled\":").boolean(s.channelManualRecovery[ch]).raw(",\"dailyEnabled\":").boolean(s.channelDailyRecovery[ch]).raw(",\"nasEnabled\":").boolean(s.channelNasRecovery[ch]).raw(",\"countOutage\":").boolean(s.channelRecovery[ch].countOutage).raw(",\"restartAfterSeconds\":").number(s.channelRecovery[ch].restartAfterSeconds).raw(",\"checkpointSeconds\":").number(s.channelRecovery[ch].checkpointSeconds).raw("}").raw(",\"pwm\":").boolean(c.pwm).raw(",\"frequency\":").number(c.frequency).raw(",\"duty\":").number(c.duty).raw("}");}j.raw("]}");char topic[64];snprintf(topic,sizeof(topic),"%s/config",base);return j.ok()&&enqueue(topic,publishBuffer,mqttflow::Kind::Config,true)>0;}
void runProbe(){
  if(mqttView().enabled||controlSnapshot().armed){if(Serial)Serial.println("ERR mqtt probe requires locked and MQTT disabled");return;}
  if(WiFi.status()!=WL_CONNECTED){if(Serial)Serial.println("ERR mqtt probe WiFi disconnected");return;}
  NetworkClient socket;IPAddress address;uint32_t started=millis();int ok=address.fromString(active.host)?socket.connect(address,active.port,3000):socket.connect(active.host,active.port,3000);uint32_t elapsed=millis()-started;socket.stop();
  if(Serial)Serial.printf("MQTT_PROBE host=%s port=%u connected=%u elapsed_ms=%lu local_ip=%s gateway=%s rssi=%ld heap_free=%lu heap_largest=%lu\n",active.host,active.port,ok?1:0,static_cast<unsigned long>(elapsed),WiFi.localIP().toString().c_str(),WiFi.gatewayIP().toString().c_str(),static_cast<long>(WiFi.RSSI()),static_cast<unsigned long>(ESP.getFreeHeap()),static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}
void releaseClient(const char*reason){
 portENTER_CRITICAL(&viewMux);snprintf(lifecycle.lastStop,sizeof(lifecycle.lastStop),"%s",reason);portEXIT_CRITICAL(&viewMux);
 if(client){if(!strcmp(reason,"disabled")&&connected.load()&&ESP.getFreeHeap()>12288)esp_mqtt_client_publish(client,availability,"offline",0,0,1);rawmqtt::abort();esp_mqtt_client_stop(client);esp_mqtt_client_destroy(client);client=nullptr;}
 connected.store(false);disconnectedAt=0;lastAck.store(0);flow={};routine={};flow.nextAt=millis();announced=false;discoveryIndex=0;portENTER_CRITICAL(&viewMux);view.outboxBytes=view.inflightId=0;portEXIT_CRITICAL(&viewMux);
}
__attribute__((noinline)) void startClient(){
 const auto net=networkView();if(!net.connected||!net.ip[0]){clientRetryAt=millis()+1000;waitFor("wifi_ip");setReply("WAIT WiFi IP before MQTT startup");return;}networkGeneration=net.generation;
 if(networkHttpBusy()){clientRetryAt=millis()+1000;waitFor("http_response");return;}
 uint32_t free=ESP.getFreeHeap(),largest=ESP.getMaxAllocHeap();const char *reason=netbudget::startWait(free,largest);if(strcmp(reason,"none")){waitFor(reason,netbudget::MQTT_START_HEAP,netbudget::MQTT_TASK_STACK);clientRetryAt=millis()+30000;setReply("WAIT MQTT startup memory; client not started");portENTER_CRITICAL(&viewMux);++view.memoryDeferrals;portEXIT_CRITICAL(&viewMux);return;}
 esp_mqtt_client_config_t c={};c.broker.address.hostname=active.host;c.broker.address.port=active.port;c.broker.address.transport=MQTT_TRANSPORT_OVER_TCP;
 c.credentials.username=active.username;c.credentials.authentication.password=active.password;c.credentials.client_id=clientId;
 c.session.protocol_ver=MQTT_PROTOCOL_V_3_1_1;c.session.keepalive=45;c.session.last_will.topic=availability;c.session.last_will.msg="offline";c.session.last_will.qos=1;c.session.last_will.retain=1;
 c.network.timeout_ms=4000;c.network.reconnect_timeout_ms=15000;c.network.transport=rawmqtt::create();c.task.stack_size=netbudget::MQTT_TASK_STACK;c.task.priority=1;c.buffer.size=512;c.buffer.out_size=1024;c.outbox.limit=4096;
 auto transport=c.network.transport;portENTER_CRITICAL(&viewMux);view.heapBefore=free;portEXIT_CRITICAL(&viewMux);
 client=transport?esp_mqtt_client_init(&c):nullptr;
 if(client){portENTER_CRITICAL(&viewMux);++lifecycle.creations;portEXIT_CRITICAL(&viewMux);}
 if(!client&&transport&&rawmqtt::live(transport))esp_transport_destroy(transport);
 uint32_t heapAfterInit=ESP.getFreeHeap();portENTER_CRITICAL(&viewMux);view.heapAfterInit=heapAfterInit;portEXIT_CRITICAL(&viewMux);
 esp_err_t result=client?esp_mqtt_client_register_event(client,static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),callback,nullptr):ESP_ERR_NO_MEM;
 if(result==ESP_OK){const char *taskReason=netbudget::taskWait(ESP.getFreeHeap(),ESP.getMaxAllocHeap());if(strcmp(taskReason,"none")){releaseClient("task_budget");clientRetryAt=millis()+30000;waitFor(taskReason,netbudget::MQTT_INIT_HEAP,netbudget::MQTT_TASK_STACK);portENTER_CRITICAL(&viewMux);++view.memoryDeferrals;portEXIT_CRITICAL(&viewMux);setReply("WAIT MQTT task memory; initialization released");return;}waitFor("connecting");result=esp_mqtt_client_start(client);}
 uint32_t heapAfterStart=ESP.getFreeHeap();portENTER_CRITICAL(&viewMux);view.heapAfterStart=heapAfterStart;portEXIT_CRITICAL(&viewMux);
 if(result!=ESP_OK){releaseClient("init_failed");portENTER_CRITICAL(&viewMux);view.lastError=result;view.errorType=-1;portEXIT_CRITICAL(&viewMux);clientRetryAt=millis()+30000;waitFor("init_failed");setReply("ERR MQTT initialization failed; retry after30s");}
}
void applyConfiguration(){bool enabled;portENTER_CRITICAL(&configMux);enabled=stored.enabled;portEXIT_CRITICAL(&configMux);releaseClient(enabled?"config_apply":"disabled");portENTER_CRITICAL(&configMux);active=stored;portEXIT_CRITICAL(&configMux);clientRetryAt=millis();if(active.enabled)startClient();else waitFor("disabled");}

}
bool mqttStart(){
 analysisQueue=xQueueCreate(2,sizeof(AnalysisCommand));if(!analysisQueue)return false;

  snprintf(suffix,sizeof(suffix),"%06lx",static_cast<unsigned long>(deviceIdentityValue()));snprintf(base,sizeof(base),"incubator/%s",suffix);snprintf(stateTopic,sizeof(stateTopic),"%s/state",base);snprintf(availability,sizeof(availability),"%s/availability",base);snprintf(commandsTopic,sizeof(commandsTopic),"%s/command/#",base);snprintf(clientId,sizeof(clientId),"incubator-%s",suffix);snprintf(progressTopic,sizeof(progressTopic),"%s/progress",base);snprintf(requestTopic,sizeof(requestTopic),"%s/recovery/request",base);snprintf(replyTopic,sizeof(replyTopic),"%s/recovery/reply",base);
  stored.schema=SCHEMA;stored.port=1883;snprintf(stored.host,sizeof(stored.host),"nas.example.local");snprintf(stored.username,sizeof(stored.username),"incubator_device");
  Preferences p;if(p.begin("inc-mqtt",true)){Config saved={};if(p.getBytesLength("config")==sizeof(saved)&&p.getBytes("config",&saved,sizeof(saved))==sizeof(saved)&&valid(saved))stored=saved;p.end();}
  view.enabled=stored.enabled;view.port=stored.port;view.credentialsSaved=stored.password[0];snprintf(view.host,sizeof(view.host),"%s",stored.host);snprintf(view.username,sizeof(view.username),"%s",stored.username);changed.store(true);return true;
}
MqttLifecycle mqttLifecycleView(){MqttLifecycle result;portENTER_CRITICAL(&viewMux);result=lifecycle;portEXIT_CRITICAL(&viewMux);return result;}
MqttView mqttView(){MqttView v;portENTER_CRITICAL(&viewMux);v=view;portEXIT_CRITICAL(&viewMux);v.connected=connected.load();v.nasAlive=mqttNasAlive();v.currentHeap=ESP.getFreeHeap();v.currentLargest=ESP.getMaxAllocHeap();if(!v.connected&&v.enabled){const uint32_t now=millis();v.retryMs=static_cast<int32_t>(clientRetryAt-now)>0?clientRetryAt-now:0;}return v;}
bool mqttConfigure(bool enabled,const char *host,uint16_t port,const char *username,const char *password,char *error,size_t size){
  Config next;portENTER_CRITICAL(&configMux);next=stored;portEXIT_CRITICAL(&configMux);
  if(!host||!username||!password||strlen(host)>=sizeof(next.host)||strlen(username)>=sizeof(next.username)||strlen(password)>=sizeof(next.password)){snprintf(error,size,"ERR MQTT field length");return false;}
  next.enabled=enabled;next.port=port;snprintf(next.host,sizeof(next.host),"%s",host);snprintf(next.username,sizeof(next.username),"%s",username);if(*password)snprintf(next.password,sizeof(next.password),"%s",password);
  if(!valid(next)){snprintf(error,size,"ERR broker host, port, username or password");return false;}
  Preferences p;bool ok=p.begin("inc-mqtt",false)&&p.putBytes("config",&next,sizeof(next))==sizeof(next);p.end();if(!ok){snprintf(error,size,"ERR MQTT config write failed");return false;}
  portENTER_CRITICAL(&configMux);stored=next;portEXIT_CRITICAL(&configMux);
  portENTER_CRITICAL(&viewMux);view.enabled=next.enabled;view.port=next.port;view.credentialsSaved=next.password[0];snprintf(view.host,sizeof(view.host),"%s",next.host);snprintf(view.username,sizeof(view.username),"%s",next.username);portEXIT_CRITICAL(&viewMux);changed.store(true);return true;
}
void mqttAnswer(const ControlReply &r){portENTER_CRITICAL(&viewMux);if(remotePendingId&&r.id==remotePendingId){remoteOk=r.ok;snprintf(remoteText,sizeof(remoteText),"%.240s",r.text);remoteReady.store(true);}portEXIT_CRITICAL(&viewMux);setReply(r.text);if(Serial)Serial.printf("MQTT command %s\n",r.text);}
void mqttService(){
 if(changed.exchange(false))applyConfiguration();if(probeRequested.exchange(false))runProbe();const uint32_t now=millis();
 const auto net=networkView();if(client&&(!net.connected||net.generation!=networkGeneration)){releaseClient("network_changed");clientRetryAt=now+1000;waitFor("network_changed");}
 if(active.enabled&&!client&&static_cast<int32_t>(now-clientRetryAt)>=0)startClient();
 if(client&&!connected.load()){if(!disconnectedAt)disconnectedAt=now;if(uint32_t(now-disconnectedAt)>30000){releaseClient("connection_timeout");clientRetryAt=now+45000;waitFor("connection_cooldown");disconnectedAt=0;}}else disconnectedAt=0;
 if(client){int bytes=esp_mqtt_client_get_outbox_size(client);portENTER_CRITICAL(&viewMux);view.outboxBytes=bytes>0?bytes:0;portEXIT_CRITICAL(&viewMux);}
 const auto s=controlSnapshot();
 const bool memoryLow=client&&(netbudget::low(ESP.getFreeHeap(),ESP.getMaxAllocHeap())),ackExpired=client&&flow.expired(now);
 if(memoryLow||ackExpired){portENTER_CRITICAL(&viewMux);if(memoryLow)++lifecycle.memoryStops;if(ackExpired)++lifecycle.ackTimeoutStops;portEXIT_CRITICAL(&viewMux);releaseClient(memoryLow?"low_memory":"puback_timeout");clientRetryAt=now+30000;waitFor(memoryLow?"memory_cooldown":"ack_cooldown",netbudget::MQTT_START_HEAP,netbudget::MQTT_TASK_STACK);setReply(memoryLow?"WAIT MQTT low memory; released client, retry after30s":"WAIT MQTT PUBACK timeout; released client, retry after30s");portENTER_CRITICAL(&viewMux);++view.memoryDeferrals;portEXIT_CRITICAL(&viewMux);return;}
 int ack=lastAck.exchange(0);if(flow.acknowledged(ack)){auto kind=flow.kind;uint64_t ordinal=flow.ordinal;bool sameEpoch=flow.epoch==connectionEpoch.load();flow.succeeded(now);portENTER_CRITICAL(&viewMux);view.inflightId=0;portEXIT_CRITICAL(&viewMux);if(kind==mqttflow::Kind::Record&&ordinal)eventAck(ordinal);else if(kind==mqttflow::Kind::Discovery&&sameEpoch)++discoveryIndex;else if(kind==mqttflow::Kind::Availability&&sameEpoch)announced=true;}
 if(!connected.load())return;if(announce.exchange(false)){discoveryIndex=0;announced=false;lastState=0;lastConfig=0;lastConfigState=UINT64_MAX;lastMask=255;}
 if(remoteReady.load()&&ESP.getFreeHeap()>=16384&&ESP.getMaxAllocHeap()>=4096){publishRemoteReply();}
 if(announced&&static_cast<uint32_t>(now-lastState)>=(photoView().automatic.active?5000:2000)&&ESP.getFreeHeap()>=16384&&ESP.getMaxAllocHeap()>=4096){if(publishState(s))lastState=now;}
 if(flow.id||static_cast<int32_t>(now-flow.nextAt)<0)return;
 if(ESP.getFreeHeap()<8192||ESP.getMaxAllocHeap()<2048){flow.nextAt=now+1000;portENTER_CRITICAL(&viewMux);++view.memoryDeferrals;portEXIT_CRITICAL(&viewMux);return;}
 if(s.nasWaiting){if(!lastRecoveryRequest||static_cast<uint32_t>(now-lastRecoveryRequest)>=2000){if(requestRecovery(s))lastRecoveryRequest=now;}return;}
 if(!announced){if(discoveryIndex<DISCOVERY_COUNT){discover(discoveryIndex);return;}enqueue(availability,"online",mqttflow::Kind::Availability,true);return;}
 const uint64_t configState=uint64_t(s.config[0].frequency)|(uint64_t(s.config[0].duty)<<15)|(uint64_t(s.config[0].pwm)<<22)|(uint64_t(s.config[1].frequency)<<23)|(uint64_t(s.config[1].duty)<<38)|(uint64_t(s.config[1].pwm)<<45);if(s.configRevision!=lastConfig||configState!=lastConfigState){if(publishConfig(s)){lastConfig=s.configRevision;lastConfigState=configState;}return;}AnalysisCommand command={};if(analysisQueue&&xQueuePeek(analysisQueue,&command,0)==pdTRUE){char topic[64];snprintf(topic,sizeof(topic),"%s/analysis/command",base);if(enqueue(topic,command.json,mqttflow::Kind::AnalysisCommand,false)>0)xQueueReceive(analysisQueue,&command,0);return;}
 uint32_t current=s.output[0]|(s.output[1]<<1)|(s.armed<<2)|(s.sensorValid<<3);current=current*31+s.config[0].frequency+s.config[0].duty*20001UL+s.config[0].pwm;current=current*31+s.config[1].frequency+s.config[1].duty*20001UL+s.config[1].pwm;
 if(s.sensorValid)current=current*31+static_cast<uint32_t>((s.temperature+100)*10)+static_cast<uint32_t>(s.humidity*10);
 const uint8_t mask=(s.cycleRunning[0]?1:0)|(s.cycleRunning[1]?2:0);const bool progressDue=mask!=lastMask||(s.nasRecoveryEnabled&&mask&&static_cast<uint32_t>(now-lastProgress)>=2000);
 const bool stateDue=false;
 const auto logs=eventLogView();bool recordDue=logs.ready&&logs.pending&&static_cast<uint32_t>(now-lastAttempt)>=500;
 static uint32_t lastSampleSend=0;bool sampleDue=mqttNasAlive()&&sampleJournalView().pending&&uint32_t(now-lastSampleSend)>=1000;
 for(uint8_t attempt=0;attempt<3;++attempt){
  const auto kind=routine.choose(progressDue,stateDue,recordDue,sampleDue);
  if(kind==mqttflow::Kind::Progress){if(publishProgress(s)){lastProgress=now;lastMask=mask;routine.accepted(kind);}return;}
  if(kind==mqttflow::Kind::State){if(publishState(s)){lastState=now;signature=current;routine.accepted(kind);}return;}
  if(kind==mqttflow::Kind::Sample){if(sampleJournalJson(publishBuffer,sizeof(publishBuffer))){char topic[64];snprintf(topic,sizeof(topic),"%s/samples",base);if(enqueue(topic,publishBuffer,mqttflow::Kind::Sample,false)>0){lastSampleSend=now;routine.accepted(kind);}return;}sampleDue=false;continue;}
  if(kind==mqttflow::Kind::Record){const int id=publishRecord(now);if(id>0){routine.accepted(kind);return;}if(id<0)return;recordDue=false;continue;}
  return;
 }
}
void mqttRequestProbe(){probeRequested.store(true);}
bool mqttNasAlive(){const uint32_t last=nasSeen.load();return connected.load()&&last&&uint32_t(millis()-last)<30000;}
bool mqttAnalysisCommand(const char*json){if(!analysisQueue||!json||strlen(json)>=256||!mqttNasAlive())return false;AnalysisCommand c={};snprintf(c.json,sizeof(c.json),"%s",json);return xQueueSend(analysisQueue,&c,0)==pdTRUE;}
