#pragma once
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include "network_budget.h"
namespace httptransfer {
constexpr size_t BLOCK_SIZE=512;
constexpr uint32_t HEAP_RESERVE=netbudget::HTTP_HEAP, LARGEST_RESERVE=netbudget::HTTP_BLOCK;
enum class Failure : uint8_t { None, Closed, Deadline, LowMemory, Socket };
struct WriteResult { int bytes,error; };
struct Result { size_t sent; Failure failure; int error; };
// IO writes must be nonblocking. Preserve accepted bytes across short writes;
// bound each socket write, wait for ACK/heap recovery, and terminate stalled peers.
template<class IO> constexpr Result transmit(IO &io,const uint8_t *data,size_t length,uint32_t budgetMs){
 const uint64_t start=io.now();size_t offset=0;bool waitingMemory=false;
 while(offset<length){
  if(!io.connected())return {offset,Failure::Closed,0};
  if(io.now()-start>=budgetMs)return {offset,waitingMemory?Failure::LowMemory:Failure::Deadline,0};
  waitingMemory=!io.memoryReady(HEAP_RESERVE,LARGEST_RESERVE);
  if(waitingMemory){io.pause();continue;}
  const size_t remaining=length-offset,count=remaining<BLOCK_SIZE?remaining:BLOCK_SIZE;
  const WriteResult w=io.write(data+offset,count);
  if(w.bytes>0){if(size_t(w.bytes)>count)return {offset,Failure::Socket,EINVAL};offset+=size_t(w.bytes);io.pause();continue;}
  if(w.bytes==0||w.error==EAGAIN||w.error==EWOULDBLOCK||w.error==EINTR||w.error==ENOMEM||w.error==ENOBUFS){io.pause();continue;}
  return {offset,Failure::Socket,w.error};
 }
 return {offset,Failure::None,0};
}
constexpr bool lowerEqual(char a,char b){return (a>='A'&&a<='Z'?char(a+'a'-'A'):a)==b;}
constexpr bool token(const char *p,const char *end,const char *name){for(;*name;++name)if(p==end||!lowerEqual(*p++,*name))return false;return p==end;}
// An explicit gzip;q=0 takes priority over a wildcard. Unknown tokens cannot
// select compression; browsers without Accept-Encoding receive plain HTML.
constexpr bool acceptsGzip(const char *text){
 if(!text)return false;
 bool wildcard=false,gzipSeen=false,gzipAllowed=false;
 const char *p=text;while(*p){while(*p==' '||*p=='\t'||*p==',')++p;if(!*p)break;const char *start=p;while(*p&&*p!=';'&&*p!=',')++p;const char *end=p;while(end>start&&(end[-1]==' '||end[-1]=='\t'))--end;
  const bool isGzip=token(start,end,"gzip"),isWildcard=token(start,end,"*");bool allowed=true;
  while(*p&&*p!=','){if(*p++!=';')continue;while(*p==' '||*p=='\t')++p;if(!lowerEqual(*p,'q'))continue;++p;while(*p==' '||*p=='\t')++p;if(*p!='=')continue;++p;while(*p==' '||*p=='\t')++p;
   bool digit=false,nonzero=false;while((*p>='0'&&*p<='9')||*p=='.'){if(*p!='.'){digit=true;nonzero=nonzero||*p!='0';}++p;}allowed=digit&&nonzero;
  }
  if(isGzip){gzipSeen=true;gzipAllowed=allowed;}else if(isWildcard)wildcard=allowed;if(*p==',')++p;
 }
 return gzipSeen?gzipAllowed:wildcard;
}
}
