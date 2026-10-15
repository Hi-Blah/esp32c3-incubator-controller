#include "daily_recovery.h"
#include "log_health.h"
namespace{
constexpr bool dailyTest(){program::Config c[2]={program::defaults(),program::defaults()};c[0].mode=program::Mode::Daily;c[1].mode=program::Mode::Daily;dailyrestart::Preferences p;return dailyrestart::bootMask(p,c,true)==3&&dailyrestart::bootMask(p,c,false)==0&&dailyrestart::bootMask({dailyrestart::SCHEMA,2},c,true)==2&&dailyrestart::cancel(3,0)==2&&dailyrestart::canApply(true,true,true,false)&&!dailyrestart::canApply(false,true,true,false)&&!dailyrestart::canApply(true,true,true,true);}
constexpr bool mixedModes(){program::Config c[2]={program::defaults(),program::defaults()};c[0].mode=program::Mode::Daily;c[1].mode=program::Mode::Cycle;dailyrestart::Preferences p;const bool dailyOnly=dailyrestart::bootMask(p,c,true)==1;c[0].mode=program::Mode::Manual;return dailyOnly&&dailyrestart::bootMask(p,c,true)==0&&!dailyrestart::valid({dailyrestart::SCHEMA,4})&&dailyrestart::bootMask({0,3},c,true)==0&&!dailyrestart::canApply(true,false,true,false)&&!dailyrestart::canApply(true,true,false,false);}
static_assert(mixedModes(),"daily preferences must not enable manual/cycle or bypass failed storage/logic");
static_assert(dailyTest(),"daily restart preference, clock gating and per-channel cancellation");
constexpr bool scheduleAtRestart(){program::Config c=program::defaults();c.mode=program::Mode::Daily;c.dailyCount=2;c.daily[0]={25200,28800};c.daily[1]={32400,46800};program::Runtime r={};return !program::evaluate(c,r,0,false,27000).requested&&program::evaluate(c,r,0,true,27000).requested&&!program::evaluate(c,r,0,true,30000).requested&&program::evaluate(c,r,0,true,36000).requested&&!program::evaluate(c,r,0,true,46800).requested;}
static_assert(scheduleAtRestart(),"boot must choose present interval, not replay past intervals");
constexpr bool midnightRestart(){program::Config c=program::defaults();c.mode=program::Mode::Daily;c.daily[0]={82800,3600};program::Runtime r={};return program::evaluate(c,r,0,true,0).requested&&!program::evaluate(c,r,0,true,3600).requested&&program::evaluate(c,r,0,true,82800).requested;}
static_assert(midnightRestart(),"cross-midnight inclusive ON and exclusive OFF boundary");
static_assert(loghealth::active(true,64,false)==FAULT_LOG_BACKLOG,"fullqueue is not storage/write failure");
static_assert(loghealth::active(false,0,false)==FAULT_LOG,"unmounted storage must remain distinct");
static_assert(loghealth::active(true,0,true)==FAULT_LOG_WRITE,"recent IO failure classification");
static_assert(!loghealth::changed(85,85)&&loghealth::changed(85,86)&&loghealth::changed(0,1),"historical counter is not a new failure; first new failure must be caught");
static_assert(loghealth::recent(true,UINT32_MAX-100,200)&&!loghealth::recent(false,0,200)&&!loghealth::recent(true,1000,31000),"IO age uses wrap-safe millis and no stage means no failure");
}
