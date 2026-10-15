#include "light_feedback.h"

#include "manual_recovery.h"
static_assert(manualrecovery::mask(false,true,5,5,3,3,false)==0,"manual recovery default off");
static_assert(manualrecovery::mask(true,true,5,5,3,3,false)==3,"both manual ON authorized");
static_assert(manualrecovery::mask(true,false,5,5,3,3,false)==0,"locked snapshot never restores");
static_assert(manualrecovery::mask(true,true,4,5,3,3,false)==0,"program edit invalidates old snapshot");
static_assert(manualrecovery::mask(true,true,5,5,3,1,false)==1,"cycle or daily must not use manual snapshot");
static_assert(manualrecovery::mask(true,true,5,5,3,3,true)==0,"abnormal reset never restores");
static_assert(manualrecovery::mask(true,true,5,5,0,3,false)==0,"previous OFF stays OFF");
