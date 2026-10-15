#pragma once
#include "light_feedback.h"
namespace autocal {
constexpr unsigned INTERIOR_POINTS=photomodel::FREQUENCIES*(photomodel::DUTIES-2);
struct Point {bool dark=false,full=false,done=false;uint8_t channel=0,frequencyIndex=0,dutyIndex=1;};
constexpr unsigned pointCount(uint8_t mask){return ((mask&1)?INTERIOR_POINTS+2:0)+((mask&2)?INTERIOR_POINTS+2:0);}
constexpr Point point(uint8_t mask,unsigned index){Point p;for(unsigned ch=0;ch<2;++ch)if(mask&(1U<<ch)){const unsigned count=INTERIOR_POINTS+2;if(index>=count){index-=count;continue;}p.channel=ch;p.dark=index==0;p.full=index==1;if(index>1){const unsigned n=index-2;p.frequencyIndex=n/(photomodel::DUTIES-2);p.dutyIndex=1+n%(photomodel::DUTIES-2);}return p;}p.done=true;return p;}
}
