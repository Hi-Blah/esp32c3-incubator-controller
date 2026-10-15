#pragma once
// Numeric IPv4 authorities support LAN access and NAT port forwarding without accepting DNS hostnames.
// Authentication, same-origin checks and CSRF validation remain in network_service.cpp.
namespace httpaccess {
constexpr bool ipv4Authority(const char *s){
 if(!s||!*s)return false;
 for(unsigned octet=0;octet<4;++octet){
  unsigned value=0,digits=0;const char *start=s;
  while(*s>='0'&&*s<='9'){value=value*10+unsigned(*s-'0');++digits;++s;if(digits>3||value>255)return false;}
  if(!digits||(digits>1&&*start=='0'))return false;
  if(octet<3){if(*s++!='.')return false;}
 }
 if(!*s)return true;
 if(*s++!=':'||!*s||*s=='0')return false;
 unsigned port=0,digits=0;
 while(*s>='0'&&*s<='9'){port=port*10+unsigned(*s-'0');++digits;++s;if(digits>5||port>65535)return false;}
 return !*s&&port>0;
}
}
