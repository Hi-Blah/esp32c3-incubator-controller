#pragma once
#include <stddef.h>
#include <stdint.h>
namespace jsonstream {
struct Counter { size_t count=0; constexpr bool write(const char*,size_t n){count+=n;return true;} };
template<size_t N> struct Buffer { char data[N]={};size_t count=0;constexpr bool write(const char*p,size_t n){if(count+n>=N)return false;for(size_t i=0;i<n;++i)data[count++]=p[i];data[count]=0;return true;} };
template<class Sink> class Writer {
  Sink &sink; bool good=true;size_t total=0;
  constexpr void bytes(const char*p,size_t n){if(!good)return;if(total+n>8192){good=false;return;}good=sink.write(p,n);if(good)total+=n;}
 public:
  constexpr explicit Writer(Sink&s):sink(s){}
  constexpr Writer& raw(const char*p){size_t n=0;while(p[n])++n;bytes(p,n);return *this;}
  constexpr Writer& quoted(const char*p){raw("\"");for(;*p;++p){const uint8_t c=static_cast<uint8_t>(*p);if(c=='"'||c=='\\'){raw("\\");bytes(p,1);}else if(c<32){const char hex[]="0123456789abcdef";char escaped[7]={'\\','u','0','0',hex[c>>4],hex[c&15],0};raw(escaped);}else bytes(p,1);}return raw("\"");}
  constexpr Writer& number(uint64_t v){char b[20]={};size_t n=0;do{b[n++]=char('0'+v%10);v/=10;}while(v);while(n){--n;bytes(b+n,1);}return *this;}
  constexpr Writer& boolean(bool b){return raw(b?"true":"false");}
  constexpr bool ok()const{return good;}
  constexpr size_t size()const{return total;}
};
}
