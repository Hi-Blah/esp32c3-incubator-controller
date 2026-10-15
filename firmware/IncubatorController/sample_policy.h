#pragma once
#include <stdint.h>
namespace samples {
constexpr uint16_t periodSeconds(bool online,uint64_t offlineMs,uint64_t fastMs=180000,uint64_t slowMs=900000,uint16_t onlineSec=10){return online?onlineSec:offlineMs<fastMs?1:offlineMs<slowMs?3:10;}
}
