#pragma once
#include <stdint.h>
namespace networktime {
// Midpoint estimate from a current-boot heartbeat. NAS epoch is UTC milliseconds.
constexpr uint64_t nasOffset(uint64_t now,uint64_t echo,uint64_t epoch){return now<echo||now-echo>15000||epoch<1704067200000ULL||epoch>4102444799999ULL||epoch<=echo||epoch-echo<(now-echo)/2?0:epoch-echo-(now-echo)/2;}
}
