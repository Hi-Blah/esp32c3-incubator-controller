#include "recovery_protocol.h"
namespace {
constexpr const char* VALID=R"JSON({"v":1,"req":"0123456789abcdef","boot":"a1b2c3-24-8ed493ff","previous":"a1b2c3-23-855a6638","cfg":7,"found":true,"epoch_ms":1791871200000,"elapsed1_ms":"18000000000000001","elapsed2_ms":"70000","running":3,"armed":true})JSON";
constexpr bool parseChecks(){recovery::Reply r={};if(!recovery::parse(VALID,r)||r.elapsed[0]!=18000000000000001ULL)return false;
 if(!recovery::matches(r,"0123456789abcdef","a1b2c3-24-8ed493ff","a1b2c3-23-855a6638",7)||recovery::matches(r,"1123456789abcdef","a1b2c3-24-8ed493ff","a1b2c3-23-855a6638",7))return false;
 if(!recovery::usable(r,1791871100000ULL,1791871300000ULL,10000)||recovery::usable(r,1791871200001ULL,1791871300000ULL,10000))return false;
 if(recovery::usable(r,1791871100000ULL,1791871199999ULL,10000))return false;
 recovery::Reply bad={};if(recovery::parse("{}",bad)||recovery::parse(nullptr,bad))return false;
 constexpr const char* MISSING=R"JSON({"v":1,"req":"0123456789abcdef","boot":"a1b2c3-24-8ed493ff","previous":"a1b2c3-23-855a6638","cfg":7,"found":false,"epoch_ms":0,"elapsed1_ms":"0","elapsed2_ms":"0","running":0,"armed":false})JSON";
 if(!recovery::parse(MISSING,bad)||recovery::usable(bad,1791871100000ULL,1791871300000ULL,10000))return false;
 return true;
}
static_assert(parseChecks(),"Recovery reply bounds, exact uint64 and identity/freshness tests failed");
constexpr bool plans(){uint64_t e[2]={70000,60000};auto p=recovery::plan(1000,e,101000,10000,true,0,true);if(!p.valid||p.gap!=100000||p.outage!=90000||p.startup!=10000||p.offset[0]!=170000)return false;
 p=recovery::plan(1000,e,101000,10000,false,0,true);if(p.offset[0]!=70000)return false;
 p=recovery::plan(1000,e,101000,10000,false,90,true);if(p.restart)return false;
 p=recovery::plan(1000,e,101000,9999,false,90,true);if(!p.restart||p.offset[0])return false;
 p=recovery::plan(1000,e,101000,10000,false,90,false);if(!p.restart)return false;
 if(recovery::plan(1000,e,999,1,true,0,true).valid)return false;
 uint64_t huge[2]={UINT64_MAX,0};if(recovery::plan(1,huge,10,1,true,0,true).valid)return false;
 return true;
}
static_assert(plans(),"Advance/pause/restart threshold, boot wait and overflow checks failed");
}
