#include "program_logic.h"
#include <initializer_list>
// These assertions execute the actual timing engine during target compilation.
// They cover edges, clock gaps, whole-program repeats and channel arbitration.
namespace {
constexpr bool timingTests(){
  if(program::resumeOffset(600,500,true,0)!=1100 || program::resumeOffset(600,500,false,0)!=600)return false;
  if(program::resumeOffset(600,3600000,true,3600)!=3600600 || program::resumeOffset(600,3600001,true,3600)!=0)return false;
  auto c=program::defaults();program::Runtime r={false,true,1000,0};
  c.mode=program::Mode::Daily;c.dailyCount=2;c.daily[0]={25200,28800};c.daily[1]={32400,46800};
  for(uint32_t sec: {25199UL,25200UL,28799UL,28800UL,32399UL,32400UL,46799UL,46800UL}){
    const bool expected=(sec>=25200&&sec<28800)||(sec>=32400&&sec<46800);
    if(program::evaluate(c,r,1000,true,sec).requested!=expected)return false;
  }
  if(program::evaluate(c,r,1000,false,25200).requested)return false;
  c.daily[1]={86399,1};if(!program::valid(c)||!program::inside(0,c.daily[1])||program::inside(1,c.daily[1]))return false;
  c.daily[0]={0,2};if(program::valid(c))return false; // Wrapped overlap.
  c=program::defaults();c.mode=program::Mode::Cycle;c.stepCount=2;c.repeats=2;
  c.steps[0]={100,200};c.steps[1]={300,400};
  if(!program::evaluate(c,r,1099,false,0).requested||program::evaluate(c,r,1100,false,0).requested)return false;
  if(!program::evaluate(c,r,1300,false,0).requested||program::evaluate(c,r,1600,false,0).requested)return false;
  if(program::evaluate(c,r,2999,false,0).finished||!program::evaluate(c,r,3000,false,0).finished)return false;
  r.offsetMs=2000;if(!program::evaluate(c,r,1000,false,0).finished||program::evaluate(c,r,1000,false,0).requested)return false;
  r.offsetMs=600;if(program::evaluate(c,r,1000,false,0).requested)return false;
  c.repeats=0;r.anchorMs=4294967290ULL;r.offsetMs=0;if(!program::evaluate(c,r,r.anchorMs+5000000000000ULL,false,0).requested)return false;
  c.steps[0].onMs=50;if(program::valid(c))return false;
  c.steps[0]={program::MAX_MS,program::MAX_MS};c.steps[1]=c.steps[0];
  if(program::period(c)!=2419200000ULL||!program::valid(c))return false;
  for(uint8_t want=0;want<4;++want)for(uint8_t old=0;old<4;++old){
    bool q[2]={bool(want&1),bool(want&2)},o[2]={bool(old&1),bool(old&2)},out[2]={};
    program::limited(q,o,false,2,out);if(out[0]||out[1])return false;
    program::limited(q,o,true,1,out);if((out[0]&&out[1])||(out[0]&&!q[0])||(out[1]&&!q[1]))return false;
    if(want&&!out[0]&&!out[1])return false;
    program::limited(q,o,true,2,out);if(out[0]!=q[0]||out[1]!=q[1])return false;
  }
  return true;
}
static_assert(timingTests(),"Advanced timing / power limit regression failed");
}
