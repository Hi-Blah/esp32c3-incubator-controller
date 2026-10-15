#pragma once
#include <stddef.h>
#include <stdint.h>
namespace dht11 {
struct Pulse { uint16_t us; bool high; };
constexpr bool decode(const Pulse *p, size_t count, float &temperature, float &humidity) {
  if (!p || count < 82 || count > 128) return false;
  size_t start = count;
  for (size_t i=0; i+81<count; ++i) {
    if (!p[i].high && p[i+1].high && p[i].us>=65 && p[i].us<=110 &&
        p[i+1].us>=65 && p[i+1].us<=110) { start=i+2; break; }
  }
  if (start==count || start+80>count) return false;
  uint8_t bytes[5]={};
  for (size_t bit=0; bit<40; ++bit) {
    const auto low=p[start+bit*2], high=p[start+bit*2+1];
    if (low.high || !high.high || low.us<30 || low.us>85 || high.us<15 || high.us>100) return false;
    bytes[bit/8] = static_cast<uint8_t>((bytes[bit/8]<<1) | (high.us>50));
  }
  if (static_cast<uint8_t>(bytes[0]+bytes[1]+bytes[2]+bytes[3])!=bytes[4] || bytes[1]>9 || (bytes[3]&0x7f)>9) return false;
  humidity=bytes[0]+bytes[1]*0.1f;
  temperature=bytes[2]+(bytes[3]&0x7f)*0.1f;
  if (bytes[3]&0x80) temperature=-temperature;
  return humidity>=0 && humidity<=100 && temperature>=-20 && temperature<=80;
}
}
