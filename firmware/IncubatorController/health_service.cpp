#include "health_service.h"
#include <Arduino.h>
namespace {HealthView view={};portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;}
HealthView healthView(){HealthView v;portENTER_CRITICAL(&mux);v=view;portEXIT_CRITICAL(&mux);return v;}
void healthUpdate(uint32_t active){portENTER_CRITICAL(&mux);view.active=active;portEXIT_CRITICAL(&mux);}
void healthLatch(uint32_t bit){portENTER_CRITICAL(&mux);view.latched|=bit;portEXIT_CRITICAL(&mux);}
void healthCommandError(const char*text){portENTER_CRITICAL(&mux);++view.commandErrors;snprintf(view.lastCommandError,sizeof(view.lastCommandError),"%s",text);portEXIT_CRITICAL(&mux);}
void healthClear(){portENTER_CRITICAL(&mux);view.latched=0;view.lastCommandError[0]=0;portEXIT_CRITICAL(&mux);}

const char*healthText(uint32_t b){if(b&FAULT_CONTROLLER)return "CONTROLLER FAILED";if(b&FAULT_PWM)return "PWM FAILED";if(b&FAULT_NVS)return "SAVE FAILED";if(b&FAULT_MEMORY)return "MEMORY LOW";if(b&FAULT_PHOTO1)return "L1 LIGHT MISMATCH";if(b&FAULT_PHOTO2)return "L2 LIGHT MISMATCH";if(b&FAULT_WIFI)return "WIFI OFFLINE";if(b&FAULT_MQTT)return "MQTT OFFLINE";if(b&FAULT_DHT)return "DHT INVALID";if(b&FAULT_LOG)return "LOG STORAGE OFF";if(b&FAULT_LOG_WRITE)return "LOG IO FAILURE";if(b&FAULT_LOG_BACKLOG)return "LOG QUEUE FULL";if(b&FAULT_SHORT_PULSE)return "PWM SHORT PULSE";return "NO ACTIVE FAULT";}
