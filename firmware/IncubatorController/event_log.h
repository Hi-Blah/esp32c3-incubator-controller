#pragma once
#include "control_service.h"
struct EventPacket { uint64_t ordinal; char json[1024]; };
struct EventLogView {
  bool ready; uint32_t bootCount, watchdogStarts, powerLikeStarts, dropped, writeFailures, stackMinimum;
  int lastError; char lastFailure[16];uint32_t lastFailureAtMs;
  uint16_t pending; char bootId[40], resetReason[24];
};
bool eventLogStart();
void eventEmit(const char *kind,const char *detail,const ControlSnapshot &snapshot,bool preserve);
EventLogView eventLogView();
bool eventPeek(EventPacket &packet);
bool eventPeekJson(char *json,size_t capacity,uint64_t &ordinal);
void eventAck(uint64_t ordinal);
void eventLogInitialize();
void eventLogPrint();

bool eventCalibrationPoint(const char*run,uint8_t channel,uint16_t frequency,uint8_t duty,uint16_t raw,uint16_t crossRaw,const ControlSnapshot&snapshot,uint32_t rawSum,uint32_t crossSum,uint16_t count);
