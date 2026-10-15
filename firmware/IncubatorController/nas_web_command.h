#pragma once
#include <stddef.h>
#include <stdint.h>
namespace nasweb {
constexpr bool token(const char *s){size_t n=0;for(;s[n];++n)if(!((s[n]>='0'&&s[n]<='9')||(s[n]>='a'&&s[n]<='f')))return false;return n==24;}
constexpr bool fresh(uint64_t now,uint64_t until){return until>=now&&until-now<=10000;}
constexpr bool command(const char *s){size_t n=0;for(;s[n];++n)if(s[n]<32||s[n]>126||s[n]=='"'||s[n]=='\\')return false;return n>0&&n<=360;}
}
