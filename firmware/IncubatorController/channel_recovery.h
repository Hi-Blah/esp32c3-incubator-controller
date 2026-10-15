#pragma once
#include "control_service.h"
namespace channelpolicy {
struct Preferences {RecoveryPolicy cycle;bool manualEnabled,nasEnabled;};
struct Saved {uint32_t schema;Preferences channel[2];};
constexpr uint32_t SCHEMA=0xC3063111;
constexpr bool valid(const Preferences&p){return p.cycle.checkpointSeconds>=10&&p.cycle.checkpointSeconds<=3600&&p.cycle.restartAfterSeconds<=604800;}
constexpr uint32_t checkpointInterval(const Preferences p[2],const bool running[2]){uint32_t result=3600;bool any=false;for(unsigned i=0;i<2;++i)if(running[i]&&p[i].cycle.enabled){any=true;if(p[i].cycle.checkpointSeconds<result)result=p[i].cycle.checkpointSeconds;}return any?result:60;}
constexpr uint8_t resumeMask(const Preferences p[2],bool authorized,const bool running[2],const program::Config c[2]){uint8_t result=0;if(authorized)for(unsigned i=0;i<2;++i)if(p[i].cycle.enabled&&running[i]&&c[i].mode==program::Mode::Cycle)result|=1U<<i;return result;}
constexpr bool anyCycle(const Preferences p[2]){return p[0].cycle.enabled||p[1].cycle.enabled;}
constexpr bool anyManual(const Preferences p[2]){return p[0].manualEnabled||p[1].manualEnabled;}
constexpr bool anyNAS(const Preferences p[2],uint8_t waiting){return (waiting&1&&p[0].nasEnabled)||(waiting&2&&p[1].nasEnabled);}
}
