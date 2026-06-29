#ifndef KSCconfig_h
#define KSCconfig_h

//编译器系统配置
#ifndef __USE_AUTO__

#define __USE_PC__ 0
#define __USE_STM32__ 1
#define __USE_ESP32__ 0

#endif
//program config
#define __USE_LCD__ 1
#define __USE_CLOCK_TASK__ 1
#define __USE_CHINESE__ 0
#define __DRAW_CIRCLE__ 1   // 圆形/圆弧/圆角矩形 (karc/kcircle/kfillcircle/kroundrect/kfillroundrect)
#define __USE_FLASH__ 1
#define __USE_UART__ 0
#define __USE_LITTLEFS__ 1
#define __USE_KEY__ 0
//屏幕配置
#if __USE_LCD__
// #define __USE_ST7735__ 
#define __USE_ST7789__
// #define __USE_OTHER_LCD__
#endif
//细节配置
#define INPUT_QUEUE_SIZE 4
#define __BUTTON_SIMU__ 1
#define __LITTLE_END_COLOR__ 1
//程序配置
#define __USE_TEXT__ 1

#define COLORBIT 2
#define COLORBYTE 2

#define TFTx 240
#define TFTy 160
#define MAX_INPUT_SIZE 255
#define _STATICBUF_SIZE 512
#include <stdint.h>
#define uintxy uint16_t
#define intxy int16_t
#define KSCCOLOR uint16_t
typedef uint8_t ku8;
typedef uint16_t ku16;
typedef int8_t ki8;
typedef int16_t ki16;
//data config
#define SYSTEMFONT 7
#define SYSTEMCHINESEFONT 16

#define SYSTEMCOLOR0 0xFFFF
#define SYSTEMCOLOR1 0x0000
#define SYSTEMCOLOR2 0x001F
#define SYSTEMCOLOR3 0xF800

#if __USE_PC__
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#define log(...) 0

#endif

#if __USE_STM32__
#include <stdlib.h>

#endif

#if __USE_ESP32__
#include "esp_err.h"
#include "esp_log.h"

#define PPTAG "KSCOS"
#define log(...) ESP_LOGW(PPTAG, __VA_ARGS__)
#define co(color) (((color)&0xFF)<<8)|((color)&0xFF00)
#endif

// 颜色配置:大端模式
#if __LITTLE_END_COLOR__ == 0
#define rred 0xF100
#define bblue 0x001F
#define ggreen 0x07E0
#define bblack 0x0000
#define wwhite 0xFFFF
#define yyellow 0xE0FF
#else
// 颜色配置:小端模式
#define rred 0x00F1
#define bblue 0x1F00
#define ggreen 0xE007
#define bblack 0x0000
#define wwhite 0xFFFF
#define yyellow 0xE0FF
#endif

#if __USE_STM32__
#define KSC_CONSOLE_DRIVER "uart_printf_1"
#elif __USE_PC__
#define KSC_CONSOLE_DRIVER "sys_console"
#endif

#endif
