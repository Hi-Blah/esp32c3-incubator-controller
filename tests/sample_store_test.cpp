#include "network_time.h"
#include "sample_store.h"
#include <assert.h>
#include "sample_policy.h"
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
inline int _mkdir(const char*p){return mkdir(p,0700);}
#endif
struct Path {char text[180];Path(const char*s){snprintf(text,sizeof(text),"%s",s);}const char*c_str()const{return text;}Path operator/(const char*s)const{char b[180];snprintf(b,sizeof(b),"%s/%s",text,s);return Path(b);}};
namespace fs {using path=Path;void create_directory(Path p){_mkdir(p.c_str());}void create_directories(Path p){_mkdir(p.c_str());}size_t file_size(Path p){FILE*f=fopen(p.c_str(),"rb");assert(f);fseek(f,0,SEEK_END);size_t n=ftell(f);fclose(f);return n;}}
samples::Record record(unsigned boot,unsigned index){samples::Record r={};snprintf(r.boot,sizeof(r.boot),"a1b2c3-%u-01234567",boot);r.bootOrder=boot;r.uptime=index*10000ULL;r.cfg=1;r.temperature=25;r.humidity=50;return r;}
int main(int argc,char**argv){assert(networktime::nasOffset(2000,1000,1792000000000ULL)==1791999998500ULL);assert(!networktime::nasOffset(1000,2000,1792000000000ULL));assert(!networktime::nasOffset(17001,1000,1792000000000ULL));assert(!networktime::nasOffset(1000,1000,1));assert(samples::periodSeconds(false,179999)==1);assert(samples::periodSeconds(false,180000)==3);assert(samples::periodSeconds(false,899999)==3);assert(samples::periodSeconds(false,900000)==10);assert(samples::periodSeconds(true,999999)==10);assert(argc==2);fs::path root=argv[1];fs::create_directories(root);
 {
  auto dir=root/"reboot";fs::create_directory(dir);samples::Store a;assert(a.begin(dir.c_str()));for(unsigned i=1;i<=5;++i){auto r=record(1,i);assert(a.append(r));}samples::Record first={};assert(a.peek(first));assert(!a.acknowledge("wrong",first.seq,first.uptime));assert(!a.acknowledge(first.boot,first.seq+1,first.uptime));assert(a.pending()==5);assert(a.acknowledge(first.boot,first.seq,first.uptime));assert(a.pending()==4);
  samples::Store b;assert(b.begin(dir.c_str()));assert(b.pending()==5); // Partial-page ACK loss deliberately replays; NAS deduplicates.
  for(unsigned i=0;i<5;++i){samples::Record r={};assert(b.peek(r));assert(r.bootOrder==1);assert(b.acknowledge(r.boot,r.seq,r.uptime));}assert(!b.pending());auto r=record(2,1);assert(b.append(r));assert(b.peek(r));assert(r.bootOrder==2);
 }
 {
  auto dir=root/"bounded";fs::create_directory(dir);samples::Store a;assert(a.begin(dir.c_str()));for(unsigned i=1;i<=samples::PAGE_COUNT*samples::PER_PAGE+5;++i){auto r=record(1,i);assert(a.append(r));}assert(a.covered==32);assert(a.pending()==2016+5);samples::Record r={};assert(a.peek(r));assert(r.seq==33);assert(a.pending()<=2048);
 }
 {
  auto dir=root/"tail";fs::create_directory(dir);samples::Store a;assert(a.begin(dir.c_str()));for(unsigned i=1;i<=2;++i){auto r=record(1,i);assert(a.append(r));}
  {FILE*f=fopen((dir/"s01.bin").c_str(),"ab");assert(f);fwrite("torn!",1,5,f);fclose(f);}samples::Store b;assert(b.begin(dir.c_str()));assert(b.damaged==1);assert(b.pending()==2);assert(fs::file_size(dir/"s01.bin")==sizeof(samples::Header)+2*sizeof(samples::Record));auto r=record(2,1);assert(b.append(r));assert(b.pending()==3);
 }
 {
  auto dir=root/"crc";fs::create_directory(dir);samples::Store a;assert(a.begin(dir.c_str()));for(unsigned i=1;i<=3;++i){auto r=record(1,i);assert(a.append(r));}
  {FILE*f=fopen((dir/"s01.bin").c_str(),"r+b");assert(f);fseek(f,sizeof(samples::Header)+sizeof(samples::Record)+10,SEEK_SET);fputc(255,f);fclose(f);}
  samples::Store b;assert(b.begin(dir.c_str()));assert(b.damaged==1);assert(b.pending()==1); // Never upload corrupted tail as valid measurements.
 }
 {
  auto dir=root/"boots";fs::create_directory(dir);for(unsigned boot=1;boot<=3;++boot){samples::Store a;assert(a.begin(dir.c_str()));auto r=record(boot,1);assert(a.append(r));}
  samples::Store a;assert(a.begin(dir.c_str()));for(unsigned boot=1;boot<=3;++boot){samples::Record r={};assert(a.peek(r));assert(r.bootOrder==boot);assert(a.acknowledge(r.boot,r.seq,r.uptime));}assert(a.pending()==0);
 }
 {
  samples::Store a;assert(a.begin((root/"missing-parent"/"missing").c_str()));auto r=record(1,1);assert(!a.append(r));assert(a.failures>0);assert(a.pending()==0);
 }
 {
  auto dir=root/"adaptive";fs::create_directory(dir);samples::Store a;assert(a.begin(dir.c_str()));
  for(unsigned i=1;i<=480;++i){auto r=record(1,i);r.uptime=i*1000ULL;r.outageId=1;r.offlineElapsed=r.uptime;r.periodSec=1;r.flags=samples::OUTAGE;if(i==75)r.flags|=samples::IMPORTANT;assert(a.append(r));}
  for(unsigned i=0;i<512;++i)a.compactStep(1,3);assert(a.thinned>250);unsigned fine=a.pending();assert(a.covered==0);
  for(unsigned i=0;i<512;++i)a.compactStep(1,10);assert(a.pending()<fine);assert(a.pending()<120);
  samples::Store b;assert(b.begin(dir.c_str()));bool critical=false;uint64_t prev=0;while(b.pending()){samples::Record r={};assert(b.peek(r));assert(r.seq>prev);prev=r.seq;critical|=r.uptime==75000;assert(r.periodSec==1);assert(b.acknowledge(r.boot,r.seq,r.uptime));}assert(critical);auto r=record(2,1);assert(b.append(r));assert(r.seq>480);
 }
 {
  auto dir=root/"identity";fs::create_directory(dir);samples::Store a;assert(a.begin(dir.c_str()));samples::Record first={};for(unsigned i=1;i<=65;++i){auto r=record(1,i);r.outageId=2;r.offlineElapsed=i*1000;r.periodSec=1;r.flags=samples::OUTAGE;assert(a.append(r));if(i==1)first=r;}
  for(unsigned i=0;i<128;++i)a.compactStep(2,10);samples::Record after={};assert(a.peek(after));assert(!memcmp(&first,&after,sizeof(first)));
 }
 printf("PASS durable page store: bounds, receipts, reboot ordering, torn writes, CRC and I/O failure\n");
}
