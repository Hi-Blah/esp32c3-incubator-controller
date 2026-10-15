#include "network_budget.h"
#include "http_transfer.h"
namespace {
constexpr bool same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
// Physical repro: closing the browser freed total heap, but 8180B remained
// the largest block. This can hold a 6144B task and must not wait on 8192B.
static_assert(same(netbudget::startWait(25780,8180),"none"));
static_assert(same(netbudget::startWait(22528,8180),"startup_heap"));
static_assert(same(netbudget::startWait(25780,6143),"startup_block"));
// Initialization may consume the only large block: recheck before task creation.
static_assert(same(netbudget::taskWait(20000,5000),"task_block"));
static_assert(same(netbudget::taskWait(15000,8000),"task_heap"));
static_assert(same(netbudget::taskWait(22000,8180),"none"));
static_assert(netbudget::calibrationReady(23760,12788));
static_assert(!netbudget::calibrationReady(40000,4095));
static_assert(netbudget::calibrationReady(16384,4096));
static_assert(netbudget::calibrationLow(7072,2548));
static_assert(!netbudget::calibrationLow(12288,2048));
static_assert(netbudget::low(3296,1012)&&!netbudget::low(8192,2048));
static_assert(httptransfer::HEAP_RESERVE>netbudget::MQTT_STOP_HEAP+512);
}
