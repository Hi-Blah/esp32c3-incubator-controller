#include "device_identity.h"
#include "module_config.h"
#if INC_ENABLE_BLE
#include "control_service.h"
#include <IncubatorNimBLE.h>
#if INCUBATOR_NIMBLE_PROFILE != 222 || CONFIG_BT_NIMBLE_MAX_CONNECTIONS != 1 || CONFIG_BT_NIMBLE_ROLE_CENTRAL || CONFIG_BT_NIMBLE_ROLE_OBSERVER
#error Install the IncubatorNimBLE peripheral profile supplied with Connected-2.2.3
#endif
#include <Preferences.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>

namespace {
// Nordic UART UUIDs; ASCII commands end in LF (0x0a).
constexpr const char *SERVICE="6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char *BLE_RX_UUID="6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char *BLE_TX_UUID="6e400003-b5a3-f393-e0a9-e50e24dcca9e";
NimBLECharacteristic *tx=nullptr;
QueueHandle_t replyQueue=nullptr;
std::atomic<bool> connected(false),secure(false),restartAdvertising(false);
std::atomic<bool> ready(false),identityReady(false),apClientActive(false),advertisingPaused(false);
uint32_t passkey=0;
char name[25]={};
char input[sizeof(ControlCommand::text)]={};size_t used=0;bool overflow=false;uint32_t lastInput=0,nextId=0;
uint64_t monoMs(){return static_cast<uint64_t>(esp_timer_get_time())/1000ULL;}
void message(const char *text){ControlReply r={};snprintf(r.text,sizeof(r.text),"%s",text);if(replyQueue)xQueueSend(replyQueue,&r,0);}
class ServerCallbacks : public NimBLEServerCallbacks {
  uint32_t onPassKeyDisplay() override {return passkey;}
  void onAuthenticationComplete(NimBLEConnInfo &info) override {secure.store(info.isEncrypted()&&info.isAuthenticated());if(!secure.load())NimBLEDevice::getServer()->disconnect(info.getConnHandle());}

  void onConnect(NimBLEServer *server,NimBLEConnInfo &info) override {
    if(server->getConnectedCount()>1){server->disconnect(info.getConnHandle());return;}
    used=0;overflow=false;secure.store(false);connected.store(true);
  }
  void onDisconnect(NimBLEServer *,NimBLEConnInfo &,int) override {used=0;overflow=false;connected.store(false);secure.store(false);restartAdvertising.store(true);}
};
class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic,NimBLEConnInfo &) override {
    if(!secure.load())return;
    const auto value=characteristic->getValue();
    if(static_cast<uint32_t>(millis()-lastInput)>10000){used=0;overflow=false;}
    lastInput=millis();
    for(size_t i=0;i<value.length();++i){char c=value[i];if(c=='\r')continue;
      if(c=='\n'){
        input[used]=0;
        if(overflow)message("ERR line too long or non-ASCII");
        else if(used){ControlCommand request={};request.origin=Origin::Ble;request.id=++nextId;request.expiresMs=monoMs()+2000;snprintf(request.text,sizeof(request.text),"%s",input);
          if(!controlEnqueue(request))message("ERR command queue busy");}
        used=0;overflow=false;
      }else if(!overflow){if(c<32||c>126||used>=sizeof(input)-1)overflow=true;else input[used++]=c;}
    }
  }
};
void worker(void *){
  ControlReply reply={};size_t offset=0,length=0;
  for(;;){
    const bool pause=apClientActive.load()&&!connected.load();if(pause&&!advertisingPaused.exchange(true))NimBLEDevice::stopAdvertising();else if(!pause&&advertisingPaused.exchange(false)&&!connected.load())NimBLEDevice::startAdvertising();
    if(restartAdvertising.exchange(false)&&!pause){vTaskDelay(pdMS_TO_TICKS(200));NimBLEDevice::startAdvertising();}
    if(!connected.load()||!secure.load()){offset=length=0;while(xQueueReceive(replyQueue,&reply,0)==pdTRUE){};}
    else {
      if(offset>=length&&xQueueReceive(replyQueue,&reply,0)==pdTRUE){length=strlen(reply.text);if(length<sizeof(reply.text)-1){reply.text[length++]='\n';reply.text[length]=0;}offset=0;}
      if(offset<length){size_t n=length-offset>20?20:length-offset;
        if(offset+n<length)while(n && (static_cast<uint8_t>(reply.text[offset+n])&0xc0)==0x80)--n;
        tx->setValue(reinterpret_cast<const uint8_t *>(reply.text+offset),n);tx->notify();offset+=n;}
    }
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}
}
bool bleStart(){
  char suffix[7];snprintf(suffix,sizeof(suffix),"%06lX",static_cast<unsigned long>(deviceIdentityValue()));snprintf(name,sizeof(name),"Incubator-BLE-%s",suffix);
  replyQueue=xQueueCreate(3,sizeof(ControlReply));if(!replyQueue)return false;
  if(!NimBLEDevice::init(name)){vQueueDelete(replyQueue);replyQueue=nullptr;return false;}
  Preferences prefs;if(prefs.begin("inc-ble",false)){passkey=prefs.getUInt("passkey",0);if(passkey<100000||passkey>999999){passkey=100000+esp_random()%900000;prefs.putUInt("passkey",passkey);}prefs.end();}
  else {NimBLEDevice::deinit(true);vQueueDelete(replyQueue);replyQueue=nullptr;return false;}
  identityReady.store(true);
  NimBLEDevice::setSecurityAuth(true,true,true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);NimBLEDevice::setSecurityPasskey(passkey);
  auto server=NimBLEDevice::createServer();server->setCallbacks(new ServerCallbacks());
  auto service=server->createService(SERVICE);
  auto rx=service->createCharacteristic(BLE_RX_UUID,NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_ENC|NIMBLE_PROPERTY::WRITE_AUTHEN);rx->setCallbacks(new RxCallbacks());
  tx=service->createCharacteristic(BLE_TX_UUID,NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::NOTIFY|NIMBLE_PROPERTY::READ_ENC|NIMBLE_PROPERTY::READ_AUTHEN);
  server->start();auto advertising=NimBLEDevice::getAdvertising();advertising->enableScanResponse(true);advertising->addServiceUUID(SERVICE);advertising->setName(name);NimBLEDevice::startAdvertising();
  ready=xTaskCreate(worker,"inc-ble",4096,nullptr,1,nullptr)==pdPASS;
  if(Serial)Serial.printf("BLE name=%s ready=%u profile=peripheral1 (PIN hidden; USB ble info)\n",name,ready.load());return ready;
}
BleView bleView(){BleView b={};b.ready=ready.load();b.connected=connected.load();b.secure=secure.load();b.advertisingPaused=advertisingPaused.load();if(identityReady.load()){b.passkey=passkey;snprintf(b.name,sizeof(b.name),"%s",name);}return b;}
void bleSetApClientActive(bool active){apClientActive.store(active);}
void bleAnswer(const ControlReply &r){if(replyQueue)xQueueSend(replyQueue,&r,0);}

#else
#include "control_service.h"
bool bleStart(){return true;}
BleView bleView(){return {};}
void bleSetApClientActive(bool){}
void bleAnswer(const ControlReply&){}
#endif
