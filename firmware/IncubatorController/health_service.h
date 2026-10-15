#pragma once
#include <stdint.h>
enum HealthBit:uint32_t {FAULT_DHT=1,FAULT_WIFI=2,FAULT_MQTT=4,FAULT_LOG=8,FAULT_MEMORY=16,FAULT_PWM=32,FAULT_NVS=64,FAULT_CONTROLLER=128,FAULT_PHOTO1=256,FAULT_PHOTO2=512,FAULT_SHORT_PULSE=1024,FAULT_LOG_BACKLOG=2048,FAULT_LOG_WRITE=4096};
struct HealthView {uint32_t active=0,latched=0,commandErrors=0;char lastCommandError[80]={};};
HealthView healthView();void healthUpdate(uint32_t active);void healthLatch(uint32_t bit);void healthCommandError(const char*text);void healthClear();

const char*healthText(uint32_t bits);
