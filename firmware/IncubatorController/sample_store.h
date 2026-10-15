#pragma once
// Bounded Flash pages. Serialized by the event-log filesystem mutex; no history-sized RAM.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#include <windows.h>
inline bool replaceFile(const char*from,const char*to){return MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;}
#else
#include <unistd.h>
#endif
namespace samples {
constexpr uint32_t MAGIC=0x53414d32, PAGE_MAGIC=0x53414732;
constexpr unsigned PAGE_COUNT=64,PER_PAGE=32;
constexpr uint16_t SENSOR_VALID=1,PHOTO1_VALID=2,PHOTO2_VALID=4,TRUSTED=8,ARMED=16,
 OUTPUT1=32,OUTPUT2=64,PWM1=128,PWM2=256,ANCHOR=512,NAS_TIME=1024,OUTAGE=2048,IMPORTANT=4096;
#pragma pack(push,1)
struct Record {
 uint32_t magic=0,crc=0;uint64_t seq=0,uptime=0,epoch=0,sensorUptime=0;
 char boot[32]={};float temperature=0,humidity=0;uint32_t cfg=0;
 uint16_t raw[2]={},frequency[2]={};uint8_t duty[2]={},mode[2]={};
 uint16_t flags=0,reserved=0;uint32_t bootOrder=0;
 uint64_t outageId=0,offlineElapsed=0;uint16_t periodSec=10,reserved2=0;
};
struct Header {uint32_t magic=0,crc=0;uint64_t generation=0,reserved=0;};
#pragma pack(pop)
static_assert(sizeof(Record)==124,"Update storage/protocol docs when changing layout");
static_assert(sizeof(Header)+sizeof(Record)*PER_PAGE<4000,"Bound each page below 4000 bytes");
inline uint32_t crc32(const void*data,size_t n){uint32_t c=~0U;const uint8_t*p=(const uint8_t*)data;while(n--){c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320U&uint32_t(-int(c&1)));}return ~c;}
inline void seal(Record&r){r.magic=MAGIC;r.crc=crc32((const uint8_t*)&r+8,sizeof(r)-8);}
inline bool valid(const Record&r){return r.magic==MAGIC&&r.seq&&r.boot[0]&&memchr(r.boot,0,sizeof(r.boot))&&r.crc==crc32((const uint8_t*)&r+8,sizeof(r)-8);}
inline bool flush(FILE*f){if(fflush(f))return false;
#if defined(_WIN32)
return _commit(_fileno(f))==0;
#else
return fsync(fileno(f))==0;
#endif
}
inline bool truncate(FILE*f,size_t n){
#if defined(_WIN32)
return _chsize_s(_fileno(f),n)==0;
#else
return ftruncate(fileno(f),n)==0;
#endif
}
inline FILE* openFile(const char*p,const char*mode){FILE*f=fopen(p,mode);if(f)setvbuf(f,nullptr,_IONBF,0);return f;}
class Store {
 struct Page {uint64_t generation=0,firstSeq=0;uint16_t count=0,ack=0;} pages[PAGE_COUNT];
 char root[160]={};int active=-1;uint64_t generation=0,nextSeq=1;unsigned cursor=0;bool rotate=true;
 void name(char*out,unsigned slot,bool temp=false)const{snprintf(out,192,"%s/s%02u.%s",root,slot,temp?"tmp":"bin");}
 int head()const{int h=-1;for(unsigned i=0;i<PAGE_COUNT;++i)if(pages[i].generation&&pages[i].ack<pages[i].count&&(h<0||pages[i].firstSeq<pages[h].firstSeq))h=i;return h;}
 bool read(unsigned slot,unsigned index,Record&r){char p[192];name(p,slot);FILE*f=openFile(p,"rb");if(!f)return false;bool ok=!fseek(f,long(sizeof(Header)+index*sizeof(Record)),SEEK_SET)&&fread(&r,1,sizeof(r),f)==sizeof(r)&&valid(r);fclose(f);return ok;}
 bool erase(unsigned slot){char p[192];name(p,slot);if(remove(p)){++failures;return false;}pages[slot]={};return true;}
 // Scan one/two pages in chronological order. Preserve boundaries and critical changes.
 bool filter(int a,int b,uint64_t outage,unsigned period,FILE*out,unsigned&kept,uint64_t&first){
  Record previous={};kept=0;first=0;
  for(unsigned part=0;part<2;++part){int slot=part?b:a;if(slot<0)continue;auto&p=pages[slot];char file[192];name(file,slot);FILE*in=openFile(file,"rb");if(!in)return false;
   if(fseek(in,long(sizeof(Header)+p.ack*sizeof(Record)),SEEK_SET)){fclose(in);return false;}
   for(unsigned i=p.ack;i<p.count;++i){Record r={};if(fread(&r,1,sizeof(r),in)!=sizeof(r)||!valid(r)){fclose(in);return false;}
    if(previous.seq&&r.seq<=previous.seq)continue; // Replay after a crash between replacement and source deletion.
    bool keep=i==p.ack||i+1==p.count||(r.flags&(ANCHOR|IMPORTANT))||r.outageId!=outage||!(r.flags&OUTAGE)||r.periodSec>=period;
    if(!keep)keep=!previous.seq||strcmp(previous.boot,r.boot)||previous.outageId!=r.outageId||previous.offlineElapsed/(period*1000ULL)!=r.offlineElapsed/(period*1000ULL);
    if(!keep)continue;seal(r);
    if(!kept)first=r.seq;++kept;previous=r;if(out&&fwrite(&r,1,sizeof(r),out)!=sizeof(r)){fclose(in);return false;}
   }fclose(in);
  }return true;
 }
 public:
 uint32_t covered=0,thinned=0,failures=0,damaged=0;
 uint64_t sequenceCursor()const{return nextSeq;}
 unsigned pending()const{unsigned n=0;for(auto&p:pages)n+=p.count-p.ack;return n;}
 uint64_t oldestSequence(){Record r={};return peek(r)?r.seq:0;}
 bool latest(Record&r){bool found=false;for(unsigned i=0;i<PAGE_COUNT;++i)if(pages[i].count){Record candidate={};if(read(i,pages[i].count-1,candidate)&&(!found||candidate.seq>r.seq)){r=candidate;found=true;}}return found;}
 bool begin(const char*directory){if(strlen(directory)>=sizeof(root))return false;strcpy(root,directory);active=-1;generation=0;nextSeq=1;rotate=true;for(auto&p:pages)p={};
  for(unsigned i=0;i<PAGE_COUNT;++i){char p[192],tmp[192];name(p,i);name(tmp,i,true);remove(tmp);FILE*f=openFile(p,"r+b");if(!f)continue;Header h={};bool ok=fread(&h,1,sizeof(h),f)==sizeof(h)&&h.magic==PAGE_MAGIC&&h.generation&&h.crc==crc32((uint8_t*)&h+8,sizeof(h)-8);
   if(!ok){fclose(f);++damaged;continue;}auto&page=pages[i];page.generation=h.generation;if(h.reserved>nextSeq)nextSeq=h.reserved;if(h.generation>generation){generation=h.generation;active=i;}
   Record r={};uint64_t prev=0;while(page.count<PER_PAGE){size_t n=fread(&r,1,sizeof(r),f);if(!n)break;if(n!=sizeof(r)||!valid(r)||r.seq<=prev){++damaged;break;}if(!page.count)page.firstSeq=r.seq;prev=r.seq;if(r.seq>=nextSeq)nextSeq=r.seq+1;++page.count;}
   const size_t length=sizeof(Header)+page.count*sizeof(Record);if(!truncate(f,length)||!flush(f))++failures;fclose(f);
  }return true;
 }
 bool reclaim(){int h=head();if(h<0||h==active)return false;unsigned lost=pages[h].count-pages[h].ack;if(!erase(h))return false;covered+=lost;return true;}
 bool append(Record&r){
  if(!nextSeq||nextSeq>UINT64_MAX-PER_PAGE){++failures;return false;}
  if(rotate||active<0||pages[active].count==PER_PAGE){if(generation==UINT64_MAX){++failures;return false;}uint64_t next=generation+1;int slot=-1;
   for(unsigned k=1;k<=PAGE_COUNT;++k){unsigned i=(unsigned(active<0?0:active)+k)%PAGE_COUNT;if(!pages[i].generation||(int(i)!=active&&pages[i].count==pages[i].ack)){slot=i;break;}}
   if(slot>=0&&pages[slot].generation&&!erase(slot))return false;
   if(slot<0){slot=head();if(slot<0||slot==active){++failures;return false;}unsigned lost=pages[slot].count-pages[slot].ack;if(!erase(slot))return false;covered+=lost;}
   Header h={};h.magic=PAGE_MAGIC;h.generation=next;h.reserved=nextSeq+PER_PAGE;h.crc=crc32((uint8_t*)&h+8,sizeof(h)-8);char p[192],tmp[192];name(p,slot);name(tmp,slot,true);FILE*f=openFile(tmp,"wb");if(!f){++failures;return false;}bool ok=fwrite(&h,1,sizeof(h),f)==sizeof(h)&&flush(f);if(fclose(f))ok=false;
#if defined(_WIN32)
   if(ok)ok=replaceFile(tmp,p);
#else
   if(ok)ok=rename(tmp,p)==0;
#endif
   if(!ok){++failures;return false;}
   int previous=active;active=slot;rotate=false;generation=next;pages[slot]={next,nextSeq,0,0};if(previous>=0&&previous!=active&&pages[previous].ack==pages[previous].count)erase(previous);
  }
  auto&page=pages[active];r.seq=nextSeq;seal(r);char p[192];name(p,active);FILE*f=openFile(p,"r+b");if(!f){++failures;return false;}size_t offset=sizeof(Header)+page.count*sizeof(Record);bool ok=fseek(f,(long)offset,SEEK_SET)==0&&fwrite(&r,1,sizeof(r),f)==sizeof(r)&&flush(f);if(!ok)truncate(f,offset);if(fclose(f))ok=false;if(!ok){++failures;return false;}if(!page.count)page.firstSeq=r.seq;++page.count;++nextSeq;return true;
 }
 bool peek(Record&r){int h=head();if(h<0)return false;bool ok=read(h,pages[h].ack,r);if(!ok)++failures;return ok;}
 bool acknowledge(const char*boot,uint64_t seq,uint64_t uptime){Record r={};if(!peek(r)||r.seq!=seq||r.uptime!=uptime||strcmp(r.boot,boot))return false;int h=head();++pages[h].ack;if(pages[h].ack==pages[h].count&&h!=active)erase(h);return true;}
 // At most two pages per call; write/flush/atomic replace before removing any source.
 bool compactStep(uint64_t outage,unsigned period){if(!outage||period<=1)return false;int a=int(cursor++%PAGE_COUNT);if(a==active||!pages[a].generation||pages[a].ack==pages[a].count)return false;
  int b=-1;for(unsigned i=0;i<PAGE_COUNT;++i)if(i!=unsigned(a)&&int(i)!=active&&pages[i].ack<pages[i].count&&pages[i].firstSeq>pages[a].firstSeq&&(b<0||pages[i].firstSeq<pages[b].firstSeq))b=i;
  unsigned kept=0;uint64_t first=0;if(!filter(a,b,outage,period,nullptr,kept,first)){++failures;return false;}
  if(kept>PER_PAGE){b=-1;if(!filter(a,b,outage,period,nullptr,kept,first)){++failures;return false;}}
  unsigned before=pages[a].count-pages[a].ack+(b<0?0:pages[b].count-pages[b].ack);if(!kept||kept==before&&b<0)return false;
  char p[192],tmp[192];name(p,a);name(tmp,a,true);FILE*f=openFile(tmp,"wb");if(!f){++failures;return false;}Header h={};h.magic=PAGE_MAGIC;h.generation=pages[a].generation;h.reserved=nextSeq;h.crc=crc32((uint8_t*)&h+8,sizeof(h)-8);
  bool ok=fwrite(&h,1,sizeof(h),f)==sizeof(h)&&filter(a,b,outage,period,f,kept,first)&&flush(f);if(fclose(f))ok=false;
#if defined(_WIN32)
  // Host tests use MoveFileEx for Windows atomic replacement; LittleFS rename replaces.
  if(ok)ok=replaceFile(tmp,p);
#else
  if(ok)ok=rename(tmp,p)==0;
#endif
  if(!ok){++failures;return false;}pages[a].count=kept;pages[a].ack=0;pages[a].firstSeq=first;if(b>=0&&!erase(b))return false;thinned+=before-kept;return true;
 }
};
}
