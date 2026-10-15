#pragma once
#include <stdint.h>
namespace manualrecovery {
constexpr uint8_t mask(bool preference,bool savedArmed,uint32_t savedRevision,uint32_t currentRevision,uint8_t savedMask,uint8_t manualModes,bool abnormalBoot){return preference&&savedArmed&&!abnormalBoot&&savedRevision==currentRevision&&savedMask<=3?static_cast<uint8_t>(savedMask&manualModes):0;}
}
