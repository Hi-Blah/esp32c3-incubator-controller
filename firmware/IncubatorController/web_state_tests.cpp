#include "web_state_json.h"
namespace {
constexpr bool contains(const char*body,const char*needle){for(size_t i=0;body[i];++i){size_t n=0;while(needle[n]&&body[i+n]&&needle[n]==body[i+n])++n;if(!needle[n])return true;}return false;}
constexpr bool primitiveTest(){jsonstream::Buffer<128> sink;jsonstream::Writer<jsonstream::Buffer<128>> j(sink);j.quoted("A\"\\\n\t").raw(",").number(UINT64_MAX).raw(",").boolean(false);return j.ok()&&contains(sink.data,"\\u000a\\u0009")&&contains(sink.data,"18446744073709551615,false");}
static_assert(primitiveTest(),"JSON quoting or uint64 formatting failed");
constexpr bool rejectOverflow(){jsonstream::Buffer<4> sink;jsonstream::Writer<jsonstream::Buffer<4>> j(sink);j.raw("1234");return !j.ok()&&sink.count==0;}
static_assert(rejectOverflow(),"JSON sink must report truncation");
constexpr size_t maximumState(){
 ControlSnapshot s={};BleView b={};MqttView m={};EventLogView d={};
 s.config[0]=s.config[1]=program::defaults();s.limit=1;s.uptimeMs=UINT64_MAX;s.checkpointEpochMs=s.recoveryGapMs=UINT64_MAX;
 for(uint8_t ch=0;ch<2;++ch){auto&c=s.config[ch];c.mode=program::Mode::Cycle;c.dailyCount=c.stepCount=16;c.repeats=1000000;c.frequency=20000;c.duty=99;c.pwm=true;for(uint8_t i=0;i<16;++i){c.daily[i]={86398,86399};c.steps[i]={604800000,604800000};}s.progress[ch].completed=s.progress[ch].remainingMs=UINT64_MAX;s.progress[ch].row=15;}
 for(size_t i=0;i<96;++i)m.host[i]='a';for(size_t i=0;i<32;++i)m.username[i]='"';for(size_t i=0;i<95;++i)m.lastReply[i]='\\';m.port=65535;m.lastError=INT32_MIN;
 for(size_t i=0;i<39;++i)d.bootId[i]='b';for(size_t i=0;i<23;++i)d.resetReason[i]='R';d.pending=64;d.bootCount=d.dropped=d.writeFailures=UINT32_MAX;
 const char*profile="\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\";
 webstate::Input input={s,b,m,d,"12345678901234567890123456789012","null","null",profile,"192.168.124.106","192.168.4.1",true,true,8,{},UINT32_MAX,UINT32_MAX,UINT32_MAX};for(uint8_t i=0;i<8;++i)input.profiles[i]=profile;
 jsonstream::Counter measure;jsonstream::Writer<jsonstream::Counter> count(measure);webstate::emit(count,input);
 jsonstream::Buffer<8193> buffer;jsonstream::Writer<jsonstream::Buffer<8193>> emit(buffer);webstate::emit(emit,input);
 return count.ok()&&emit.ok()&&count.size()==buffer.count&&contains(buffer.data,"\"temperature\":null")&&contains(buffer.data,"\"lastError\":-2147483648")&&contains(buffer.data,"\"remainingMs\":18446744073709551615")&&contains(buffer.data,"[604800000,604800000]")?count.size():0;
}
static_assert(maximumState()>0&&maximumState()<=8192,"Maximum state or count/body agreement failed");
constexpr size_t largestStateBytes=maximumState();
// All existing 16-row programs fit the smaller command queue, including longest durations/counts.
static_assert(sizeof(ControlCommand::text)>16+16*(8+1+8)+15,"16 cycle rows no longer fit");
static_assert(sizeof(ControlCommand::text)>8+16*17+15,"16 daily rows no longer fit");
}
