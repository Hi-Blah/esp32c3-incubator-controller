#include "module_config.h"
#include "light_feedback.h"
#include "mqtt_service.h"
#include "event_log.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <cstdio>
#include <cstring>
namespace {portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;PhotoView view={};uint64_t lastSample=0,lastAnalysis[2]={};constexpr uint8_t pins[2]={INC_PHOTO1_PIN,INC_PHOTO2_PIN};
uint64_t nowMs(){return esp_timer_get_time()/1000ULL;}
bool forward(uint8_t ch,const char*op,uint16_t a,uint16_t b,char*reply,unsigned size){if(ch>1){snprintf(reply,size,"ERR channel1|2");return false;}auto v=photoView();auto id=eventLogView();char json[256];snprintf(json,sizeof(json),"{\"v\":1,\"boot\":\"%s\",\"ch\":%u,\"op\":\"%s\",\"a\":%u,\"b\":%u,\"raw\":%u}",id.bootId,ch+1,op,a,b,v.channels[ch].raw);bool ok=mqttAnalysisCommand(json);snprintf(reply,size,ok?"OK queued for NAS; wait for NAS acknowledgement":"ERR NAS/MQTT unavailable; no settings changed");return ok;}}
bool photoStart(){view.compiled=INC_ENABLE_PHOTO;
#if INC_ENABLE_PHOTO
 analogReadResolution(12);for(auto pin:pins){pinMode(pin,INPUT);analogSetPinAttenuation(pin,ADC_11db);}
#endif
 return true;}
void photoSample(bool,bool,uint64_t now,bool,uint16_t,uint8_t,bool,uint16_t,uint8_t){
#if INC_ENABLE_PHOTO
 if(now-lastSample<100)return;lastSample=now;uint16_t raw[2]={static_cast<uint16_t>(analogRead(pins[0])),static_cast<uint16_t>(analogRead(pins[1]))};portENTER_CRITICAL(&mux);view.sampledMs=now;for(unsigned i=0;i<2;i++){view.channels[i].raw=raw[i];view.channels[i].valid=true;if(now-lastAnalysis[i]>15000){view.channels[i].expectedValid=false;view.channels[i].fault=false;view.channels[i].status=9;}}portEXIT_CRITICAL(&mux);
#endif
}
PhotoView photoView(){PhotoView p;portENTER_CRITICAL(&mux);p=view;portEXIT_CRITICAL(&mux);return p;}
bool photoAnalysisFresh(){portENTER_CRITICAL(&mux);const uint64_t last=lastAnalysis[0]<lastAnalysis[1]?lastAnalysis[0]:lastAnalysis[1];portEXIT_CRITICAL(&mux);return last&&nowMs()-last<15000;}
bool photoConfigure(uint8_t ch,const char*op,char*r,unsigned size){if(strcmp(op,"on")&&strcmp(op,"off")&&strcmp(op,"dark")&&strcmp(op,"bright")){snprintf(r,size,"ERR photo on|off|dark|bright");return false;}return forward(ch,op,0,0,r,size);}
bool photoThresholds(uint8_t ch,uint16_t on,uint16_t off,char*r,unsigned size){if(!on||on>off||off>1000){snprintf(r,size,"ERR thresholds1..1000");return false;}return forward(ch,"thresholds",on,off,r,size);}
bool photoDimThreshold(uint8_t ch,uint16_t ratio,char*r,unsigned size){if(!ratio||ratio>1000)return false;return forward(ch,"dim",ratio,0,r,size);}
void photoAutoProgress(const PhotoAutoView&p){portENTER_CRITICAL(&mux);view.automatic=p;portEXIT_CRITICAL(&mux);}
bool photoAcceptAnalysis(const char*json){char boot[40]={};unsigned ch=0,en=0,cal=0,on=0,fault=0,lev=0,exp=0,rat=0,status=0,dark=0,bright=0,onmin=0,offmax=0,dim=0,model=0,reliable=0,known=0;unsigned long long sample=0;int end=0;
 int n=sscanf(json,"{\"v\":1,\"boot\":\"%39[^\"]\",\"sample_ms\":%llu,\"channel\":%u,\"enabled\":%u,\"calibrated\":%u,\"on\":%u,\"fault\":%u,\"level\":%u,\"expected\":%u,\"ratio\":%u,\"status\":%u,\"dark\":%u,\"bright\":%u,\"on_min\":%u,\"off_max\":%u,\"dim_ratio\":%u,\"model_ready\":%u,\"reliable\":%u,\"known\":%u}%n",boot,&sample,&ch,&en,&cal,&on,&fault,&lev,&exp,&rat,&status,&dark,&bright,&onmin,&offmax,&dim,&model,&reliable,&known,&end);
 auto id=eventLogView();auto p=photoView();if(n!=19||json[end]||strcmp(boot,id.bootId)||ch<1||ch>2||sample>p.sampledMs||p.sampledMs-sample>15000||en>1||cal>1||on>1||fault>1||known>1||model>1||reliable>1||lev>1000||exp>1000||rat>10000||status>10||dark>4095||bright>4095||onmin>offmax||offmax>1000||dim>1000)return false;
 portENTER_CRITICAL(&mux);auto &c=view.channels[ch-1];c.enabled=en;c.calibrated=cal;c.on=on;c.fault=fault;c.level=lev;c.expected=exp;c.ratio=rat;c.status=status;c.dark=dark;c.bright=bright;c.onThreshold=onmin;c.offThreshold=offmax;c.dimRatio=dim;c.modelReady=model;c.modelReliable=reliable;c.expectedValid=known;lastAnalysis[ch-1]=nowMs();portEXIT_CRITICAL(&mux);return true;}
const char*photoStatus(uint8_t s){const char*names[]={"UNCONFIGURED","DISABLED","OK","MISSING_LIGHT","DIM_LIGHT","STUCK_ON","MODEL_REQUIRED","CALIBRATING","LOW_DUTY","NAS_UNAVAILABLE","MANUAL_OR_STOPPED"};return s<11?names[s]:"UNKNOWN";}
