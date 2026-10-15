#pragma once
// Edit this one file before Arduino compilation. Existing pin wiring stays unchanged.
#ifndef INC_ENABLE_BLE
#define INC_ENABLE_BLE 1
#endif
#ifndef INC_ENABLE_DHT
#define INC_ENABLE_DHT 1
#endif
#ifndef INC_ENABLE_OLED
#define INC_ENABLE_OLED 1
#endif
#ifndef INC_ENABLE_PHOTO
#define INC_ENABLE_PHOTO 1
#endif
// Photo inputs are ADC1 GPIO0/1. GPIO0/1 buttons are omitted when photo support is compiled.
#include "board_config.h"
#if (INC_ENABLE_BLE != 0 && INC_ENABLE_BLE != 1) || (INC_ENABLE_DHT != 0 && INC_ENABLE_DHT != 1) || (INC_ENABLE_OLED != 0 && INC_ENABLE_OLED != 1) || (INC_ENABLE_PHOTO != 0 && INC_ENABLE_PHOTO != 1)
#error "Feature options must be 0 or 1"
#endif

// Persistent raw samples. 64 pages x 32 records; never allocate this capacity in RAM.
#ifndef INC_ENABLE_SAMPLE_JOURNAL
#define INC_ENABLE_SAMPLE_JOURNAL 1
#endif
#ifndef INC_SAMPLE_INTERVAL_MS
#define INC_SAMPLE_INTERVAL_MS 10000UL
#endif
#ifndef INC_SAMPLE_FAST_PHASE_MS
#define INC_SAMPLE_FAST_PHASE_MS 180000UL
#endif
#ifndef INC_SAMPLE_SLOW_PHASE_MS
#define INC_SAMPLE_SLOW_PHASE_MS 900000UL
#endif
static_assert(INC_ENABLE_SAMPLE_JOURNAL==0||INC_ENABLE_SAMPLE_JOURNAL==1,"Journal option must be 0 or 1");
static_assert(INC_SAMPLE_INTERVAL_MS<=3600000UL,"Capture period must not exceed one hour");
static_assert(INC_SAMPLE_INTERVAL_MS%1000UL==0,"Sample interval must use whole seconds");
static_assert(INC_SAMPLE_INTERVAL_MS>=1000UL,"Online sample period must be at least one second");
static_assert(INC_SAMPLE_SLOW_PHASE_MS>INC_SAMPLE_FAST_PHASE_MS,"Slow phase follows fast phase");

// POSIX timezone: CST-8 means UTC+8. This affects daily schedules.
#ifndef INC_TIMEZONE
#define INC_TIMEZONE "CST-8"
#endif
