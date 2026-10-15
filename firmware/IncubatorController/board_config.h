#pragma once
// Board wiring defaults. Override macros here or via compiler -D flags.
// Change module_config.h for feature selection; change this file for wiring.
#ifndef INC_I2C_SDA_PIN
#define INC_I2C_SDA_PIN 6
#endif
#ifndef INC_I2C_SCL_PIN
#define INC_I2C_SCL_PIN 5
#endif
#ifndef INC_DHT_PIN
#define INC_DHT_PIN 21
#endif
#ifndef INC_LIGHT1_PIN
#define INC_LIGHT1_PIN 4
#endif
#ifndef INC_LIGHT2_PIN
#define INC_LIGHT2_PIN 7
#endif
#ifndef INC_PHOTO1_PIN
#define INC_PHOTO1_PIN 0
#endif
#ifndef INC_PHOTO2_PIN
#define INC_PHOTO2_PIN 1
#endif
#ifndef INC_PAGE_BUTTON_PIN
#define INC_PAGE_BUTTON_PIN 10
#endif
#ifndef INC_LIGHT1_BUTTON_PIN
#define INC_LIGHT1_BUTTON_PIN 0
#endif
#ifndef INC_LIGHT2_BUTTON_PIN
#define INC_LIGHT2_BUTTON_PIN 1
#endif
namespace boardconfig {
constexpr bool exposed(int p){return (p>=0&&p<=10)||p==20||p==21;}
constexpr bool safe(int p){return exposed(p)&&p!=2&&p!=8&&p!=9;}
constexpr int pins[]={INC_LIGHT1_PIN,INC_LIGHT2_PIN,
#if INC_ENABLE_OLED
INC_I2C_SDA_PIN,INC_I2C_SCL_PIN,
#endif
#if INC_ENABLE_DHT
INC_DHT_PIN,
#endif
#if INC_ENABLE_PHOTO
INC_PHOTO1_PIN,INC_PHOTO2_PIN,
#else
INC_LIGHT1_BUTTON_PIN,INC_LIGHT2_BUTTON_PIN,
#endif
INC_PAGE_BUTTON_PIN};
constexpr bool valid(){for(unsigned i=0;i<sizeof(pins)/sizeof(*pins);++i){if(!safe(pins[i]))return false;for(unsigned j=0;j<i;++j)if(pins[i]==pins[j])return false;}return true;}
static_assert(valid(),"Duplicate/unsafe GPIO: review board_config.h (USB, flash and strapping pins excluded)");
#if INC_ENABLE_PHOTO
static_assert(INC_PHOTO1_PIN>=0&&INC_PHOTO1_PIN<=4&&INC_PHOTO2_PIN>=0&&INC_PHOTO2_PIN<=4,"Photo AO needs ESP32-C3 ADC1 GPIO0..4");
#endif
}
