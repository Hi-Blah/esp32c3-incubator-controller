#pragma once
#include <stdint.h>
namespace recovery {
constexpr bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*a==*b){++a;++b;}return *a==*b;}
constexpr bool token(const char*s,unsigned min,unsigned max){if(!s)return false;unsigned n=0;for(;*s;++s){if(!((*s>='0'&&*s<='9')||(*s>='a'&&*s<='f')||*s=='-')||++n>max)return false;}return n>=min;}
struct Reply {char req[17]={},boot[40]={},previous[40]={};uint32_t cfg=0;bool found=false,armed=false;uint8_t running=0;uint64_t epochMs=0,elapsed[2]={};};
// Fixed, ordered, flat JSON wire format; no heap allocation, duplicate keys, nesting or float conversion.
class Reader {
 const char*p;bool good=true;
 constexpr void ws(){while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')++p;}
 public:
 constexpr explicit Reader(const char*s):p(s?s:""){if(!s)good=false;}
 constexpr bool literal(const char*s){ws();while(*s){if(*p!=*s){good=false;return false;}++p;++s;}return good;}
 constexpr bool key(const char*s){return literal("\"")&&literal(s)&&literal("\":");}
 constexpr bool text(char*out,unsigned size){ws();if(*p++!='\"'){good=false;return false;}unsigned n=0;while(*p&&*p!='\"'){if(*p<33||*p>126||*p=='\\'||n+1>=size){good=false;return false;}out[n++]=*p++;}out[n]=0;if(*p!='\"'){good=false;return false;}++p;return good;}
 constexpr bool integer(uint64_t&out){ws();out=0;unsigned n=0;const char*first=p;while(*p>='0'&&*p<='9'){uint8_t d=*p-'0';if(out>(UINT64_MAX-d)/10){good=false;return false;}out=out*10+d;++p;++n;}if(!n||(n>1&&*first=='0'))good=false;return good;}
 constexpr bool decimalText(uint64_t&out){return literal("\"")&&integer(out)&&literal("\"");}
 constexpr bool boolean(bool&out){ws();if(*p=='t'){out=true;return literal("true");}out=false;return literal("false");}
 constexpr bool finish(){ws();return good&&!*p;}
};
constexpr bool parse(const char*s,Reply&r){
 Reader p(s);uint64_t version=0,cfg=0,running=0;
 return p.literal("{")&&p.key("v")&&p.integer(version)&&version==1&&p.literal(",")&&p.key("req")&&p.text(r.req,sizeof(r.req))&&
 p.literal(",")&&p.key("boot")&&p.text(r.boot,sizeof(r.boot))&&p.literal(",")&&p.key("previous")&&p.text(r.previous,sizeof(r.previous))&&
 p.literal(",")&&p.key("cfg")&&p.integer(cfg)&&cfg>0&&cfg<=UINT32_MAX&&(r.cfg=static_cast<uint32_t>(cfg),true)&&
 p.literal(",")&&p.key("found")&&p.boolean(r.found)&&p.literal(",")&&p.key("epoch_ms")&&p.integer(r.epochMs)&&
 p.literal(",")&&p.key("elapsed1_ms")&&p.decimalText(r.elapsed[0])&&p.literal(",")&&p.key("elapsed2_ms")&&p.decimalText(r.elapsed[1])&&
 p.literal(",")&&p.key("running")&&p.integer(running)&&running<=3&&(r.running=static_cast<uint8_t>(running),true)&&
 p.literal(",")&&p.key("armed")&&p.boolean(r.armed)&&p.literal("}")&&p.finish()&&token(r.req,16,16)&&token(r.boot,10,39)&&token(r.previous,10,39)&&
 (r.armed||r.running==0);
}
constexpr bool matches(const Reply&r,const char*req,const char*boot,const char*previous,uint32_t cfg){return equal(r.req,req)&&equal(r.boot,boot)&&equal(r.previous,previous)&&r.cfg==cfg;}
constexpr bool usable(const Reply&r,uint64_t localEpoch,uint64_t now,uint64_t uptime){return r.found&&r.elapsed[0]<=19353600000000000ULL&&r.elapsed[1]<=19353600000000000ULL&&now>=uptime&&r.epochMs>=localEpoch&&r.epochMs>=1704067200000ULL&&r.epochMs<=now&&r.epochMs<=now-uptime+2500;}
struct Plan {bool valid=false,restart=false;uint64_t gap=0,outage=0,startup=0,offset[2]={};};
constexpr Plan plan(uint64_t saved,const uint64_t elapsed[2],uint64_t now,uint64_t uptime,bool advance,uint32_t threshold,bool remote){
 Plan p;if(now<saved||now<uptime)return p;p.gap=now-saved;p.startup=uptime;const uint64_t boot=now-uptime;p.outage=boot>saved?boot-saved:0;
 p.restart=threshold&&(remote?p.outage:p.gap)>static_cast<uint64_t>(threshold)*1000;
 for(unsigned i=0;i<2;++i){if(!p.restart&&advance&&elapsed[i]>UINT64_MAX-p.gap)return p;p.offset[i]=p.restart?0:elapsed[i]+(advance?p.gap:0);}p.valid=true;return p;
}
}
