#pragma once
#include <stdint.h>
constexpr uint32_t hardwareSuffix(uint64_t mac){return ((mac>>24)&255)<<16|((mac>>32)&255)<<8|((mac>>40)&255);}
bool deviceIdentityStart();
uint32_t deviceIdentityValue();
bool deviceIdentitySave(const char *sixHex,bool hardwareDefault);
