#pragma once
#include <stdint.h>
namespace pwmcheck {constexpr bool accepted(int result,uint32_t actual,uint32_t wanted){const uint32_t delta=actual>wanted?actual-wanted:wanted-actual;return result==0&&actual>0&&delta<=wanted/50+1;}}
