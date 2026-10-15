#pragma once
#include "health_service.h"
namespace loghealth{
constexpr uint32_t RECENT_MS=30000;
constexpr bool recent(bool hasFailure,uint32_t at,uint32_t now){return hasFailure&&static_cast<uint32_t>(now-at)<RECENT_MS;}
constexpr uint32_t active(bool ready,unsigned pending,bool recentFailure){return (!ready?uint32_t(FAULT_LOG):0U)|(pending>=64?uint32_t(FAULT_LOG_BACKLOG):0U)|(recentFailure?uint32_t(FAULT_LOG_WRITE):0U);}
constexpr bool changed(uint32_t before,uint32_t now){return before!=now;}
}
