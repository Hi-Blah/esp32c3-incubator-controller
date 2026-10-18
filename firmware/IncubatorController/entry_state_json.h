#pragma once
#include "json_stream.h"
// The landing page needs routing and provisioning data, not the complete
// programs, optical model, BLE state or diagnostics. Keep this under one block.
namespace entrystate {
template<class J> constexpr void emit(J &j,const char *firmware,const char *csrf,const char *clock,const char *ssid,const char *ip,bool wifi,bool nasAlive){
 j.raw("{\"ok\":true,\"firmware\":").quoted(firmware).raw(",\"csrf\":").quoted(csrf).raw(",\"clock\":").quoted(clock);
 j.raw(",\"wifi\":{\"connected\":").boolean(wifi).raw(",\"ssid\":").quoted(ssid).raw(",\"ip\":").quoted(ip);
 j.raw("},\"mqtt\":{\"nasAlive\":").boolean(nasAlive).raw("}}");
}
}
