#pragma once
#include "json_stream.h"
#include "control_service.h"
#include "mqtt_service.h"
#include "event_log.h"
#include "health_service.h"
#include "light_feedback.h"
#include "photo_json.h"
namespace webstate {
struct Input {
  const ControlSnapshot &s;const BleView &b;const MqttView &m;const EventLogView &d;
  const char *csrf,*temperature,*humidity,*ssid,*ip,*apIp;
  bool wifi,apActive;uint8_t profileCount;const char *profiles[8];
  uint32_t heapFree,heapMin,heapLargest;
  HealthView health={};PhotoView photo={};
  uint32_t generation=0,disconnectReason=0,httpRestarts=0;
};
constexpr const char* mode(program::Mode m){return m==program::Mode::Daily?"DAILY":m==program::Mode::Cycle?"CYCLE":"MANUAL";}
template<class J> constexpr void emit(J &j,const Input &in){
 const auto&s=in.s;const auto&b=in.b;const auto&m=in.m;const auto&d=in.d;
 j.raw("{\"ok\":true,\"firmware\":\"Connected-2.2.3\",\"csrf\":").quoted(in.csrf);
 j.raw(",\"clock\":").quoted(s.clockText).raw(",\"clockSource\":").quoted(clockSourceName(s.clockSource));
 j.raw(",\"armed\":").boolean(s.armed).raw(",\"limit\":").number(s.limit).raw(",\"logicPassed\":").boolean(s.logicPassed).raw(",\"storageReady\":").boolean(s.storageReady);
 j.raw(",\"temperature\":").raw(in.temperature).raw(",\"humidity\":").raw(in.humidity).raw(",\"uptimeSeconds\":").number(s.uptimeMs/1000);
 j.raw(",\"uptimeMs\":").number(s.uptimeMs).raw(",\"epochMs\":").number(s.epochMs);
 j.raw(",\"sampledAtMs\":").number(s.sampledAtMs).raw(",\"sampleEpochMs\":").number(s.sampleEpochMs).raw(",\"sampleTimeTrusted\":").boolean(s.sampleTimeTrusted);
 j.raw(",\"health\":{\"active\":").number(in.health.active).raw(",\"latched\":").number(in.health.latched).raw(",\"lastError\":").quoted(in.health.lastCommandError).raw("}");
 j.raw(",\"photo\":");photojson::emit(j,in.photo);
 j.raw(",\"wifi\":{\"connected\":").boolean(in.wifi).raw(",\"ssid\":").quoted(in.ssid).raw(",\"ip\":").quoted(in.ip).raw(",\"apActive\":").boolean(in.apActive).raw(",\"apIp\":").quoted(in.apIp).raw(",\"generation\":").number(in.generation).raw(",\"disconnectReason\":").number(in.disconnectReason).raw(",\"httpRestarts\":").number(in.httpRestarts).raw(",\"profiles\":[");
 for(uint8_t i=0;i<in.profileCount;++i){if(i)j.raw(",");j.quoted(in.profiles[i]);}j.raw("]}");
 j.raw(",\"ble\":{\"ready\":").boolean(b.ready).raw(",\"connected\":").boolean(b.connected).raw(",\"secure\":").boolean(b.secure).raw(",\"advertisingPaused\":").boolean(b.advertisingPaused).raw(",\"name\":").quoted(b.name).raw("}");
 j.raw(",\"recovery\":{\"enabled\":").boolean(s.recovery.enabled).raw(",\"countOutage\":").boolean(s.recovery.countOutage).raw(",\"restartAfterSeconds\":").number(s.recovery.restartAfterSeconds).raw(",\"checkpointSeconds\":").number(s.recovery.checkpointSeconds).raw(",\"waiting\":").boolean(s.resumeWaiting).raw(",\"recovered\":").boolean(s.recovered).raw(",\"checkpointEpochMs\":").number(s.checkpointEpochMs).raw(",\"gapMs\":").number(s.recoveryGapMs).raw(",\"manualEnabled\":").boolean(s.manualRecoveryEnabled).raw(",\"nasEnabled\":").boolean(s.nasRecoveryEnabled).raw(",\"nasWaiting\":").boolean(s.nasWaiting).raw(",\"source\":").quoted(s.recoverySource).raw(",\"estimatedOutageMs\":").number(s.estimatedOutageMs).raw(",\"startupWaitMs\":").number(s.startupWaitMs).raw("},\"lights\":[");
 for(uint8_t ch=0;ch<2;++ch){if(ch)j.raw(",");const auto &c=s.config[ch];const auto &p=s.progress[ch];
  j.raw("{\"enabled\":").boolean(s.channelEnabled[ch]).raw(",\"recovery\":{\"enabled\":").boolean(s.channelRecovery[ch].enabled).raw(",\"dailyEnabled\":").boolean(s.channelDailyRecovery[ch]).raw(",\"dailyWaiting\":").boolean(s.channelDailyWaiting[ch]).raw(",\"manualEnabled\":").boolean(s.channelManualRecovery[ch]).raw(",\"nasEnabled\":").boolean(s.channelNasRecovery[ch]).raw(",\"countOutage\":").boolean(s.channelRecovery[ch].countOutage).raw(",\"restartAfterSeconds\":").number(s.channelRecovery[ch].restartAfterSeconds).raw(",\"checkpointSeconds\":").number(s.channelRecovery[ch].checkpointSeconds).raw(",\"source\":").quoted(s.channelRecoverySource[ch]).raw(",\"gapMs\":").number(s.channelGapMs[ch]).raw(",\"estimatedOutageMs\":").number(s.channelOutageMs[ch]).raw(",\"startupWaitMs\":").number(s.channelStartupMs[ch]).raw("},\"output\":").boolean(s.output[ch]).raw(",\"requested\":").boolean(p.requested).raw(",\"mode\":").quoted(mode(c.mode)).raw(",\"finished\":").boolean(p.finished).raw(",\"row\":").number(p.row).raw(",\"completed\":").number(p.completed).raw(",\"remainingMs\":").number(p.remainingMs).raw(",\"repeats\":").number(c.repeats).raw(",\"daily\":[");
  for(uint8_t i=0;i<c.dailyCount;++i){if(i)j.raw(",");j.raw("[").number(c.daily[i].on).raw(",").number(c.daily[i].off).raw("]");}j.raw("],\"steps\":[");
  for(uint8_t i=0;i<c.stepCount;++i){if(i)j.raw(",");j.raw("[").number(c.steps[i].onMs).raw(",").number(c.steps[i].offMs).raw("]");}j.raw("],\"pwm\":").boolean(c.pwm).raw(",\"actualFrequency\":").number(s.actualFrequency[ch]).raw(",\"frequency\":").number(c.frequency).raw(",\"duty\":").number(c.duty).raw("}");
 }j.raw("]");
 j.raw(",\"mqtt\":{\"enabled\":").boolean(m.enabled).raw(",\"connected\":").boolean(m.connected).raw(",\"nasAlive\":").boolean(m.nasAlive).raw(",\"credentialsSaved\":").boolean(m.credentialsSaved).raw(",\"host\":").quoted(m.host).raw(",\"port\":").number(m.port).raw(",\"username\":").quoted(m.username).raw(",\"lastReply\":").quoted(m.lastReply).raw(",\"waitReason\":").quoted(m.waitReason).raw(",\"retryMs\":").number(m.retryMs).raw(",\"waitHeap\":").number(m.waitHeap).raw(",\"waitLargest\":").number(m.waitLargest).raw(",\"lastError\":");if(m.lastError<0){j.raw("-").number(static_cast<uint64_t>(-static_cast<int64_t>(m.lastError)));}else j.number(m.lastError);j.raw("}");
 j.raw(",\"diagnostics\":{\"bootId\":").quoted(d.bootId).raw(",\"resetReason\":").quoted(d.resetReason).raw(",\"bootCount\":").number(d.bootCount).raw(",\"logReady\":").boolean(d.ready).raw(",\"pendingEvents\":").number(d.pending).raw(",\"droppedEvents\":").number(d.dropped).raw(",\"writeFailures\":").number(d.writeFailures).raw(",\"logFailureStage\":").quoted(d.lastFailure).raw(",\"logFailureAtMs\":").number(d.lastFailureAtMs).raw(",\"logFailureErrno\":");if(d.lastError<0)j.raw("-").number(-static_cast<int64_t>(d.lastError));else j.number(d.lastError);j.raw(",\"heapFree\":").number(in.heapFree).raw(",\"heapMin\":").number(in.heapMin).raw(",\"heapLargest\":").number(in.heapLargest).raw("}}");
}
}
