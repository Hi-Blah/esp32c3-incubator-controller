#include "entry_state_json.h"
#define INC_NAS_WEB_ORIGIN "http://nas.example.local:8790"
#include "http_response_policy.h"
namespace {
constexpr bool equal(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
constexpr bool contains(const char*text,const char*part){for(;*text;++text){const char*a=text,*b=part;while(*a&&*b&&*a==*b){++a;++b;}if(!*b)return true;}return false;}
constexpr bool escapedEntry(){
 jsonstream::Buffer<512> b;jsonstream::Writer<jsonstream::Buffer<512>> j(b);
 entrystate::emit(j,"test","token","UNSYNC","a\"b\\c","",false,false);
 return j.ok()&&equal(b.data,"{\"ok\":true,\"firmware\":\"test\",\"csrf\":\"token\",\"clock\":\"UNSYNC\",\"wifi\":{\"connected\":false,\"ssid\":\"a\\\"b\\\\c\",\"ip\":\"\"},\"mqtt\":{\"nasAlive\":false}}");
}
constexpr bool boundedEntry(){
 char ssid[33]={};for(unsigned i=0;i<32;++i)ssid[i]=1; // worst case JSON expansion
 jsonstream::Buffer<512>b;jsonstream::Writer<jsonstream::Buffer<512>>j(b);
 entrystate::emit(j,"Connected-2.2.4","12345678901234567890123456789012","2026-10-19 12:34:56",ssid,"255.255.255.255",true,true);
 return j.ok()&&b.count<512&&!contains(b.data,"lights")&&!contains(b.data,"password")&&contains(b.data,"\"nasAlive\":true");
}
static_assert(escapedEntry(),"landing state must remain valid JSON for quoted SSIDs");
static_assert(boundedEntry(),"landing state fits one512byte block without full programs or secrets");
static_assert(contains(httppolicy::entryCsp,"connect-src 'self' http://nas.example.local:8790;"),"landing page can probe only the configured NAS origin");
static_assert(contains(httppolicy::localCsp,"connect-src 'self';")&&!contains(httppolicy::localCsp,"nas.example.local"),"local controls retain their same-origin policy");
}
