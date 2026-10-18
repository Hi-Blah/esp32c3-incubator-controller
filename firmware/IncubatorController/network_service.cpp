#include "device_identity.h"
#include "control_service.h"
#include "mqtt_service.h"
#include "event_log.h"
#include "web_ui.h"
#include "web_entry.h"
#include "entry_state_json.h"
#include "http_response_policy.h"
#include "web_ui_gzip.h"
#include "http_transfer.h"
#include <lwip/sockets.h>
#include <errno.h>
#include "format_decimal.h"
#include "web_state_json.h"
#include "http_authority.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>

namespace {
constexpr uint8_t MAX_WIFI = 8;
constexpr uint32_t LIST_SCHEMA = 0xC3062101UL;
struct Credentials { char ssid[33], password[64]; };
struct WifiList { uint32_t schema; uint8_t count; Credentials items[MAX_WIFI]; } list = {};
WebServer server(80); Preferences prefs;
// The HTTP task is single threaded; both state endpoints reuse one snapshot.
ControlSnapshot httpSnapshot={};
QueueHandle_t replies = nullptr, actions = nullptr;
portMUX_TYPE viewMux = portMUX_INITIALIZER_UNLOCKED;
NetworkView sharedView = {};
std::atomic<bool> synced(false),everSynced(false);
std::atomic<uint32_t> disconnectReason(0);uint32_t generation=0,httpRestarts=0;
bool prefsReady=false,saved=false,apActive=false,wasConnected=false,waiting=false;
bool radioSleep=false;
bool manualAp=false;uint32_t manualApAt=0;
String password,apName,csrf;
uint8_t activeIndex=0;char preferredSsid[33]={};
uint32_t attemptAt=0,beginAt=0,connectedAt=0,ntpAt=0,nextId=0;
TaskHandle_t httpTask=nullptr;
uint32_t stateRequests=0,commandTimeouts=0,responseWriteFailures=0,homeRequests=0,homeFailures=0;
uint32_t homeLastMs=0,homeLastSent=0;int httpLastErrno=0;httptransfer::Failure httpLastFailure=httptransfer::Failure::None;
uint64_t monoMs(){return static_cast<uint64_t>(esp_timer_get_time())/1000ULL;}
bool due(uint32_t now,uint32_t deadline){return static_cast<int32_t>(now-deadline)>=0;}
String quoted(const String &s){String r="\"";const char *hex="0123456789abcdef";for(size_t i=0;i<s.length();++i){uint8_t c=s[i];if(c=='"'||c=='\\'){r+='\\';r+=char(c);}else if(c<32){r+="\\u00";r+=hex[c>>4];r+=hex[c&15];}else r+=char(c);}return r+'"';}
String randomText(size_t n){const char a[]="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";String s;for(size_t i=0;i<n;++i)s+=a[esp_random()%(sizeof(a)-1)];return s;}
bool validCredential(const Credentials &c){
  if(!memchr(c.ssid,0,sizeof(c.ssid))||!memchr(c.password,0,sizeof(c.password)))return false;
  if(!strlen(c.ssid)||strlen(c.password)<8)return false;
  for(const char *p=c.ssid;*p;++p)if(static_cast<uint8_t>(*p)<32||*p==127)return false;
  for(const char *p=c.password;*p;++p)if(static_cast<uint8_t>(*p)<32||*p==127)return false;return true;
}
bool validList(const WifiList &l){if(l.schema!=LIST_SCHEMA||l.count>MAX_WIFI)return false;for(uint8_t i=0;i<l.count;++i){if(!validCredential(l.items[i]))return false;for(uint8_t j=0;j<i;++j)if(!strcmp(l.items[i].ssid,l.items[j].ssid))return false;}return true;}
void publish(){NetworkView n={};n.httpStackMinimum=httpTask?uxTaskGetStackHighWaterMark(httpTask):0;n.stateRequests=stateRequests;n.commandTimeouts=commandTimeouts;n.responseWriteFailures=responseWriteFailures;n.homeRequests=homeRequests;n.homeFailures=homeFailures;n.homeLastMs=homeLastMs;n.homeLastSent=homeLastSent;n.httpLastFailure=static_cast<uint8_t>(httpLastFailure);n.httpLastErrno=httpLastErrno;n.generation=generation;n.disconnectReason=disconnectReason.load();n.httpRestarts=httpRestarts;n.ready=true;n.connected=WiFi.status()==WL_CONNECTED;n.apActive=apActive;n.configured=list.count;n.credentialsSaved=saved;n.rssi=n.connected?WiFi.RSSI():0;
  snprintf(n.ip,sizeof(n.ip),"%s",n.connected?WiFi.localIP().toString().c_str():"");snprintf(n.ssid,sizeof(n.ssid),"%s",n.connected?WiFi.SSID().c_str():"");
  snprintf(n.apIp,sizeof(n.apIp),"%s",apActive?WiFi.softAPIP().toString().c_str():"");snprintf(n.apName,sizeof(n.apName),"%s",apName.c_str());
  portENTER_CRITICAL(&viewMux);sharedView=n;portEXIT_CRITICAL(&viewMux);
}
void info(bool showSecrets=false){if(!Serial)return;Serial.printf("WEB username=admin password=%s\n",showSecrets?password.c_str():"HIDDEN (USB wifi key)");if(apActive)Serial.printf("AP name=%s password=%s url=http://%s/\n",apName.c_str(),showSecrets?password.c_str():"HIDDEN",WiFi.softAPIP().toString().c_str());
  if(WiFi.status()==WL_CONNECTED)Serial.printf("WIFI SSID=%s url=http://%s/\n",WiFi.SSID().c_str(),WiFi.localIP().toString().c_str());Serial.printf("WIFI saved_profiles=%u nvs=%u\n",list.count,saved);Serial.printf("NET_DIAG rssi_dbm=%ld heap_free=%lu heap_min=%lu heap_largest=%lu ap_clients=%u manual_ap=%u generation=%lu disconnect_reason=%lu http_restarts=%lu\n",static_cast<long>(WiFi.status()==WL_CONNECTED?WiFi.RSSI():0),static_cast<unsigned long>(ESP.getFreeHeap()),static_cast<unsigned long>(ESP.getMinFreeHeap()),static_cast<unsigned long>(ESP.getMaxAllocHeap()),apActive?WiFi.softAPgetStationNum():0,manualAp,static_cast<unsigned long>(generation),static_cast<unsigned long>(disconnectReason.load()),static_cast<unsigned long>(httpRestarts));
}
void radioInfo(){if(!Serial)return;wifi_ps_type_t ps=WIFI_PS_MIN_MODEM;int8_t power=0;esp_err_t psError=esp_wifi_get_ps(&ps),txError=esp_wifi_get_max_tx_power(&power);Serial.printf("RADIO requested_sleep=%s configured_ps=%u driver_ps=%d ps_error=%d tx_qdbm=%d tx_error=%d channel=%ld rssi_dbm=%ld\n",radioSleep?"ON":"OFF",static_cast<unsigned>(WiFi.getSleep()),psError==ESP_OK?static_cast<int>(ps):-1,psError,static_cast<int>(power),txError,static_cast<long>(WiFi.channel()),static_cast<long>(WiFi.status()==WL_CONNECTED?WiFi.RSSI():0));}
void setRadioSleep(bool on){if(controlSnapshot().armed){if(Serial)Serial.println("ERR lock before WiFi sleep setting");return;}if(!WiFi.setSleep(on)){if(Serial)Serial.println("ERR WiFi sleep setting failed");return;}radioSleep=on;if(Serial)Serial.printf("OK WiFi sleep %s; diagnostic setting until reset\n",on?"ON":"OFF");radioInfo();}
void openAp(bool manual=false){if(manual){manualAp=true;manualApAt=millis();}if(!apActive){WiFi.mode(WIFI_AP_STA);apActive=WiFi.softAP(apName.c_str(),password.c_str(),1,false,2);connectedAt=millis();}info();}
bool saveList(const WifiList &l){return prefsReady&&prefs.putBytes("wifi-list",&l,sizeof(l))==sizeof(l);}
__attribute__((noinline)) void restoreNetworkChoice(){if(!prefsReady)return;String chosen=prefs.getString("last-ssid","");snprintf(preferredSsid,sizeof(preferredSsid),"%s",chosen.c_str());for(unsigned i=0;i<list.count;++i)if(!strcmp(preferredSsid,list.items[i].ssid)){activeIndex=i;return;}}
__attribute__((noinline)) void rememberNetworkChoice(){if(!prefsReady)return;String name=WiFi.SSID();if(name.length()<=32&&strcmp(name.c_str(),preferredSsid)){if(prefs.putString("last-ssid",name)==name.length())snprintf(preferredSsid,sizeof(preferredSsid),"%s",name.c_str());else if(Serial)Serial.println("WARN preferred WiFi save failed; connection kept");}for(unsigned i=0;i<list.count;++i)if(name==list.items[i].ssid){activeIndex=i;break;}}
void load(){prefsReady=prefs.begin("inc-net",false);if(prefsReady)password=prefs.getString("device-pass","");if(password.length()!=16){password=randomText(16);if(prefsReady)prefs.putString("device-pass",password);}csrf=randomText(32);list.schema=LIST_SCHEMA;
  WifiList candidate={};if(prefsReady&&prefs.getBytesLength("wifi-list")==sizeof(candidate)&&prefs.getBytes("wifi-list",&candidate,sizeof(candidate))==sizeof(candidate)&&validList(candidate)){list=candidate;saved=true;restoreNetworkChoice();return;}
  struct Legacy {uint32_t schema;char ssid[33],password[64];} old={};
  if(prefsReady&&prefs.getBytesLength("wifi")==sizeof(old)&&prefs.getBytes("wifi",&old,sizeof(old))==sizeof(old)&&old.schema==0xC3061001UL){Credentials c={};memcpy(c.ssid,old.ssid,sizeof(c.ssid));memcpy(c.password,old.password,sizeof(c.password));if(validCredential(c)){list.count=1;list.items[0]=c;saved=saveList(list);}}
}
void headers(bool entry=false){server.sendHeader("Cache-Control","no-store");server.sendHeader("X-Content-Type-Options","nosniff");server.sendHeader("X-Frame-Options","DENY");server.sendHeader("Referrer-Policy","no-referrer");server.sendHeader("Content-Security-Policy",entry?httppolicy::entryCsp:httppolicy::localCsp);}
void response(int code,bool ok,const String &message){headers();server.send(code,"application/json; charset=utf-8",String("{\"ok\":")+(ok?"true":"false")+",\"message\":"+quoted(message)+"}");}
bool authorized(bool mutation=false){const String host=server.hostHeader();
  if(!httpaccess::ipv4Authority(host.c_str())){response(403,false,"ERR use an IPv4 address with an optional port");return false;}
  if(!server.authenticate("admin",password.c_str())){headers();server.requestAuthentication(BASIC_AUTH,"Incubator","Login required");return false;}
  if(mutation){if(server.clientContentLength()>4096){response(413,false,"ERR request too large");return false;}if(server.header("X-CSRF-Token")!=csrf){response(403,false,"ERR refresh page");return false;}
    String origin=server.header("Origin");if(origin.length()&&origin!=String("http://")+server.hostHeader()){response(403,false,"ERR origin");return false;}}return true;
}
std::atomic<bool> httpBusy(false);
struct HttpBusyGuard {HttpBusyGuard(){httpBusy.store(true);}~HttpBusyGuard(){httpBusy.store(false);}};
bool admitResponse(bool page=false){
 if(ESP.getFreeHeap()>=httptransfer::HEAP_RESERVE&&ESP.getMaxAllocHeap()>=httptransfer::LARGEST_RESERVE)return true;
 httpLastFailure=httptransfer::Failure::LowMemory;httpLastErrno=0;
 // Reject before announcing a successful response and a large Content-Length.
 // The small failure body is also useful when the browser opens /local itself.
 headers();server.sendHeader("Retry-After","10");
 if(page)server.send(503,"text/html; charset=utf-8","<!doctype html><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><title>本机网页繁忙</title><p>本机网页资源暂时不足，控制程序继续运行。请稍后重试，或使用NAS控制页面。</p><a href=\"" INC_NAS_WEB_ORIGIN "/device/\">打开NAS控制页面</a>");
 else server.send(503,"application/json; charset=utf-8","{\"ok\":false,\"message\":\"本机网页资源暂时不足，请使用NAS页面或稍后重试\"}");
 return false;
}
struct HttpIO {
 NetworkClient &client;
 uint64_t now()const{return monoMs();}
 bool connected()const{return client.connected();}
 bool memoryReady(uint32_t free,uint32_t largest)const{return ESP.getFreeHeap()>=free&&ESP.getMaxAllocHeap()>=largest;}
 httptransfer::WriteResult write(const uint8_t *p,size_t n){const int fd=client.fd();if(fd<0)return {-1,ENOTCONN};const int written=::send(fd,p,n,MSG_DONTWAIT);return {written,written<0?errno:0};}
 void pause(){const TickType_t ticks=pdMS_TO_TICKS(2);vTaskDelay(ticks?ticks:1);}
};
bool sendBody(NetworkClient &client,const uint8_t *data,size_t length,uint32_t budgetMs){
 HttpIO io={client};const auto result=httptransfer::transmit(io,data,length,budgetMs);
 if(result.failure!=httptransfer::Failure::None){httpLastFailure=result.failure;httpLastErrno=result.error;client.stop();return false;}return true;
}
struct HttpSink {
 NetworkClient client;char buffer[512]={};size_t used=0;uint64_t deadline=0;
 explicit HttpSink(NetworkClient c):client(c),deadline(monoMs()+4500){}
 bool flush(){if(!used)return true;const uint64_t now=monoMs();if(now>=deadline){used=0;httpLastFailure=httptransfer::Failure::Deadline;httpLastErrno=0;client.stop();return false;}const bool ok=sendBody(client,reinterpret_cast<const uint8_t*>(buffer),used,static_cast<uint32_t>(deadline-now));used=0;return ok;}
 bool write(const char*p,size_t n){while(n){size_t count=sizeof(buffer)-used;if(count>n)count=n;memcpy(buffer+used,p,count);used+=count;p+=count;n-=count;if(used==sizeof(buffer)&&!flush())return false;}return true;}
};
void localHome(){
 if(!authorized())return;
 HttpBusyGuard guard;
 ++homeRequests;
 if(!admitResponse(true)){++homeFailures;homeLastMs=0;homeLastSent=0;return;}
 const bool gzip=httptransfer::acceptsGzip(server.header("Accept-Encoding").c_str());
 const uint8_t *body=gzip?WEB_UI_GZIP:reinterpret_cast<const uint8_t*>(WEB_UI);
 const size_t length=gzip?sizeof(WEB_UI_GZIP):sizeof(WEB_UI)-1;
 headers();server.sendHeader("Vary","Accept-Encoding");if(gzip)server.sendHeader("Content-Encoding","gzip");server.setContentLength(length);server.send(200,"text/html; charset=utf-8","");
 NetworkClient client=server.client();HttpIO io={client};const uint64_t start=monoMs();const auto result=httptransfer::transmit(io,body,length,15000);homeLastMs=static_cast<uint32_t>(monoMs()-start);homeLastSent=result.sent;
 if(result.failure!=httptransfer::Failure::None){++homeFailures;++responseWriteFailures;httpLastFailure=result.failure;httpLastErrno=result.error;client.stop();}
 if(Serial)Serial.printf("WEB_HOME encoding=%s sent=%lu/%lu elapsed_ms=%lu result=%u errno=%d heap_free=%lu largest=%lu\n",gzip?"gzip":"plain",static_cast<unsigned long>(result.sent),static_cast<unsigned long>(length),static_cast<unsigned long>(homeLastMs),static_cast<unsigned>(result.failure),result.error,static_cast<unsigned long>(ESP.getFreeHeap()),static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}

void home(){if(!authorized())return;HttpBusyGuard guard;++homeRequests;if(!admitResponse(true)){++homeFailures;homeLastMs=0;homeLastSent=0;return;}headers(true);server.setContentLength(sizeof(WEB_ENTRY)-1);server.send(200,"text/html; charset=utf-8","");NetworkClient client=server.client();HttpIO io={client};const uint64_t start=monoMs();const auto result=httptransfer::transmit(io,reinterpret_cast<const uint8_t*>(WEB_ENTRY),sizeof(WEB_ENTRY)-1,5000);homeLastMs=monoMs()-start;homeLastSent=result.sent;if(result.failure!=httptransfer::Failure::None){++homeFailures;++responseWriteFailures;httpLastFailure=result.failure;httpLastErrno=result.error;client.stop();}if(Serial)Serial.printf("WEB_ENTRY sent=%lu/%lu elapsed_ms=%lu result=%u\n",static_cast<unsigned long>(result.sent),static_cast<unsigned long>(sizeof(WEB_ENTRY)-1),static_cast<unsigned long>(homeLastMs),static_cast<unsigned>(result.failure));}
void entryState(){
 if(!authorized())return;HttpBusyGuard guard;++stateRequests;if(!admitResponse())return;
 controlRead(httpSnapshot);const bool wifi=WiFi.status()==WL_CONNECTED;
 const String ssid=wifi?WiFi.SSID():String(""),ip=wifi?WiFi.localIP().toString():String("");
 jsonstream::Buffer<512> buffer;jsonstream::Writer<jsonstream::Buffer<512>> body(buffer);
 entrystate::emit(body,"Connected-2.2.3b",csrf.c_str(),httpSnapshot.clockText,ssid.c_str(),ip.c_str(),wifi,mqttNasAlive());
 if(!body.ok()){response(503,false,"ERR entry state exceeds bounded response size");return;}
 headers();server.setContentLength(buffer.count);server.send(200,"application/json; charset=utf-8","");
 NetworkClient client=server.client();if(!sendBody(client,reinterpret_cast<const uint8_t*>(buffer.data),buffer.count,1000))++responseWriteFailures;
}
void state(){
 if(!authorized())return;HttpBusyGuard guard;++stateRequests;if(!admitResponse())return;
 auto &s=httpSnapshot;controlRead(s);const auto b=bleView();const auto m=mqttView();const auto d=eventLogView();const bool wifi=WiFi.status()==WL_CONNECTED;
 const String ssid=wifi?WiFi.SSID():String(""),ip=wifi?WiFi.localIP().toString():String(""),apIp=apActive?WiFi.softAPIP().toString():String("");
 char t[20],h[20];decimal::sample(t,sizeof(t),s.temperature,s.sensorValid);decimal::sample(h,sizeof(h),s.humidity,s.sensorValid);
 webstate::Input in={s,b,m,d,csrf.c_str(),t,h,ssid.c_str(),ip.c_str(),apIp.c_str(),wifi,apActive,list.count,{},ESP.getFreeHeap(),ESP.getMinFreeHeap(),ESP.getMaxAllocHeap()};
 in.health=healthView();in.photo=photoView();in.generation=generation;in.disconnectReason=disconnectReason.load();in.httpRestarts=httpRestarts;
 for(uint8_t i=0;i<list.count;++i)in.profiles[i]=list.items[i].ssid;
 jsonstream::Counter measure;jsonstream::Writer<jsonstream::Counter> count(measure);webstate::emit(count,in);
 if(!count.ok()){response(503,false,"ERR state exceeds bounded response size");return;}
 headers();server.setContentLength(count.size());server.send(200,"application/json; charset=utf-8","");
 HttpSink sink(server.client());jsonstream::Writer<HttpSink> body(sink);webstate::emit(body,in);if(!body.ok()||!sink.flush())++responseWriteFailures;
}
void command(){if(!authorized(true))return;String text=server.arg("cmd");if(text.isEmpty()||text.length()>=sizeof(ControlCommand::text)){response(400,false,"ERR command length");return;}
  for(size_t i=0;i<text.length();++i)if(text[i]<32||text[i]>126){response(400,false,"ERR command characters");return;}
  ControlCommand c={};c.origin=Origin::Web;c.id=++nextId;c.expiresMs=monoMs()+2000;snprintf(c.text,sizeof(c.text),"%s",text.c_str());
  if(!controlEnqueue(c)){response(503,false,"ERR busy");return;}ControlReply r;uint64_t deadline=monoMs()+2500;
  while(monoMs()<deadline)if(xQueueReceive(replies,&r,pdMS_TO_TICKS(20))==pdTRUE&&r.id==c.id){response(r.ok?200:400,r.ok,r.text);return;}
  ++commandTimeouts;response(503,false,"ERR timeout; query status before retrying");
}
void scheduleConnection(uint8_t index){activeIndex=index;WiFi.disconnectAsync(false,false);waiting=true;beginAt=millis()+300;attemptAt=millis();wasConnected=false;}
void wifiHandler(){if(!authorized(true))return;
  String op=server.arg("op");if(op=="connect"||op=="remove"){
    if(controlSnapshot().armed){response(409,false,"ERR lock before switching or removing WiFi");return;}
    String value=server.arg("index");char *end=nullptr;long index=strtol(value.c_str(),&end,10);
    if(!value.length()||*end||index<0||index>=list.count){response(400,false,"ERR profile index");return;}
    if(op=="connect"){response(200,true,"OK switching profile");scheduleConnection(index);return;}
    WifiList next=list;for(uint8_t i=index;i+1<next.count;++i)next.items[i]=next.items[i+1];--next.count;next.items[next.count]={};
    if(!saveList(next)){response(500,false,"ERR NVS write failed");return;}bool removingCurrent=WiFi.status()==WL_CONNECTED&&WiFi.SSID()==list.items[index].ssid;list=next;saved=true;
    response(200,true,"OK profile removed");if(removingCurrent){if(list.count)scheduleConnection(0);else{WiFi.disconnectAsync(false,false);waiting=false;openAp();}}else if(activeIndex>=list.count)activeIndex=0;return;
  }
  if(op!="add"){response(400,false,"ERR WiFi operation");return;}
  String ssid=server.arg("ssid"),key=server.arg("password");Credentials next={};
  if(ssid.length()>32||key.length()>63||ssid.length()!=strlen(ssid.c_str())||key.length()!=strlen(key.c_str())){response(400,false,"ERR WiFi length");return;}
  snprintf(next.ssid,sizeof(next.ssid),"%s",ssid.c_str());snprintf(next.password,sizeof(next.password),"%s",key.c_str());
  if(!validCredential(next)){response(400,false,"ERR SSID 1..32 bytes; password 8..63 bytes");return;}
  WifiList updated=list;uint8_t index=updated.count;for(uint8_t i=0;i<updated.count;++i)if(ssid==updated.items[i].ssid)index=i;
  if(index==updated.count){if(updated.count==MAX_WIFI){response(400,false,"ERR maximum 8 WiFi profiles");return;}++updated.count;}updated.items[index]=next;
  if(!saveList(updated)){response(500,false,"ERR NVS write failed");return;}list=updated;saved=true;response(200,true,"OK WiFi saved; current connection is kept");
  if(!controlSnapshot().armed&&WiFi.status()!=WL_CONNECTED&&!waiting)scheduleConnection(index);
}
void mqttHandler(){if(!authorized(true))return;if(controlSnapshot().armed){response(409,false,"ERR lock before MQTT configuration");return;}
  String host=server.arg("host"),portText=server.arg("port"),user=server.arg("username"),key=server.arg("password"),enabled=server.arg("enabled");char *end=nullptr;long port=strtol(portText.c_str(),&end,10);
  if(!portText.length()||*end||port<1||port>65535||(enabled!="0"&&enabled!="1")||host.length()!=strlen(host.c_str())||user.length()!=strlen(user.c_str())||key.length()!=strlen(key.c_str())){response(400,false,"ERR MQTT fields");return;}
  char error[128];if(!mqttConfigure(enabled=="1",host.c_str(),port,user.c_str(),key.c_str(),error,sizeof(error))){response(400,false,error);return;}response(200,true,"OK MQTT configuration saved; connecting");
}
void photoModelHandler(){if(!authorized())return;response(200,true,"Optical models are stored on NAS. Open the main portal at / for its configured link.");}
void setupServer(){const char *h[]={"X-CSRF-Token","Origin","Accept-Encoding"};server.collectHeaders(h,3);server.on("/",HTTP_GET,home);server.on("/local",HTTP_GET,localHome);
  server.on("/api/entry",HTTP_GET,entryState);server.on("/api/state",HTTP_GET,state);server.on("/api/photo-model",HTTP_GET,photoModelHandler);server.on("/api/command",HTTP_POST,command);server.on("/api/wifi",HTTP_POST,wifiHandler);server.on("/api/mqtt",HTTP_POST,mqttHandler);server.onNotFound([]{response(404,false,"ERR not found");});server.begin();
}
void timeAvailable(timeval *){time_t e=time(nullptr);if(e>=1704067200LL&&e<4102444800LL){everSynced.store(true);synced.store(true);}}
void startNtp(){esp_sntp_stop();esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);esp_sntp_setservername(0,"ntp.aliyun.com");esp_sntp_setservername(1,"pool.ntp.org");esp_sntp_setservername(2,"time.cloudflare.com");esp_sntp_init();ntpAt=millis();}
void worker(void *){
  char suffix[7];snprintf(suffix,sizeof(suffix),"%06lX",static_cast<unsigned long>(deviceIdentityValue()));apName=String("Incubator-")+suffix;
  WiFi.persistent(false);WiFi.mode(WIFI_OFF);vTaskDelay(pdMS_TO_TICKS(50));
  WiFi.onEvent([](arduino_event_id_t,arduino_event_info_t info){disconnectReason.store(info.wifi_sta_disconnected.reason);},ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);if(!WiFi.setSleep(false)&&Serial)Serial.println("ERR WiFi sleep default OFF failed");WiFi.setAutoReconnect(false);load();setupServer();radioInfo();
  if(list.count)scheduleConnection(activeIndex);else openAp();info();
  for(;;){uint32_t now=millis();NetworkAction action;
    if(xQueueReceive(actions,&action,0)==pdTRUE){if(action==NetworkAction::Restart){if(!controlSnapshot().armed){server.stop();++httpRestarts;WiFi.mode(WIFI_OFF);vTaskDelay(pdMS_TO_TICKS(50));WiFi.mode(WIFI_STA);WiFi.setSleep(false);apActive=false;wasConnected=false;server.begin();if(list.count)scheduleConnection(activeIndex);else openAp();if(Serial)Serial.println("OK WiFi and HTTP reinitialized; settings kept");}}else if(action==NetworkAction::Info){info();radioInfo();}else if(action==NetworkAction::RadioInfo)radioInfo();else if(action==NetworkAction::SleepOn)setRadioSleep(true);else if(action==NetworkAction::SleepOff)setRadioSleep(false);else if(action==NetworkAction::Secrets)info(true);else if(action==NetworkAction::OpenAp)openAp(true);else if(!controlSnapshot().armed){WifiList empty={};empty.schema=LIST_SCHEMA;if(saveList(empty)){list=empty;saved=true;waiting=false;WiFi.disconnectAsync(false,true);openAp();}else if(Serial)Serial.println("ERR WiFi forget write failed");}}
    if(waiting&&due(now,beginAt)){waiting=false;if(list.count){if(activeIndex>=list.count)activeIndex=0;WiFi.begin(list.items[activeIndex].ssid,list.items[activeIndex].password);attemptAt=millis();}}
    bool connected=WiFi.status()==WL_CONNECTED;
    if(connected&&!wasConnected){rememberNetworkChoice();++generation;server.stop();server.begin();++httpRestarts;connectedAt=millis();WiFi.setSleep(false);startNtp();if(Serial)Serial.printf("NET_READY generation=%lu http_restarts=%lu disconnect_reason=%lu\n",static_cast<unsigned long>(generation),static_cast<unsigned long>(httpRestarts),static_cast<unsigned long>(disconnectReason.load()));info();}
    if(!connected&&wasConnected)attemptAt=millis();wasConnected=connected;
    if(list.count&&!connected&&!waiting&&static_cast<uint32_t>(millis()-attemptAt)>=12000){if(!apActive)openAp();scheduleConnection((activeIndex+1)%list.count);}
    if(connected&&!everSynced.load()&&static_cast<uint32_t>(millis()-ntpAt)>=60000)startNtp();
    const bool apExpired=manualAp?static_cast<uint32_t>(millis()-manualApAt)>=900000:static_cast<uint32_t>(millis()-connectedAt)>=60000&&(WiFi.softAPgetStationNum()==0||static_cast<uint32_t>(millis()-connectedAt)>=900000);
    if(apActive&&connected&&apExpired){WiFi.softAPdisconnect(false);WiFi.mode(WIFI_STA);apActive=false;manualAp=false;}
    bleSetApClientActive(apActive&&WiFi.softAPgetStationNum()>0);publish();server.handleClient();vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}
bool networkStart(){replies=xQueueCreate(2,sizeof(ControlReply));actions=xQueueCreate(4,sizeof(NetworkAction));sntp_set_time_sync_notification_cb(timeAvailable);return replies&&actions&&xTaskCreate(worker,"inc-http",8192,nullptr,1,&httpTask)==pdPASS;}
NetworkView networkView(){NetworkView v;portENTER_CRITICAL(&viewMux);v=sharedView;portEXIT_CRITICAL(&viewMux);return v;}
void networkAnswer(const ControlReply &r){if(replies)xQueueSend(replies,&r,0);}
bool networkTakeTimeSync(){return synced.exchange(false);}
bool networkRequestAction(NetworkAction a){return actions&&xQueueSend(actions,&a,0)==pdTRUE;}

bool networkHttpBusy(){return httpBusy.load();}
