#pragma once
#include <stdint.h>
// Sampling points only. No optical model, fitting or interpolation is kept on the MCU.
namespace photomodel {constexpr unsigned FREQUENCIES=12,DUTIES=9;constexpr uint16_t frequency[FREQUENCIES]={40,50,60,100,200,500,1000,2000,5000,10000,15000,20000};constexpr uint8_t duty[DUTIES]={0,10,20,40,50,60,80,90,100};}
