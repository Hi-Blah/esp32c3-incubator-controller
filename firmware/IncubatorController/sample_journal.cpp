#include "module_config.h"
#include "sample_journal.h"
#include "sample_store.h"
#include "sample_policy.h"
#include "control_service.h"
#include "mqtt_service.h"
#include "event_log.h"
#include "light_feedback.h"
#include "format_decimal.h"
#include <LittleFS.h>
#include <Preferences.h>
namespace {
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
SampleJournalView view={bool(INC_ENABLE_SAMPLE_JOURNAL),false,0,samples::PAGE_COUNT*samples::PER_PAGE,0,0,0,0,INC_SAMPLE_INTERVAL_MS,0,0};
#if INC_ENABLE_SAMPLE_JOURNAL
samples::Store store;ControlSnapshot snapshot;samples::Record previous={};
bool initialized=false,prefsReady=false,linkKnown=false,wasOnline=false;uint64_t retryAt=0,lastSample=0,offlineStart=0,offlineBase=0,outageId=0,compactId=0;uint16_t period=10,compactPeriod=0;uint8_t lastSource=0;
uint32_t lastSave=0,savedCovered=0,savedThinned=0,savedFailures=0,lastCompact=0;Preferences prefs;
struct Receipt {char boot[32];uint64_t seq,uptime;};Receipt receipt={};bool receiptReady=false;
void refresh(){SampleJournalView next={true,initialized,(uint16_t)store.pending(),samples::PAGE_COUNT*samples::PER_PAGE,store.covered,store.thinned,store.failures,store.damaged,uint32_t(period)*1000,(uint32_t)LittleFS.totalBytes(),(uint32_t)LittleFS.usedBytes()};portENTER_CRITICAL(&mux);view=next;portEXIT_CRITICAL(&mux);}
#endif
}
SampleJournalView sampleJournalView(){SampleJournalView r;portENTER_CRITICAL(&mux);r=view;portEXIT_CRITICAL(&mux);return r;}
void sampleJournalTick(){
#if INC_ENABLE_SAMPLE_JOURNAL
 if(!eventLogView().ready)return;
 if(!initialized){if(LittleFS.totalBytes()<512*1024)return;initialized=store.begin("/littlefs");if(!initialized)return;prefsReady=prefs.begin("inc-samples",false);if(prefsReady){store.covered=prefs.getUInt("covered",0);store.thinned=prefs.getUInt("thinned",0);store.failures+=prefs.getUInt("failures",0);}if(store.latest(previous)&&(previous.flags&samples::OUTAGE)){outageId=previous.outageId;offlineBase=previous.offlineElapsed;offlineStart=0;}refresh();}
 Receipt ack={};bool have=false;portENTER_CRITICAL(&mux);if(receiptReady){ack=receipt;receiptReady=false;have=true;}portEXIT_CRITICAL(&mux);if(have)store.acknowledge(ack.boot,ack.seq,ack.uptime);
 controlRead(snapshot);const uint64_t now=snapshot.uptimeMs;const auto boot=eventLogView();if(!boot.bootCount)return;
 const bool online=mqttNasAlive();const bool edge=linkKnown&&online!=wasOnline;linkKnown=true;wasOnline=online;
 if(!online&&!outageId){outageId=store.sequenceCursor();offlineStart=now;offlineBase=0;}
 const uint64_t elapsed=online?0:offlineBase+now-offlineStart;
 const uint16_t nextPeriod=samples::periodSeconds(online,elapsed,INC_SAMPLE_FAST_PHASE_MS,INC_SAMPLE_SLOW_PHASE_MS,INC_SAMPLE_INTERVAL_MS/1000);
 const bool phase=period!=nextPeriod;period=nextPeriod;if(!online&&period>1){compactId=outageId;compactPeriod=period;}
 const bool trusted=networkClock(snapshot.clockSource)&&snapshot.epochMs>=1704067200000ULL&&snapshot.epochMs<=4102444799999ULL;const uint8_t source=trusted?(snapshot.clockSource==ClockSource::Nas?2:1):0;
 const bool due=!lastSample||now-lastSample>=uint64_t(period)*1000;const bool anchor=source&&source!=lastSource;
 const uint16_t state=(snapshot.armed?samples::ARMED:0)|(snapshot.output[0]?samples::OUTPUT1:0)|(snapshot.output[1]?samples::OUTPUT2:0)|(snapshot.config[0].pwm?samples::PWM1:0)|(snapshot.config[1].pwm?samples::PWM2:0);
 bool changed=strcmp(previous.boot,boot.bootId)||((previous.flags^state)&(samples::ARMED|samples::OUTPUT1|samples::OUTPUT2|samples::PWM1|samples::PWM2))||previous.cfg!=snapshot.configRevision;
 for(unsigned i=0;i<2;++i)changed|=previous.mode[i]!=(uint8_t)snapshot.config[i].mode||previous.frequency[i]!=snapshot.config[i].frequency||previous.duty[i]!=snapshot.config[i].duty;
 if(now>=retryAt&&(due||anchor||edge||phase||changed)){samples::Record r={};snprintf(r.boot,sizeof(r.boot),"%s",boot.bootId);r.bootOrder=boot.bootCount;r.uptime=now;r.sensorUptime=snapshot.sampledAtMs<=now?snapshot.sampledAtMs:0;r.epoch=trusted?snapshot.epochMs:0;r.cfg=snapshot.configRevision;r.outageId=online?0:outageId;r.offlineElapsed=elapsed;r.periodSec=period;
  r.temperature=snapshot.temperature;r.humidity=snapshot.humidity;r.flags=state|(source?samples::TRUSTED:0)|(source==2?samples::NAS_TIME:0)|(anchor&&!due&&!changed?samples::ANCHOR:0)|(!online?samples::OUTAGE:0)|((changed||edge||phase||anchor)?samples::IMPORTANT:0);
  const auto photo=photoView();if(snapshot.sensorValid&&now>=snapshot.sampledAtMs&&now-snapshot.sampledAtMs<=10000)r.flags|=samples::SENSOR_VALID;
  for(unsigned i=0;i<2;++i){r.raw[i]=photo.channels[i].raw;r.frequency[i]=snapshot.config[i].frequency;r.duty[i]=snapshot.config[i].duty;r.mode[i]=(uint8_t)snapshot.config[i].mode;if(photo.compiled&&photo.channels[i].valid&&now>=photo.sampledMs&&now-photo.sampledMs<=2000)r.flags|=i?samples::PHOTO2_VALID:samples::PHOTO1_VALID;}
  while(LittleFS.totalBytes()-LittleFS.usedBytes()<65536&&store.reclaim()){}
  if(LittleFS.totalBytes()-LittleFS.usedBytes()>=65536){if(store.append(r)){previous=r;if(due)lastSample=now;lastSource=source;if(online)outageId=offlineBase=offlineStart=0;}else{lastSample=now;retryAt=now+1000;}}
  else{++store.failures;lastSample=now;retryAt=now+1000;}
 }
 // Incremental streaming compaction. Critical records may make the average slightly denser.
 if(compactId&&uint32_t(millis()-lastCompact)>=500){store.compactStep(compactId,compactPeriod);lastCompact=millis();}
 if(uint32_t(millis()-lastSave)>=60000){if(prefsReady){if(savedCovered!=store.covered)prefs.putUInt("covered",store.covered);if(savedThinned!=store.thinned)prefs.putUInt("thinned",store.thinned);if(savedFailures!=store.failures)prefs.putUInt("failures",store.failures);}savedCovered=store.covered;savedThinned=store.thinned;savedFailures=store.failures;lastSave=millis();}static uint32_t lastRefresh=0;if(uint32_t(millis()-lastRefresh)>=1000){refresh();lastRefresh=millis();}
#endif
}
bool sampleJournalJson(char*json,size_t capacity){
#if INC_ENABLE_SAMPLE_JOURNAL
 if(!sampleJournalView().ready||!eventStorageTake())return false;samples::Record r={};bool ok=store.peek(r);eventStorageGive();if(!ok)return false;
 char t[20],h[20];decimal::sample(t,sizeof(t),r.temperature,r.flags&samples::SENSOR_VALID);decimal::sample(h,sizeof(h),r.humidity,r.flags&samples::SENSOR_VALID);auto v=sampleJournalView();
 int n=snprintf(json,capacity,R"JSON({"v":1,"boot_id":"%s","boot_order":%lu,"seq":"%llu","uptime_ms":%llu,"epoch_ms":%llu,"sensor_uptime_ms":%llu,"flags":%u,"cfg":%lu,"temperature":%s,"humidity":%s,"raw":[%u,%u],"frequency":[%u,%u],"duty":[%u,%u],"mode":[%u,%u],"outage_id":"%llu","offline_elapsed_ms":%llu,"period_s":%u,"journal":{"pending":%u,"capacity":%u,"covered":%lu,"thinned":%lu,"failures":%lu}})JSON",r.boot,(unsigned long)r.bootOrder,r.seq,r.uptime,r.epoch,r.sensorUptime,r.flags,(unsigned long)r.cfg,t,h,r.raw[0],r.raw[1],r.frequency[0],r.frequency[1],r.duty[0],r.duty[1],r.mode[0],r.mode[1],r.outageId,r.offlineElapsed,r.periodSec,v.pending,v.capacity,(unsigned long)v.covered,(unsigned long)v.thinned,(unsigned long)v.failures);return n>0&&(size_t)n<capacity;
#else
 (void)json;(void)capacity;return false;
#endif
}
void sampleJournalReceipt(const char*json,bool retained){
#if INC_ENABLE_SAMPLE_JOURNAL
 if(retained||!json)return;Receipt r={};unsigned long long seq=0,uptime=0;int end=0;int n=sscanf(json,"{\"v\":1,\"boot_id\":\"%31[0-9a-f-]\",\"seq\":\"%llu\",\"uptime_ms\":%llu}%n",r.boot,&seq,&uptime,&end);
 if(n!=3||!end||json[end]||!seq)return;r.seq=seq;r.uptime=uptime;portENTER_CRITICAL(&mux);receipt=r;receiptReady=true;portEXIT_CRITICAL(&mux);
#else
 (void)json;(void)retained;
#endif
}
void sampleJournalPrint(){auto v=sampleJournalView();if(Serial)Serial.printf("SAMPLES enabled=%u ready=%u pending=%u capacity=%u interval_ms=%lu covered=%lu thinned=%lu failures=%lu damaged=%lu fs_total=%lu fs_used=%lu\n",v.enabled,v.ready,v.pending,v.capacity,(unsigned long)v.intervalMs,(unsigned long)v.covered,(unsigned long)v.thinned,(unsigned long)v.failures,(unsigned long)v.damaged,(unsigned long)v.flashTotal,(unsigned long)v.flashUsed);}
