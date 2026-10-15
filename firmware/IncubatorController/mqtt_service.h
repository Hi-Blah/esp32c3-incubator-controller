#pragma once
#include "control_service.h"
struct MqttView {
  bool enabled, connected, credentialsSaved, nasAlive;
  uint16_t port;
  uint32_t retainedCommandsRejected, connectionAttempts, outboxBytes;
  uint32_t publishFailures, memoryDeferrals, inflightId, stackMinimum, heapBefore, heapAfterInit, heapAfterStart, heapAfterConnect;
  uint32_t waitHeap, waitLargest, currentHeap, currentLargest, retryMs;
  int lastError, errorType, transportError, socketError;
  char waitReason[24];
  char host[97], username[33], lastReply[96];
};
struct MqttLifecycle {uint32_t creations,memoryStops,ackTimeoutStops;char lastStop[24];};
MqttLifecycle mqttLifecycleView();
bool mqttStart();
void mqttService();
MqttView mqttView();
bool mqttConfigure(bool enabled,const char *host,uint16_t port,const char *username,const char *password,char *error,size_t size);
void mqttAnswer(const ControlReply &reply);

void mqttRequestProbe();

bool mqttAnalysisCommand(const char*json);
bool mqttNasAlive();
