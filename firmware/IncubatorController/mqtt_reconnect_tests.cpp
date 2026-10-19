#include "mqtt_publish_flow.h"
namespace {
using namespace mqttflow;
constexpr bool queuedAckWins(){
 Flow f;f.accepted(7,Kind::Record,42,1000,1);
 // Simulate a delayed main loop: the worker has already received this PUBACK.
 if(!f.expired(32000)||f.timeout(32000,true,7)||!f.timeout(32000,true,8))return false;
 f.succeeded(32000);return !f.timeout(64000,true,0);
}
static_assert(queuedAckWins(),"A pending valid ACK wins over an elapsed loop deadline");
constexpr bool retransmittedRecord(){
 Flow f;Session s;s.connected(1,1000);f.accepted(9,Kind::Record,123,2000,1);
 if(f.timeout(50000,false,0))return false;
 if(s.connected(2,60000))f.reconnected(60000);
 if(f.id!=9||f.ordinal!=123||f.epoch!=1||f.timeout(60000,true,0)||f.timeout(89999,true,0))return false;
 // An HA birth announcement is not a TCP reconnect and must not renew the ACK window.
 s.rediscover(89999);if(s.connected(2,89999))f.reconnected(89999);
 if(!f.timeout(90000,true,0)||!f.acknowledged(9))return false;
 s.acknowledged(Kind::Availability,0,1,90000);
 return !s.announced&&f.ordinal==123;
}
static_assert(retransmittedRecord(),"Reconnect retains a record until ACK, without accepting stale online ACKs");
constexpr bool flappingIsBounded(){
 Flow f;f.accepted(5,Kind::Record,77,0,1);
 for(uint32_t now=20000;now<180000;now+=20000){f.reconnected(now);if(f.timeout(now,true,0))return false;}
 f.reconnected(180000);
 return f.timeout(180000,true,0)&&!f.timeout(180000,true,5)&&f.ordinal==77;
}
static_assert(flappingIsBounded(),"Repeated short connections cannot postpone an unacknowledged packet forever");
constexpr bool shortConnections(){
 Session s;
 // Only one discovery packet fits each connection, while live/bootstrap data
 // must become usable on every connection, before all thirteen entities exist.
 for(uint32_t generation=1;generation<=DISCOVERY_COUNT;++generation){
  const uint32_t now=generation*60000;
  if(!s.connected(generation,now)||!s.statePending||s.bootstrap(true)!=Kind::Availability)return false;
  s.acknowledged(Kind::Availability,0,generation,now+100);
  if(s.bootstrap(true)!=Kind::Config||s.discoveryDue(now+2000))return false;
  s.statePending=false;
  if(s.bootstrap(false)!=Kind::None||s.discoveryDue(now+1999)||!s.discoveryDue(now+2000))return false;
  if(s.discoveryIndex!=generation-1)return false;
  // The PUBACK may arrive after the TCP link reconnects; the unchanged ordinal
  // still represents the retained discovery config we actually sent.
  s.acknowledged(Kind::Discovery,generation,generation-1,now+2500);
 }
 if(s.discoveryIndex!=DISCOVERY_COUNT)return false;
 s.rediscover(900000);return s.discoveryIndex==0&&s.announced&&!s.statePending;
}
static_assert(shortConnections(),"Short sessions restore live operation and eventually finish all discovery entries");
constexpr bool backgroundFair(){
 RoutineScheduler r;unsigned count[5]={};
 for(unsigned i=0;i<50;++i){const auto k=r.choose(true,true,true,true,true);
  if(k==Kind::Progress)++count[0];else if(k==Kind::State)++count[1];else if(k==Kind::Record)++count[2];else if(k==Kind::Sample)++count[3];else if(k==Kind::Discovery)++count[4];else return false;
  r.accepted(k);
 }
 for(unsigned i=0;i<5;++i)if(count[i]!=10)return false;
 return true;
}
static_assert(backgroundFair(),"Discovery cannot starve progress, events or Flash samples, or be starved by them");
constexpr bool watchdogWrap(){
 DisconnectWatch w;const uint32_t start=UINT32_MAX-10000;
 if(w.expired(false,start)||w.expired(false,start+119999)||!w.expired(false,start+120000))return false;
 if(w.expired(true,start+120001)||w.expired(false,start+120002)||w.expired(false,start+240001))return false;
 Session s;s.connected(1,UINT32_MAX-1000);s.announced=true;s.statePending=false;
 return !s.discoveryDue(998)&&s.discoveryDue(999)&&w.expired(false,start+240002);
}
static_assert(watchdogWrap(),"Reconnect cooldown and discovery deadlines survive millis rollover");
}
