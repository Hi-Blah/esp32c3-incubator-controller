#pragma once
#include "light_feedback.h"
namespace photojson{
constexpr const char*status(unsigned s){return s==0?"UNCONFIGURED":s==1?"MONITORING_DISABLED":s==2?"OK":s==3?"MISSING_LIGHT":s==4?"DIM_LIGHT":s==5?"STUCK_ON":s==6?"PWM_MODEL_REQUIRED":s==7?"CALIBRATING":s==8?"LOW_DUTY_ESTIMATE":s==9?"NAS_UNAVAILABLE":s==10?"MANUAL_OR_STOPPED":"UNKNOWN";}
template<class J> constexpr void emit(J&j,const PhotoView&p){
 j.raw("{\"compiled\":").boolean(p.compiled).raw(",\"sampled_ms\":").number(p.sampledMs).raw(",\"sampledMs\":").number(p.sampledMs).raw(",\"channels\":[");
 for(unsigned i=0;i<2;++i){if(i)j.raw(",");const auto&c=p.channels[i];j.raw("{\"raw\":").number(c.raw).raw(",\"enabled\":").boolean(c.enabled).raw(",\"calibrated\":").boolean(c.calibrated).raw(",\"valid\":").boolean(c.valid).raw(",\"dark\":").number(c.dark).raw(",\"bright\":").number(c.bright).raw(",\"level\":").number(c.level).raw(",\"expected\":").number(c.expected).raw(",\"expected_valid\":").boolean(c.expectedValid).raw(",\"ratio\":").number(c.ratio).raw(",\"on\":").boolean(c.on).raw(",\"fault\":").boolean(c.fault).raw(",\"status_text\":").quoted(status(c.status)).raw(",\"status\":").number(c.status).raw(",\"model_ready\":").boolean(c.modelReady).raw(",\"model_reliable\":").boolean(c.modelReliable).raw(",\"onThreshold\":").number(c.onThreshold).raw(",\"offThreshold\":").number(c.offThreshold).raw(",\"dimRatio\":").number(c.dimRatio).raw("}");}
 j.raw("],\"automatic\":{\"active\":").boolean(p.automatic.active).raw(",\"channel\":").number(p.automatic.channel).raw(",\"stage\":").number(p.automatic.stage).raw(",\"completed\":").number(p.automatic.completed).raw(",\"total\":").number(p.automatic.total).raw(",\"frequency\":").number(p.automatic.frequency).raw(",\"duty\":").number(p.automatic.duty).raw(",\"message\":").quoted(p.automatic.message).raw("}}");
}
}
