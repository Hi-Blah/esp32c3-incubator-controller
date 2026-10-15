#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>
// Integer-only decimal formatting avoids newlib's floating-point printf stack/heap path.
namespace decimal {
constexpr bool tenths(char *out,size_t capacity,uint64_t scaled,bool negative=false) {
  if(!out||capacity==0)return false;
  char digits[20]={};size_t count=0;uint64_t whole=scaled/10;
  do {digits[count++]=static_cast<char>('0'+whole%10);whole/=10;}while(whole);
  const bool sign=negative&&scaled!=0;
  const size_t length=count+2+(sign?1:0);
  if(length>=capacity){out[0]=0;return false;}
  size_t at=0;if(sign)out[at++]='-';
  while(count)out[at++]=digits[--count];
  out[at++]='.';out[at++]=static_cast<char>('0'+scaled%10);out[at]=0;return true;
}
inline void sample(char *out,size_t capacity,float value,bool valid=true) {
  if(valid&&isfinite(value)&&value>=-100000.0f&&value<=100000.0f){
    const bool negative=value<0;const float absolute=negative?-value:value;
    if(tenths(out,capacity,static_cast<uint64_t>(absolute*10.0f+0.5f),negative))return;
  }
  if(capacity>=5){out[0]='n';out[1]='u';out[2]='l';out[3]='l';out[4]=0;}else if(capacity)out[0]=0;
}
inline bool seconds(char *out,size_t capacity,uint64_t milliseconds) {
  return tenths(out,capacity,milliseconds/100);
}
}
