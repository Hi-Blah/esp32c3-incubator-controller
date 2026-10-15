#include "mqtt_publish_flow.h"
#include "pwm_result.h"
#include "http_authority.h"
#include <initializer_list>
namespace {
constexpr bool authorityTest(){
 for(const char*s: {"192.168.4.1","192.168.124.106:80","192.0.2.10:18080","255.255.255.255:65535","0.0.0.0:1"})if(!httpaccess::ipv4Authority(s))return false;
 for(const char*s: {"","incubator.test","localhost","192.168.1.1.evil.test","256.1.1.1","1.2.3","1.2.3.4.5","01.2.3.4","1.2.3.4:","1.2.3.4:0","1.2.3.4:65536","1.2.3.4:00080","http://1.2.3.4","user@1.2.3.4","1.2.3.4/path","[::1]","1.2.3.4:80 junk","1.2.3.4\r\nX: value"})if(httpaccess::ipv4Authority(s))return false;
 return !httpaccess::ipv4Authority(nullptr);
}
static_assert(authorityTest(),"LAN/NAT HTTP authority validation failed");
constexpr bool flowTest(){mqttflow::Flow f;if(!f.ready(0,20000,8192,0))return false;f.accepted(7,mqttflow::Kind::Record,42,0,1);if(f.ready(500,20000,8192,0)||f.acknowledged(8)||!f.acknowledged(7)||f.ordinal!=42)return false;if(!f.expired(30000))return false;f.succeeded(10);if(f.ready(100,20000,8192,0)||!f.ready(210,20000,8192,0))return false;f.failed(300);return !f.ready(799,20000,8192,0)&&f.ready(800,20000,8192,0)&&!f.ready(800,8191,8192,0)&&!f.ready(800,20000,2047,0)&&!f.ready(800,20000,8192,1);}
static_assert(flowTest(),"One QoS1 packet, ACK, timeout and backpressure contract failed");
constexpr bool cappedBackoff(){mqttflow::Flow f;for(unsigned i=0;i<20;++i)f.failed(i*30000);return f.retryMs==30000;}
static_assert(cappedBackoff(),"Retry delay must cap at30s");
static_assert(pwmcheck::accepted(0,50,50)&&pwmcheck::accepted(0,20000,20000),"ESP_OK zero is success, not frequency");
static_assert(!pwmcheck::accepted(258,50,50)&&!pwmcheck::accepted(0,0,50)&&!pwmcheck::accepted(0,100,50),"Reject driver failure or wrong readback");
}

namespace {
struct SendStats {unsigned records=0,states=0,progress=0;uint32_t stateGap=0,progressGap=0;};
// A1200ms PUBACK plus the200ms guard leaves realtime streams permanently overdue.
// Run the real picker through64 queued records and optional temporary file-lock misses.
constexpr SendStats traffic(bool fair,uint32_t start,bool fileBusy){
 mqttflow::RoutineScheduler scheduler;SendStats stats;
 uint32_t now=start,lastProgress=start-2000,lastState=start-2000,lastRecord=start-500;
 unsigned remaining=64;
 for(unsigned slot=0;slot<250;++slot){
  const bool pd=static_cast<uint32_t>(now-lastProgress)>=2000;
  const bool sd=static_cast<uint32_t>(now-lastState)>=2000;
  const bool rd=remaining&&static_cast<uint32_t>(now-lastRecord)>=500;
  auto kind=fair?scheduler.choose(pd,sd,rd):pd?mqttflow::Kind::Progress:sd?mqttflow::Kind::State:rd?mqttflow::Kind::Record:mqttflow::Kind::None;
  if(kind==mqttflow::Kind::Record&&fileBusy&&slot%7==0)kind=scheduler.choose(pd,sd,false);
  if(kind==mqttflow::Kind::Progress){auto gap=static_cast<uint32_t>(now-lastProgress);if(gap>stats.progressGap)stats.progressGap=gap;lastProgress=now;++stats.progress;}
  else if(kind==mqttflow::Kind::State){auto gap=static_cast<uint32_t>(now-lastState);if(gap>stats.stateGap)stats.stateGap=gap;lastState=now;++stats.states;}
  else if(kind==mqttflow::Kind::Record){lastRecord=now;--remaining;++stats.records;}
  if(fair)scheduler.accepted(kind);
  now+=1400;
 }
 return stats;
}
constexpr auto priorTraffic=traffic(false,2000,false);
static_assert(priorTraffic.records==0&&priorTraffic.states>0&&priorTraffic.progress>0,"Reproduce backlog starvation while realtime traffic appears healthy");
constexpr auto repairedTraffic=traffic(true,2000,false);
static_assert(repairedTraffic.records==64&&repairedTraffic.states>0&&repairedTraffic.progress>0&&repairedTraffic.stateGap<=4200&&repairedTraffic.progressGap<=4200,"Drain full backlog without starving realtime data under slow PUBACKs");
constexpr auto blockedFiles=traffic(true,2000,true);
static_assert(blockedFiles.records==64&&blockedFiles.states>0&&blockedFiles.progress>0&&blockedFiles.stateGap<=5600&&blockedFiles.progressGap<=5600,"Temporary busy files must not block realtime data or prevent later delivery");
constexpr auto wrappedClock=traffic(true,UINT32_MAX-4000,false);
static_assert(wrappedClock.records==64&&wrappedClock.states==repairedTraffic.states&&wrappedClock.progress==repairedTraffic.progress,"Publishing cadence survives millis rollover");
}
