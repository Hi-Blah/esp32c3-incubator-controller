#include "sample_journal.h"
#include "device_identity.h"
#include "event_log.h"
#include "format_decimal.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_random.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <atomic>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
namespace {
constexpr uint32_t MAGIC=0xC3063001;
constexpr size_t QUEUE_SLOTS=64,HISTORY_SLOTS=32;
struct Record {uint32_t magic,crc;uint64_t ordinal;uint16_t length;uint8_t preserve;char json[1024];};
struct EventRequest {
  uint64_t uptimeMs, checkpointEpochMs, recoveryGapMs, sampleEpochMs, elapsed[2];
  uint32_t sequence, restartAfterSeconds, checkpointSeconds,rawSum[2];uint16_t rawCount;
  float temperature,humidity;
  uint16_t frequency[2]; uint8_t duty[2];
  bool preserve, sensorValid, timeTrusted, sampleTimeTrusted, armed, output[2], pwm[2], recoveryEnabled, countOutage;
  char kind[20],detail[32],clockText[24];
};
static_assert(sizeof(EventRequest)<=208,"Compact event queue exceeded memory budget");
// Both buffers are protected by files, including scan/peek/dump; no task shares them unlocked.
Record ioRecord={},scanCandidate={};
QueueHandle_t incoming=nullptr,acks=nullptr;
TaskHandle_t logTask=nullptr;
bool serialize(const EventRequest &request,Record &record);
SemaphoreHandle_t files=nullptr;
Preferences prefs;
std::atomic<bool> initialize(false),printHistory(false);
portMUX_TYPE viewMux=portMUX_INITIALIZER_UNLOCKED;
EventLogView view={};
uint64_t orders[QUEUE_SLOTS]={},history[HISTORY_SLOTS]={},nextOrder=1;
uint32_t sequence=0;
portMUX_TYPE sequenceMux=portMUX_INITIALIZER_UNLOCKED;
uint32_t checksum(const char *p,size_t length){uint32_t v=2166136261UL;for(size_t i=0;i<length;++i)v=(v^static_cast<uint8_t>(p[i]))*16777619UL;return v;}
const char *resetName(esp_reset_reason_t r){switch(r){case ESP_RST_POWERON:return "POWER_OR_EXTERNAL";case ESP_RST_SW:return "SOFTWARE";case ESP_RST_PANIC:return "PANIC";case ESP_RST_INT_WDT:return "INTERRUPT_WDT";case ESP_RST_TASK_WDT:return "TASK_WDT";case ESP_RST_WDT:return "OTHER_WDT";case ESP_RST_BROWNOUT:return "BROWNOUT";case ESP_RST_DEEPSLEEP:return "DEEP_SLEEP";case ESP_RST_EXT:return "EXTERNAL_RESET";case ESP_RST_USB:return "USB_RESET";case ESP_RST_JTAG:return "JTAG_RESET";case ESP_RST_EFUSE:return "EFUSE_ERROR";case ESP_RST_PWR_GLITCH:return "POWER_GLITCH";case ESP_RST_CPU_LOCKUP:return "CPU_LOCKUP";default:return "UNKNOWN";}}
void countFailure(const char*stage="unknown",int error=0){const uint32_t at=millis();portENTER_CRITICAL(&viewMux);++view.writeFailures;view.lastFailureAtMs=at;view.lastError=error;snprintf(view.lastFailure,sizeof(view.lastFailure),"%s",stage);portEXIT_CRITICAL(&viewMux);}
void countDrop(){portENTER_CRITICAL(&viewMux);++view.dropped;portEXIT_CRITICAL(&viewMux);}
void path(char *b,size_t size,bool archive,size_t index,bool temporary=false){snprintf(b,size,"/%c%02u.%s",archive?'h':'q',static_cast<unsigned>(index),temporary?"tmp":"dat");}
void absolutePath(char *b,size_t n,const char*name){snprintf(b,n,"/littlefs%s",name);}
bool read(const char *name,Record &r){
 char full[40];absolutePath(full,sizeof(full),name);int fd=::open(full,O_RDONLY);if(fd<0)return false;
 struct stat st={};int statResult=::fstat(fd,&st);bool ok=statResult==0&&st.st_size==sizeof(r);int saved=statResult<0?errno:st.st_size!=sizeof(r)?EIO:0;size_t used=0;
 while(ok&&used<sizeof(r)){ssize_t got=::read(fd,reinterpret_cast<uint8_t*>(&r)+used,sizeof(r)-used);if(got<0&&errno==EINTR)continue;if(got<=0){ok=false;saved=got<0?errno:EIO;break;}used+=got;}
 if(::close(fd)!=0){ok=false;saved=errno;}
 if(ok&&(r.magic!=MAGIC||!r.ordinal||r.length>=sizeof(r.json)||r.json[r.length]!=0||r.crc!=checksum(r.json,r.length))){ok=false;saved=EILSEQ;}
 if(!ok)errno=saved?saved:EIO;
 return ok;
}
bool moveFile(const char*from,const char*to){char a[40],b[40];absolutePath(a,sizeof(a),from);absolutePath(b,sizeof(b),to);return ::rename(a,b)==0;}
bool removeFile(const char*name){char full[40];absolutePath(full,sizeof(full),name);return ::unlink(full)==0;}
bool write(bool archive,size_t slot,Record &r){
 char target[16],tmp[16],full[40];path(target,sizeof(target),archive,slot);path(tmp,sizeof(tmp),archive,slot,true);absolutePath(full,sizeof(full),tmp);
 int fd=::open(full,O_WRONLY|O_CREAT|O_TRUNC,0666);if(fd<0){countFailure("open",errno);return false;}
 bool ok=true;size_t used=0;while(used<sizeof(r)){ssize_t n=::write(fd,reinterpret_cast<const uint8_t*>(&r)+used,sizeof(r)-used);if(n<0&&errno==EINTR)continue;if(n<=0){countFailure("write",n<0?errno:EIO);ok=false;break;}used+=n;}
 if(ok&&::fsync(fd)!=0){countFailure("sync",errno);ok=false;}if(::close(fd)!=0){countFailure("close",errno);ok=false;}
 if(!ok)return false;
 if(!moveFile(tmp,target)){countFailure("rename",errno);return false;}return true;
}
void scan(){nextOrder=1;memset(orders,0,sizeof(orders));memset(history,0,sizeof(history));for(unsigned group=0;group<2;++group){size_t n=group?HISTORY_SLOTS:QUEUE_SLOTS;for(size_t i=0;i<n;++i){char name[16],tmp[16];path(name,sizeof(name),group,i);path(tmp,sizeof(tmp),group,i,true);auto &old=ioRecord;auto &candidate=scanCandidate;old={};candidate={};bool haveOld=read(name,old),haveTemp=read(tmp,candidate);if(haveTemp&&(!haveOld||candidate.ordinal>old.ordinal)){if(moveFile(tmp,name)){old=candidate;haveOld=true;}else countFailure("scan_rename",errno);}if(haveOld){(group?history[i]:orders[i])=old.ordinal;if(old.ordinal>=nextOrder)nextOrder=old.ordinal+1;}}}}
void updatePending(){uint16_t n=0;for(auto v:orders)if(v)++n;portENTER_CRITICAL(&viewMux);view.pending=n;portEXIT_CRITICAL(&viewMux);}
void dump(){if(!Serial)return;for(unsigned printed=0;printed<8;++printed){uint64_t limit=UINT64_MAX;size_t selected=HISTORY_SLOTS;static uint64_t previous=0;if(printed==0)previous=UINT64_MAX;for(size_t i=0;i<HISTORY_SLOTS;++i)if(history[i]&&history[i]<previous&&(selected==HISTORY_SLOTS||history[i]>limit)){selected=i;limit=history[i];}if(selected==HISTORY_SLOTS)break;previous=limit;char name[16];path(name,sizeof(name),true,selected);auto &r=ioRecord;if(read(name,r)){Serial.println(r.json);}}}
void worker(void*){uint32_t savedDropped=0,savedFailures=0,lastCounterSave=millis();for(;;){
  if(initialize.exchange(false)){xSemaphoreTake(files,portMAX_DELAY);bool ready=eventLogView().ready;if(!ready){ready=LittleFS.begin(true,"/littlefs",5,"spiffs");if(ready)scan();}portENTER_CRITICAL(&viewMux);view.ready=ready;portEXIT_CRITICAL(&viewMux);updatePending();xSemaphoreGive(files);if(Serial)Serial.printf("LOG_STORAGE ready=%u\n",ready);}
  if(eventLogView().ready){
    uint64_t ack;while(xQueueReceive(acks,&ack,0)==pdTRUE){xSemaphoreTake(files,portMAX_DELAY);size_t slot=ack%QUEUE_SLOTS;if(orders[slot]==ack){char name[16];path(name,sizeof(name),false,slot);if(removeFile(name))orders[slot]=0;else countFailure("remove",errno);}updatePending();xSemaphoreGive(files);}
    EventRequest request={};if(xQueueReceive(incoming,&request,pdMS_TO_TICKS(20))==pdTRUE){xSemaphoreTake(files,portMAX_DELAY);auto &r=ioRecord;if(serialize(request,r)){r.ordinal=nextOrder++;size_t slot=r.ordinal%QUEUE_SLOTS;if(write(false,slot,r)){if(orders[slot])countDrop();orders[slot]=r.ordinal;}else countDrop();if(r.preserve){size_t oldest=0;for(size_t i=1;i<HISTORY_SLOTS;++i)if(history[i]<history[oldest])oldest=i;if(write(true,oldest,r))history[oldest]=r.ordinal;}updatePending();}xSemaphoreGive(files);}
    xSemaphoreTake(files,portMAX_DELAY);sampleJournalTick();xSemaphoreGive(files);
    if(printHistory.exchange(false)){xSemaphoreTake(files,portMAX_DELAY);dump();xSemaphoreGive(files);}
  }else vTaskDelay(pdMS_TO_TICKS(100));
  if(static_cast<uint32_t>(millis()-lastCounterSave)>=60000){auto v=eventLogView();if(prefs.isKey("boot")){if(v.dropped!=savedDropped)prefs.putUInt("dropped",v.dropped);if(v.writeFailures!=savedFailures)prefs.putUInt("failures",v.writeFailures);}savedDropped=v.dropped;savedFailures=v.writeFailures;lastCounterSave=millis();}
}}
}
bool eventLogStart(){files=xSemaphoreCreateMutex();incoming=xQueueCreate(6,sizeof(EventRequest));acks=xQueueCreate(8,sizeof(uint64_t));if(!files||!incoming||!acks)return false;
  auto reason=esp_reset_reason();bool havePrefs=prefs.begin("inc-diag",false);if(havePrefs){view.bootCount=prefs.getUInt("boot",0)+1;view.watchdogStarts=prefs.getUInt("wdt",0)+((reason==ESP_RST_INT_WDT||reason==ESP_RST_TASK_WDT||reason==ESP_RST_WDT)?1:0);view.powerLikeStarts=prefs.getUInt("power",0)+(reason==ESP_RST_POWERON?1:0);view.dropped=prefs.getUInt("dropped",0);view.writeFailures=prefs.getUInt("failures",0);prefs.putUInt("boot",view.bootCount);prefs.putUInt("wdt",view.watchdogStarts);prefs.putUInt("power",view.powerLikeStarts);}
  snprintf(view.bootId,sizeof(view.bootId),"%06lx-%lu-%08lx",static_cast<unsigned long>(deviceIdentityValue()),static_cast<unsigned long>(view.bootCount),static_cast<unsigned long>(esp_random()));snprintf(view.resetReason,sizeof(view.resetReason),"%s",resetName(reason));
  view.ready=LittleFS.begin(false,"/littlefs",5,"spiffs");if(view.ready){scan();updatePending();}
  return xTaskCreate(worker,"inc-log",4096,nullptr,1,&logTask)==pdPASS;
}
void eventEmit(const char *kind,const char *detail,const ControlSnapshot &s,bool preserve) {
  if(!incoming)return;EventRequest request={};
  if(!kind||strlen(kind)>=sizeof(request.kind)||(detail&&strlen(detail)>=sizeof(request.detail))){countDrop();return;}
  snprintf(request.kind,sizeof(request.kind),"%s",kind);snprintf(request.detail,sizeof(request.detail),"%s",detail?detail:"");
  snprintf(request.clockText,sizeof(request.clockText),"%s",s.clockText);
  portENTER_CRITICAL(&sequenceMux);request.sequence=++sequence;portEXIT_CRITICAL(&sequenceMux);
  request.preserve=preserve;request.uptimeMs=s.uptimeMs;request.checkpointEpochMs=s.checkpointEpochMs;request.recoveryGapMs=s.recoveryGapMs;request.sampleEpochMs=s.sampleEpochMs;
  request.temperature=s.temperature;request.humidity=s.humidity;request.sensorValid=s.sensorValid;request.timeTrusted=networkClock(s.clockSource);request.sampleTimeTrusted=s.sampleTimeTrusted;request.armed=s.armed;
  request.recoveryEnabled=s.recovery.enabled;request.countOutage=s.recovery.countOutage;request.restartAfterSeconds=s.recovery.restartAfterSeconds;request.checkpointSeconds=s.recovery.checkpointSeconds;
  for(unsigned i=0;i<2;++i){request.elapsed[i]=s.progress[i].elapsedMs;request.output[i]=s.output[i];request.pwm[i]=s.config[i].pwm;request.frequency[i]=s.config[i].frequency;request.duty[i]=s.config[i].duty;}
  if(xQueueSend(incoming,&request,0)!=pdTRUE)countDrop();
}
bool eventCalibrationPoint(const char*run,uint8_t ch,uint16_t hz,uint8_t duty,uint16_t raw,uint16_t crossRaw,const ControlSnapshot&s,uint32_t rawSum,uint32_t crossSum,uint16_t count){
 if(!incoming||!run||strlen(run)>=32||ch>1)return false;EventRequest r={};snprintf(r.kind,sizeof(r.kind),"calibration_point");snprintf(r.detail,sizeof(r.detail),"%s",run);snprintf(r.clockText,sizeof(r.clockText),"%s",s.clockText);
 portENTER_CRITICAL(&sequenceMux);r.sequence=++sequence;portEXIT_CRITICAL(&sequenceMux);
 r.uptimeMs=s.uptimeMs;r.timeTrusted=networkClock(s.clockSource);r.sampleEpochMs=s.epochMs;r.frequency[0]=hz;r.frequency[1]=raw;r.duty[0]=duty;r.duty[1]=ch+1;r.elapsed[0]=crossRaw;r.rawSum[0]=rawSum;r.rawSum[1]=crossSum;r.rawCount=count;
 if(xQueueSend(incoming,&r,0)!=pdTRUE){countDrop();return false;}return true;
}
namespace {
bool serialize(const EventRequest &s,Record &r) {
  r={};r.magic=MAGIC;r.preserve=s.preserve;auto v=eventLogView();
  char t[20],h[20];decimal::sample(t,sizeof(t),s.temperature,s.sensorValid);decimal::sample(h,sizeof(h),s.humidity,s.sensorValid);
  char epoch[32]="null";if(s.clockText[0])snprintf(epoch,sizeof(epoch),R"JSON("%s")JSON",s.clockText);
  int n;
  if(!strcmp(s.kind,"calibration_point")){
    n=snprintf(r.json,sizeof(r.json),R"JSON({"event_id":"%s-%lu","boot_id":"%s","kind":"calibration_point","run":"%s","time":%s,"time_trusted":%s,"uptime_ms":%llu,"epoch_ms":%llu,"channel":%u,"frequency":%u,"duty":%u,"raw":%u,"cross_raw":%llu,"raw_sum":%lu,"cross_sum":%lu,"sample_count":%u})JSON",v.bootId,static_cast<unsigned long>(s.sequence),v.bootId,s.detail,epoch,s.timeTrusted?"true":"false",s.uptimeMs,s.sampleEpochMs,s.duty[1],s.frequency[0],s.duty[0],s.frequency[1],s.elapsed[0],static_cast<unsigned long>(s.rawSum[0]),static_cast<unsigned long>(s.rawSum[1]),s.rawCount);
  }else n=snprintf(r.json,sizeof(r.json),R"JSON({"event_id":"%s-%lu","boot_id":"%s","kind":"%s","detail":"%s","time":%s,"time_trusted":%s,"uptime_ms":%llu,"reset_reason":"%s","boot_count":%lu,"power_or_external_count":%lu,"watchdog_count":%lu,"last_saved_epoch_ms":%llu,"estimated_gap_ms":%llu,"armed":%s,"l1":%s,"l2":%s,"elapsed1_ms":%llu,"elapsed2_ms":%llu,"sensor_valid":%s,"temperature":%s,"humidity":%s,"sample_epoch_ms":%llu,"sample_time_trusted":%s,"recovery_enabled":%s,"count_outage":%s,"restart_after_s":%lu,"checkpoint_s":%lu,"frequency1":%u,"frequency2":%u,"duty1":%u,"duty2":%u,"pwm1":%s,"pwm2":%s})JSON",v.bootId,static_cast<unsigned long>(s.sequence),v.bootId,s.kind,s.detail,epoch,s.timeTrusted?"true":"false",s.uptimeMs,v.resetReason,static_cast<unsigned long>(v.bootCount),static_cast<unsigned long>(v.powerLikeStarts),static_cast<unsigned long>(v.watchdogStarts),s.checkpointEpochMs,s.recoveryGapMs,s.armed?"true":"false",s.output[0]?"true":"false",s.output[1]?"true":"false",s.elapsed[0],s.elapsed[1],s.sensorValid?"true":"false",t,h,s.sampleEpochMs,s.sampleTimeTrusted?"true":"false",s.recoveryEnabled?"true":"false",s.countOutage?"true":"false",static_cast<unsigned long>(s.restartAfterSeconds),static_cast<unsigned long>(s.checkpointSeconds),s.frequency[0],s.frequency[1],s.duty[0],s.duty[1],s.pwm[0]?"true":"false",s.pwm[1]?"true":"false");
  if(n<0||static_cast<size_t>(n)>=sizeof(r.json)){countDrop();return false;}r.length=n;r.crc=checksum(r.json,r.length);return true;
}
}
EventLogView eventLogView(){EventLogView v;portENTER_CRITICAL(&viewMux);v=view;portEXIT_CRITICAL(&viewMux);if(logTask)v.stackMinimum=uxTaskGetStackHighWaterMark(logTask);return v;}
bool eventPeekJson(char *json,size_t capacity,uint64_t &ordinal){if(!json||!capacity||!files||!eventLogView().ready||xSemaphoreTake(files,0)!=pdTRUE)return false;size_t selected=QUEUE_SLOTS;for(size_t i=0;i<QUEUE_SLOTS;++i)if(orders[i]&&(selected==QUEUE_SLOTS||orders[i]<orders[selected]))selected=i;bool ok=false;if(selected<QUEUE_SLOTS){char name[16];path(name,sizeof(name),false,selected);auto &r=ioRecord;if(read(name,r)){if(size_t(r.length)+1<=capacity){ordinal=r.ordinal;memcpy(json,r.json,size_t(r.length)+1);ok=true;}}else{orders[selected]=0;countFailure("record_read",errno);updatePending();}}xSemaphoreGive(files);return ok;}
bool eventPeek(EventPacket &packet){return eventPeekJson(packet.json,sizeof(packet.json),packet.ordinal);}
void eventAck(uint64_t ordinal){if(acks)xQueueSend(acks,&ordinal,0);}
void eventLogInitialize(){initialize.store(true);}
void eventLogPrint(){printHistory.store(true);}

bool eventStorageTake(){return files&&xSemaphoreTake(files,0)==pdTRUE;}
void eventStorageGive(){xSemaphoreGive(files);}
