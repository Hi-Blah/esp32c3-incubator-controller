#include "mqtt_raw_tcp.h"
#include <esp_timer.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <string.h>
namespace rawmqtt {namespace {
// Only the MQTT worker performs IO. Destroy runs after SDK stop has joined that worker.
struct Context {esp_transport_handle_t handle=nullptr;std::atomic<int> fd{-1};} context;
std::atomic<int> lastError(0);
std::atomic<bool> aborting(false);
portMUX_TYPE failureMux=portMUX_INITIALIZER_UNLOCKED;
Failure lastFailure={};
int fail(int e){lastError.store(e);errno=e;return -1;}
int64_t ms(){return esp_timer_get_time()/1000;}
int marked(const char*stage,int e,int fd,int64_t started,int length=0,int sent=0){if(fd<0&&e==ENOTCONN)return fail(e);wifi_ap_record_t ap={};const int rssi=esp_wifi_sta_get_ap_info(&ap)==ESP_OK?ap.rssi:0;const uint32_t heap=heap_caps_get_free_size(MALLOC_CAP_8BIT),largest=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);const int64_t now=ms();portENTER_CRITICAL(&failureMux);++lastFailure.count;lastFailure.atMs=static_cast<uint32_t>(now);lastFailure.elapsedMs=static_cast<uint32_t>(now-started);lastFailure.requested=length>0?length:0;lastFailure.sent=sent>0?sent:0;lastFailure.error=e;lastFailure.fd=fd;lastFailure.rssi=rssi;lastFailure.heap=heap;lastFailure.largest=largest;strncpy(lastFailure.stage,stage,sizeof(lastFailure.stage)-1);lastFailure.stage[sizeof(lastFailure.stage)-1]=0;portEXIT_CRITICAL(&failureMux);return fail(e);}
int remaining(int64_t deadline){int64_t n=deadline-ms();return n>0?static_cast<int>(n):0;}
int wait(int fd,bool write,int timeout){if(fd<0)return fail(ENOTCONN);const int64_t deadline=ms()+(timeout>0?timeout:0);for(;;){fd_set readSet,writeSet;FD_ZERO(&readSet);FD_ZERO(&writeSet);FD_SET(fd,write?&writeSet:&readSet);int left=remaining(deadline);timeval tv={left/1000,(left%1000)*1000};int n=::select(fd+1,write?nullptr:&readSet,write?&writeSet:nullptr,nullptr,&tv);if(n<0&&errno==EINTR&&remaining(deadline))continue;return n<0?fail(errno):n;}}
int closeSocket(esp_transport_handle_t){int fd=context.fd.exchange(-1);if(fd>=0)::close(fd);return 0;}
int connectSocket(esp_transport_handle_t t,const char*host,int port,int timeout){
 closeSocket(t);lastError.store(0);const int64_t deadline=ms()+(timeout>0?timeout:1);sockaddr_in address={};address.sin_family=AF_INET;address.sin_port=htons(port);
 if(::inet_pton(AF_INET,host,&address.sin_addr)!=1){addrinfo hints={},*result=nullptr;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;if(::getaddrinfo(host,nullptr,&hints,&result)!=0||!result){if(result)::freeaddrinfo(result);return marked("connect_dns",EHOSTUNREACH,-1,deadline-(timeout>0?timeout:1));}address.sin_addr=reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;::freeaddrinfo(result);}
 if(!remaining(deadline))return marked("connect_deadline",ETIMEDOUT,-1,deadline-(timeout>0?timeout:1));
 int fd=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(fd<0)return marked("connect_socket",errno,-1,deadline-(timeout>0?timeout:1));context.fd=fd;
 int flags=::fcntl(fd,F_GETFL,0);if(flags<0||::fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){int e=errno;closeSocket(t);return marked("connect",e,fd,deadline-(timeout>0?timeout:1));}
 int result=::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address));
 if(result<0&&errno!=EINPROGRESS&&errno!=EWOULDBLOCK){int e=errno;closeSocket(t);return marked("connect",e,fd,deadline-(timeout>0?timeout:1));}
 if(result<0){result=wait(fd,true,remaining(deadline));int e=0;socklen_t n=sizeof(e);if(result<=0){e=result==0?ETIMEDOUT:lastError.load();closeSocket(t);return marked("connect",e,fd,deadline-(timeout>0?timeout:1));}if(::getsockopt(fd,SOL_SOCKET,SO_ERROR,&e,&n)<0)e=errno;if(e){closeSocket(t);return marked("connect",e,fd,deadline-(timeout>0?timeout:1));}}
 lastError.store(0);return 0;
}
int readSocket(esp_transport_handle_t t,char*buffer,int length,int timeout){if(aborting.load()){closeSocket(t);return fail(ECONNABORTED);}const int64_t started=ms();int fd=context.fd.load();int n=wait(fd,false,timeout);if(n<0)return marked("read_wait",lastError.load(),fd,started,length);if(n==0)return 0;if(aborting.load())return fail(ECONNABORTED);int got=::recv(fd,buffer,length,MSG_DONTWAIT);if(got>0){lastError.store(0);return got;}if(!got)return marked("read_eof",ECONNRESET,fd,started,length);if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)return 0;return marked("read_recv",errno,fd,started,length);}
int writeSocket(esp_transport_handle_t t,const char*buffer,int length,int timeout){const int64_t started=ms(),deadline=started+(timeout>0?timeout:1);int used=0;while(used<length){if(aborting.load()){closeSocket(t);return fail(ECONNABORTED);}int fd=context.fd.load();int n=wait(fd,true,remaining(deadline));if(n<=0)return marked("write_wait",n<0?lastError.load():ETIMEDOUT,fd,started,length,used);if(aborting.load())return fail(ECONNABORTED);n=::send(fd,buffer+used,length-used,MSG_DONTWAIT);if(n>0){used+=n;continue;}if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)){if(remaining(deadline))continue;return marked("write_retry",ETIMEDOUT,fd,started,length,used);}return marked("write_send",n<0?errno:ECONNRESET,fd,started,length,used);}return used;}
int pollRead(esp_transport_handle_t t,int timeout){if(aborting.load()){closeSocket(t);return fail(ECONNABORTED);}const int64_t started=ms();int fd=context.fd.load(),n=wait(fd,false,timeout);return n<0?marked("poll_read",lastError.load(),fd,started):n;}
int pollWrite(esp_transport_handle_t,int timeout){const int64_t started=ms();int fd=context.fd.load(),n=wait(fd,true,timeout);return n<0?marked("poll_write",lastError.load(),fd,started):n;}
int destroy(esp_transport_handle_t t){closeSocket(t);context.handle=nullptr;return 0;}
}
esp_transport_handle_t create(){if(context.handle)return nullptr;auto t=esp_transport_init();if(!t)return nullptr;context.handle=t;context.fd=-1;aborting.store(false);lastError.store(0);esp_transport_set_context_data(t,&context);esp_transport_set_func(t,connectSocket,readSocket,writeSocket,closeSocket,pollRead,pollWrite,destroy);esp_transport_set_default_port(t,1883);return t;}
bool live(esp_transport_handle_t t){return context.handle==t;}
Failure failureView(){Failure result;portENTER_CRITICAL(&failureMux);result=lastFailure;portEXIT_CRITICAL(&failureMux);return result;}
int error(){return lastError.load();}
void abort(){aborting.store(true);}
}
