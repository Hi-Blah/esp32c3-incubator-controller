#pragma once
#include <stdint.h>
namespace mqttflow {
enum class Kind:uint8_t{None,Discovery,Availability,State,Record,Progress,RecoveryRequest,Config,AnalysisCommand,Sample};
// Routine traffic, including paced discovery, rotates after connection bootstrap.
// A continuously due state/progress stream cannot consume every slot before a record.
struct RoutineScheduler {
 uint8_t turn=0;
 constexpr Kind choose(bool progressDue,bool stateDue,bool recordDue,bool sampleDue=false,bool discoveryDue=false)const{
  const bool due[5]={progressDue,stateDue,recordDue,sampleDue,discoveryDue};
  const Kind kinds[5]={Kind::Progress,Kind::State,Kind::Record,Kind::Sample,Kind::Discovery};
  for(uint8_t i=0;i<5;++i){const uint8_t index=(turn+i)%5;if(due[index])return kinds[index];}
  return Kind::None;
 }
 constexpr void accepted(Kind kind){
  if(kind==Kind::Progress)turn=1;else if(kind==Kind::State)turn=2;else if(kind==Kind::Record)turn=3;else if(kind==Kind::Sample)turn=4;else if(kind==Kind::Discovery)turn=0;
 }
};
struct Flow {Kind kind=Kind::None;int id=0;uint64_t ordinal=0;uint32_t sentAt=0,acceptedAt=0,nextAt=0,epoch=0,retryMs=500;
 constexpr bool ready(uint32_t now,uint32_t free,uint32_t largest,uint32_t outbox)const{return id==0&&static_cast<int32_t>(now-nextAt)>=0&&free>=8192&&largest>=2048&&outbox==0;}
 constexpr void accepted(int message,Kind k,uint64_t order,uint32_t now,uint32_t generation){id=message;kind=k;ordinal=order;sentAt=acceptedAt=now;epoch=generation;retryMs=500;}
 constexpr bool acknowledged(int message)const{return id>0&&message==id;}
 constexpr void clear(){id=0;kind=Kind::None;ordinal=0;}
 constexpr void succeeded(uint32_t now){clear();nextAt=now+200;retryMs=500;}
 constexpr void failed(uint32_t now){nextAt=now+retryMs;if(retryMs<30000)retryMs=retryMs>15000?30000:retryMs*2;}
 constexpr bool expired(uint32_t now)const{return id>0&&(static_cast<uint32_t>(now-sentAt)>=30000||static_cast<uint32_t>(now-acceptedAt)>=180000);}
 // A reconnect does not acknowledge or discard a durable record. Its retransmission
 // gets a full ACK window; the original epoch still identifies stale bootstrap ACKs.
 constexpr void reconnected(uint32_t now){if(id>0)sentAt=now;}
 constexpr bool timeout(uint32_t now,bool online,int receivedAck)const{return online&&!acknowledged(receivedAck)&&expired(now);}
};
constexpr uint8_t DISCOVERY_COUNT=13;
struct Session {
 uint32_t epoch=0,nextDiscoveryAt=0;uint8_t discoveryIndex=0;
 bool announced=false,statePending=false;
 constexpr bool connected(uint32_t generation,uint32_t now){
  if(epoch==generation)return false;
  epoch=generation;announced=false;statePending=true;rediscover(now);return true;
 }
 constexpr void rediscover(uint32_t now){
  // Keep a partial sweep across short connections, rather than repeatedly sending
  // only its first entity. Completed sweeps restart after a connection/HA restart.
  if(discoveryIndex>=DISCOVERY_COUNT)discoveryIndex=0;
  nextDiscoveryAt=now+2000;
 }
 constexpr void acknowledged(Kind kind,uint64_t ordinal,uint32_t sentEpoch,uint32_t now){
  if(kind==Kind::Availability&&sentEpoch==epoch)announced=true;
  if(kind==Kind::Discovery&&ordinal==uint64_t(discoveryIndex)+1){++discoveryIndex;nextDiscoveryAt=now+2000;}
 }
 constexpr Kind bootstrap(bool configurationDue)const{return !announced?Kind::Availability:configurationDue?Kind::Config:Kind::None;}
 constexpr bool discoveryDue(uint32_t now)const{return announced&&!statePending&&discoveryIndex<DISCOVERY_COUNT&&static_cast<int32_t>(now-nextDiscoveryAt)>=0;}
};
// Let the SDK's 15s reconnect loop make several attempts before rebuilding its
// task/transport. This watchdog remains bounded, independent of a pending PUBACK.
struct DisconnectWatch {
 uint32_t since=0;bool waiting=false;
 constexpr bool expired(bool online,uint32_t now){
  if(online){waiting=false;return false;}
  if(!waiting){since=now;waiting=true;}
  return static_cast<uint32_t>(now-since)>=120000;
 }
};
}
