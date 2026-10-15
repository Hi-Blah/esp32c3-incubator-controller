#pragma once
#include <stdint.h>
namespace netbudget {
constexpr uint32_t MQTT_START_HEAP=24576, MQTT_TASK_STACK=6144;
constexpr uint32_t MQTT_INIT_HEAP=16384;
constexpr uint32_t MQTT_STOP_HEAP=8192, MQTT_STOP_BLOCK=2048;
constexpr uint32_t HTTP_HEAP=20480, HTTP_BLOCK=4096;
constexpr uint32_t CAL_START_HEAP=16384, CAL_START_BLOCK=4096, CAL_STOP_HEAP=12288, CAL_STOP_BLOCK=2048;
constexpr bool calibrationReady(uint32_t free,uint32_t largest){return free>=CAL_START_HEAP&&largest>=CAL_START_BLOCK;}
constexpr bool calibrationLow(uint32_t free,uint32_t largest){return free<CAL_STOP_HEAP||largest<CAL_STOP_BLOCK;}
constexpr const char *startWait(uint32_t free,uint32_t largest){return free<MQTT_START_HEAP?"startup_heap":largest<MQTT_TASK_STACK?"startup_block":"none";}
constexpr const char *taskWait(uint32_t free,uint32_t largest){return free<MQTT_INIT_HEAP?"task_heap":largest<MQTT_TASK_STACK?"task_block":"none";}
constexpr bool low(uint32_t free,uint32_t largest){return free<MQTT_STOP_HEAP||largest<MQTT_STOP_BLOCK;}
}
