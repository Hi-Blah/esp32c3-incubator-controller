#pragma once
#include "module_config.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rmt_rx.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "dht_decode.h"
class BoundedDht11 {
  static constexpr gpio_num_t pin=static_cast<gpio_num_t>(INC_DHT_PIN);
  rmt_channel_handle_t rx=nullptr;
  QueueHandle_t done=nullptr;
  bool captureReady=false;
  portMUX_TYPE launchMux=portMUX_INITIALIZER_UNLOCKED;
  rmt_symbol_word_t symbols[64]={};
  static bool IRAM_ATTR received(rmt_channel_handle_t, const rmt_rx_done_event_data_t *event, void *ctx) {
    auto *self=static_cast<BoundedDht11*>(ctx); size_t count=event->num_symbols; BaseType_t wake=pdFALSE;
    xQueueSendFromISR(self->done,&count,&wake); return wake==pdTRUE;
  }
public:
  bool begin() {
    done=xQueueCreate(1,sizeof(size_t)); if(!done) return false;
    rmt_rx_channel_config_t config={}; config.gpio_num=pin; config.clk_src=RMT_CLK_SRC_DEFAULT;
    config.resolution_hz=1000000; config.mem_block_symbols=48;
    if(rmt_new_rx_channel(&config,&rx)!=ESP_OK) return false;
    rmt_rx_event_callbacks_t callbacks={}; callbacks.on_recv_done=received;
    if(rmt_rx_register_event_callbacks(rx,&callbacks,this)!=ESP_OK || rmt_enable(rx)!=ESP_OK) return false;
    gpio_set_direction(pin,GPIO_MODE_INPUT_OUTPUT_OD); gpio_set_pull_mode(pin,GPIO_PULLUP_ONLY); gpio_set_level(pin,1);
    captureReady=true;return true;
  }
  bool read(float &temperature,float &humidity) {
    if(!captureReady || !rx || !done) return false;
    size_t count=0; while(xQueueReceive(done,&count,0)==pdTRUE){}
    gpio_set_direction(pin,GPIO_MODE_INPUT_OUTPUT_OD); gpio_set_pull_mode(pin,GPIO_PULLUP_ONLY);
    gpio_set_level(pin,0); delay(20); // IRQs and the controller task continue running.
    rmt_receive_config_t receive={}; receive.signal_range_min_ns=2000; receive.signal_range_max_ns=150000;
    // Only launch and release are atomic; all pulse capture and waits keep IRQs enabled.
    portENTER_CRITICAL(&launchMux);
    esp_err_t launched=rmt_receive(rx,symbols,sizeof(symbols),&receive);
    gpio_set_level(pin,1);
    portEXIT_CRITICAL(&launchMux);
    if(launched!=ESP_OK)return false;
    if(xQueueReceive(done,&count,pdMS_TO_TICKS(20))!=pdTRUE){rmt_disable(rx);rmt_enable(rx);return false;}
    if(count>64) return false;
    dht11::Pulse pulses[128]={}; size_t used=0;
    for(size_t i=0;i<count;++i){if(symbols[i].duration0)pulses[used++]={static_cast<uint16_t>(symbols[i].duration0),bool(symbols[i].level0)};
      if(symbols[i].duration1)pulses[used++]={static_cast<uint16_t>(symbols[i].duration1),bool(symbols[i].level1)};}
    return dht11::decode(pulses,used,temperature,humidity);
  }
};
