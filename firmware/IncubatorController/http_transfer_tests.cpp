#include "http_transfer.h"
namespace {
constexpr uint8_t payload[1537]={0x1f,0x8b,0,0xff,0x80,0x7f,0,13,10};
struct Fake {
 uint64_t time=0;size_t received=0,largest=0,calls=0;bool exact=true;
 int scenario=0;uint32_t pauses=0;
 constexpr uint64_t now()const{return time;}
 constexpr bool connected()const{return scenario!=4||received<100;}
  constexpr bool memoryReady(uint32_t free,uint32_t largestBlock)const{return free==httptransfer::HEAP_RESERVE&&largestBlock==httptransfer::LARGEST_RESERVE&&(scenario!=2||time>=6)&&scenario!=7&&(scenario!=8||received<73);}
 constexpr httptransfer::WriteResult write(const uint8_t *p,size_t n){
  ++calls;if(n>largest)largest=n;
  if(scenario==3)return {-1,EAGAIN};
  if(scenario==5)return {-1,EPIPE};
  if(scenario==6&&calls==1)return {-1,ENOMEM};
  const size_t take=n>73?73:n;for(size_t i=0;i<take;++i)if(p[i]!=payload[received+i])exact=false;received+=take;return {int(take),0};
 }
 constexpr void pause(){time+=2;++pauses;}
};
constexpr bool partial(){Fake f={};auto r=httptransfer::transmit(f,payload,sizeof(payload),1000);return r.failure==httptransfer::Failure::None&&r.sent==sizeof(payload)&&f.exact&&f.largest==512&&f.calls>3;}
constexpr bool memoryWait(){Fake f={};f.scenario=2;auto r=httptransfer::transmit(f,payload,sizeof(payload),1000);return r.failure==httptransfer::Failure::None&&f.time>=6&&f.exact;}
constexpr bool timeout(){Fake f={};f.scenario=3;auto r=httptransfer::transmit(f,payload,sizeof(payload),10);return r.failure==httptransfer::Failure::Deadline&&r.sent==0&&f.time==10;}
constexpr bool closed(){Fake f={};f.scenario=4;auto r=httptransfer::transmit(f,payload,sizeof(payload),1000);return r.failure==httptransfer::Failure::Closed&&r.sent==146&&f.exact;}
constexpr bool fatal(){Fake f={};f.scenario=5;auto r=httptransfer::transmit(f,payload,sizeof(payload),1000);return r.failure==httptransfer::Failure::Socket&&r.sent==0&&r.error==EPIPE&&f.calls==1;}
constexpr bool temporaryMemory(){Fake f={};f.scenario=6;auto r=httptransfer::transmit(f,payload,sizeof(payload),1000);return r.failure==httptransfer::Failure::None&&r.sent==sizeof(payload)&&f.exact;}
constexpr bool empty(){Fake f={};auto r=httptransfer::transmit(f,nullptr,0,1000);return r.failure==httptransfer::Failure::None&&r.sent==0&&f.calls==0;}
constexpr bool memoryDeadline(){Fake f={};f.scenario=2;auto r=httptransfer::transmit(f,payload,sizeof(payload),4);return r.failure==httptransfer::Failure::LowMemory&&r.sent==0&&f.calls==0&&f.time==4;}
constexpr bool sustainedMemory(){Fake f={};f.scenario=7;auto r=httptransfer::transmit(f,payload,sizeof(payload),15000);return r.failure==httptransfer::Failure::LowMemory&&r.sent==0&&f.calls==0&&f.time==httptransfer::MEMORY_WAIT_MS;}
constexpr bool pressureMidBody(){Fake f={};f.scenario=8;auto r=httptransfer::transmit(f,payload,sizeof(payload),15000);return r.failure==httptransfer::Failure::LowMemory&&r.sent==73&&f.exact&&f.calls==1&&f.time==httptransfer::MEMORY_WAIT_MS+2;}
static_assert(partial(),"partial sends must preserve binary bytes and never write more than512B");
static_assert(memoryWait(),"wait for reserved heap before writing");
static_assert(timeout(),"blocked socket must stop at deadline");
static_assert(closed(),"do not write after peer closes");
static_assert(fatal(),"fatal socket error must stop without retransmitting accepted bytes");
static_assert(temporaryMemory(),"transient ENOMEM must not drop or duplicate body bytes");
static_assert(empty(),"empty body does not access null payload");
static_assert(memoryDeadline(),"memory wait is bounded and separately reported");
static_assert(sustainedMemory(),"persistent low heap must release the HTTP connection without waiting15seconds");
static_assert(pressureMidBody(),"stop promptly if heap falls during a transfer; preserve the accepted prefix");
static_assert(httptransfer::acceptsGzip("gzip, deflate, br"),"browser gzip negotiation");
static_assert(!httptransfer::acceptsGzip("gzip;q=0, *;q=1"),"explicit refusal overrides wildcard");
static_assert(httptransfer::acceptsGzip("GZip ; Q=0.50"),"case/whitespace/weighted token");
static_assert(!httptransfer::acceptsGzip(nullptr)&&!httptransfer::acceptsGzip("br, deflate")&&!httptransfer::acceptsGzip("gzip;q=0.000"),"plain fallback");
}
