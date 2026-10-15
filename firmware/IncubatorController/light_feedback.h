#pragma once
#include <stdint.h>
#include "photo_model.h"
struct PhotoChannel {bool enabled=false,calibrated=false,valid=false,on=false,fault=false,expectedValid=false,modelReady=false,modelReliable=false;uint16_t raw=0,level=0,dark=0,bright=0,onThreshold=10,offThreshold=50,dimRatio=600,expected=0,ratio=0;uint8_t status=0;};
struct PhotoAutoView {bool active=false;uint8_t channel=0,stage=0;uint16_t completed=0,total=0,frequency=0;uint8_t duty=0;char message[72]={};};
struct PhotoView {bool compiled=false;uint64_t sampledMs=0;PhotoChannel channels[2];PhotoAutoView automatic={};};
bool photoStart();void photoSample(bool command1,bool command2,uint64_t now,bool pwm1=false,uint16_t hz1=50,uint8_t duty1=50,bool pwm2=false,uint16_t hz2=50,uint8_t duty2=50);PhotoView photoView();
bool photoConfigure(uint8_t channel,const char*operation,char*reply,unsigned size);
bool photoThresholds(uint8_t channel,uint16_t onMin,uint16_t offMax,char*reply,unsigned size);
bool photoDimThreshold(uint8_t channel,uint16_t ratio,char*reply,unsigned size);
void photoAutoProgress(const PhotoAutoView &progress);
const char *photoStatus(uint8_t status);
bool photoAcceptAnalysis(const char*json);
bool photoAnalysisFresh();
