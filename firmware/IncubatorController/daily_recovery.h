#pragma once
#include "program_logic.h"
namespace dailyrestart {
constexpr uint32_t SCHEMA=0xC3063121;
struct Preferences {uint32_t schema=SCHEMA;uint8_t mask=3;};
constexpr bool valid(Preferences p){return p.schema==SCHEMA&&p.mask<=3;}
constexpr uint8_t bootMask(Preferences p,const program::Config c[2],bool normalReset){uint8_t result=0;if(normalReset&&valid(p))for(unsigned i=0;i<2;++i)if((p.mask&(1U<<i))&&c[i].mode==program::Mode::Daily)result|=1U<<i;return result;}
constexpr uint8_t cancel(uint8_t pending,unsigned channel){return channel<2?pending&~(1U<<channel):pending;}
constexpr bool canApply(bool clockValid,bool logicPassed,bool storageReady,bool calibration){return clockValid&&logicPassed&&storageReady&&!calibration;}
}
