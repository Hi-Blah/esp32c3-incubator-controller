#include "module_config.h"
#include "network_time.h"
#include "sample_policy.h"
static_assert(INC_LIGHT1_PIN==4&&INC_LIGHT2_PIN==7,"Default wiring");
static_assert(samples::periodSeconds(false,180000)==3&&samples::periodSeconds(false,900000)==10,"Phases");
static_assert(networktime::nasOffset(2000,1000,1792000000000ULL)==1791999998500ULL,"NAS midpoint");
