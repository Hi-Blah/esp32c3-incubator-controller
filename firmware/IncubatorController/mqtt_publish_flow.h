#pragma once
#include <stdint.h>
namespace mqttflow {
enum class Kind:uint8_t{None,Discovery,Availability,State,Record,Progress,RecoveryRequest,Config,AnalysisCommand,Sample};
// Only routine traffic rotates; boot recovery and discovery retain their higher priority.
// A continuously due state/progress stream cannot consume every slot before a record.
struct RoutineScheduler {
 uint8_t turn=0;
 constexpr Kind choose(bool progressDue,bool stateDue,bool recordDue,bool sampleDue=false)const{
  const bool due[4]={progressDue,stateDue,recordDue,sampleDue};
  const Kind kinds[4]={Kind::Progress,Kind::State,Kind::Record,Kind::Sample};
  for(uint8_t i=0;i<4;++i){const uint8_t index=(turn+i)%4;if(due[index])return kinds[index];}
  return Kind::None;
 }
 constexpr void accepted(Kind kind){
  if(kind==Kind::Progress)turn=1;else if(kind==Kind::State)turn=2;else if(kind==Kind::Record)turn=3;else if(kind==Kind::Sample)turn=0;
 }
};
struct Flow {Kind kind=Kind::None;int id=0;uint64_t ordinal=0;uint32_t sentAt=0,nextAt=0,epoch=0,retryMs=500;
 constexpr bool ready(uint32_t now,uint32_t free,uint32_t largest,uint32_t outbox)const{return id==0&&static_cast<int32_t>(now-nextAt)>=0&&free>=8192&&largest>=2048&&outbox==0;}
 constexpr void accepted(int message,Kind k,uint64_t order,uint32_t now,uint32_t generation){id=message;kind=k;ordinal=order;sentAt=now;epoch=generation;retryMs=500;}
 constexpr bool acknowledged(int message)const{return id>0&&message==id;}
 constexpr void clear(){id=0;kind=Kind::None;ordinal=0;}
 constexpr void succeeded(uint32_t now){clear();nextAt=now+200;retryMs=500;}
 constexpr void failed(uint32_t now){nextAt=now+retryMs;if(retryMs<30000)retryMs=retryMs>15000?30000:retryMs*2;}
 constexpr bool expired(uint32_t now)const{return id>0&&static_cast<uint32_t>(now-sentAt)>=30000;}
};
}
