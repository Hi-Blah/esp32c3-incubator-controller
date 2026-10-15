#pragma once
#include <Arduino.h>
#include "program_logic.h"
enum class ClockSource : uint8_t { Unsynced, Manual, Ntp, Nas };
inline constexpr bool networkClock(ClockSource c){return c==ClockSource::Ntp||c==ClockSource::Nas;}
inline constexpr const char* clockSourceName(ClockSource c){return c==ClockSource::Ntp?"NTP":c==ClockSource::Nas?"NAS":c==ClockSource::Manual?"MANUAL":"UNSYNC";}
// Validated current-boot NAS heartbeat, consumed by the controller task.
void controlNasTime(uint64_t echoedUptime,uint64_t receivedEpoch);
enum class NetworkAction : uint8_t { Restart, Info, OpenAp, Forget, Secrets, RadioInfo, SleepOn, SleepOff };
enum class Origin : uint8_t { Web, Ble, Usb, Button, Mqtt, Recovery };
struct RecoveryPolicy {
  bool enabled, countOutage;
  uint32_t restartAfterSeconds, checkpointSeconds;
};
struct ControlSnapshot {
  program::Config config[2];
  program::Progress progress[2];
  bool output[2], armed, logicPassed, storageReady, sensorValid, oledReady;
  bool resumeWaiting, recovered;
  uint8_t limit;
  ClockSource clockSource;
  RecoveryPolicy recovery;
  RecoveryPolicy channelRecovery[2];
  bool channelManualRecovery[2],channelNasRecovery[2],channelEnabled[2],channelDailyRecovery[2],channelDailyWaiting[2];
  uint64_t channelGapMs[2],channelOutageMs[2],channelStartupMs[2];
  char channelRecoverySource[2][16];
  float temperature, humidity;
  uint64_t uptimeMs, sampledAtMs, sampleEpochMs, checkpointEpochMs, recoveryGapMs;
  bool sampleTimeTrusted;
  char clockText[24];
  uint32_t configRevision;
  uint64_t epochMs, estimatedOutageMs, startupWaitMs;
  bool nasRecoveryEnabled, nasWaiting, cycleRunning[2], manualRecoveryEnabled;
  uint32_t actualFrequency[2];uint64_t transitions[2],lastSwitchMs[2];
  char recoveryReq[17], previousBoot[40], recoverySource[16];
};
struct NetworkView {
  bool ready, connected, apActive, configured, credentialsSaved;
  int16_t rssi;
  uint32_t generation,disconnectReason,httpRestarts;
  uint32_t httpStackMinimum, stateRequests, commandTimeouts, responseWriteFailures;
  uint32_t homeRequests,homeFailures,homeLastMs,homeLastSent;uint8_t httpLastFailure;int httpLastErrno;
  char ip[16], apIp[16], apName[25], ssid[33];
};
struct ControlCommand {
  Origin origin;
  uint32_t id;
  uint64_t expiresMs;
  char text[512];
};
struct ControlReply {
  uint32_t id;
  bool ok;
  char text[768];
};
struct BleView { bool ready, connected, secure, advertisingPaused; uint32_t passkey; char name[25]; };

bool controlEnqueue(const ControlCommand &command);
ControlSnapshot controlSnapshot();
void controlRead(ControlSnapshot &out);
void controlSensor(float temperature, float humidity, bool valid, bool oledReady);
bool networkStart();
NetworkView networkView();
void networkAnswer(const ControlReply &reply);
bool networkTakeTimeSync();
bool networkRequestAction(NetworkAction action);
bool bleStart();
void bleSetApClientActive(bool active);
BleView bleView();
void bleAnswer(const ControlReply &reply);

bool networkHttpBusy();
