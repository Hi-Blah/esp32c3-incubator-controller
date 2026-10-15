#include "dht_decode.h"
namespace {
struct Trace {dht11::Pulse pulse[84];};
constexpr Trace makeTrace(bool badChecksum=false) {
  Trace result={}; result.pulse[0]={15,false};result.pulse[1]={30,true};
  result.pulse[2]={80,false};result.pulse[3]={80,true};
  uint8_t bytes[5]={49,0,27,1,77};
  if(badChecksum)bytes[4]=76;
  for(unsigned i=0;i<40;++i){result.pulse[4+i*2]={50,false};result.pulse[5+i*2]={static_cast<uint16_t>((bytes[i/8]&(1U<<(7-i%8)))?70:26),true};}
  return result;
}
constexpr bool validTrace(){auto p=makeTrace();float t=0,h=0;return dht11::decode(p.pulse,84,t,h)&&t>27.09f&&t<27.11f&&h==49;}
constexpr bool badChecksumRejected(){auto p=makeTrace(true);float t=0,h=0;return !dht11::decode(p.pulse,84,t,h);}
constexpr bool truncatedRejected(){auto p=makeTrace();float t=0,h=0;return !dht11::decode(p.pulse,81,t,h);}
constexpr bool stuckLineRejected(){Trace p={};float t=0,h=0;return !dht11::decode(p.pulse,84,t,h);}
constexpr bool wrongPulseRejected(){auto p=makeTrace();p.pulse[10].us=500;float t=0,h=0;return !dht11::decode(p.pulse,84,t,h);}
static_assert(validTrace(),"DHT11 handshake/data/checksum fixture");
static_assert(badChecksumRejected(),"Invalid checksum must not become a measurement");
static_assert(truncatedRejected(),"Incomplete capture must be invalid");
static_assert(stuckLineRejected(),"Disconnected/stuck line must be invalid");
static_assert(wrongPulseRejected(),"Malformed pulse must be invalid");
}
