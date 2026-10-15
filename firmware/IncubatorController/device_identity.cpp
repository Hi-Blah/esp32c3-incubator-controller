#include "device_identity.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
namespace {uint32_t active=0;bool valid(const char *s){if(!s||strlen(s)!=6)return false;for(unsigned i=0;i<6;i++)if(!isxdigit(static_cast<unsigned char>(s[i])))return false;return true;}}
uint32_t deviceIdentityValue(){return active;}
bool deviceIdentityStart(){
 Preferences id;if(!id.begin("inc-id",false))return false;const String saved=id.getString("logical","");
 if(valid(saved.c_str())){active=strtoul(saved.c_str(),nullptr,16);id.end();return true;}
 Preferences old;bool legacy=false;if(old.begin("inc-con",true)){legacy=old.isKey("settings");old.end();}
 if(!legacy&&old.begin("inc-mqtt",true)){legacy=old.isKey("config");old.end();}
 if(!legacy&&old.begin("inc-net",true)){legacy=old.isKey("device-pass");old.end();}
 const uint64_t mac=ESP.getEfuseMac();active=legacy?mac&0xffffff:hardwareSuffix(mac);
 char text[7];snprintf(text,sizeof(text),"%06lx",static_cast<unsigned long>(active));const bool ok=id.putString("logical",text)==6;id.end();return ok;
}
bool deviceIdentitySave(const char *text,bool hardwareDefault){
 char next[7];if(hardwareDefault)snprintf(next,sizeof(next),"%06lx",static_cast<unsigned long>(hardwareSuffix(ESP.getEfuseMac())));else {if(!valid(text))return false;for(unsigned i=0;i<6;i++)next[i]=tolower(static_cast<unsigned char>(text[i]));next[6]=0;}
 Preferences p;bool ok=p.begin("inc-id",false)&&p.putString("logical",next)==6;p.end();return ok;
}
