#include "device_identity.h"
// Synthetic MAC vectors only; no deployed device addresses.
static_assert(hardwareSuffix(0xaabbcc112233ULL)==0xccbbaa);
static_assert(hardwareSuffix(0xddbbcc112233ULL)==0xccbbdd);
static_assert(hardwareSuffix(0xaabbcc112233ULL)!=hardwareSuffix(0xddbbcc112233ULL),"same OUI must not produce same factory identity");
