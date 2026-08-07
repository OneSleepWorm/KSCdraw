#ifndef _KSCbasicdraw_H_
#define _KSCbasicdraw_H_

#include "KSCconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if __USE_LCD__ ==1
void screen_init(void* data);
#if __USE_PC__
void screen_hw_init(void);
#endif
void screen_setcolorpixels(void* data, const KSCCOLOR* color, uint16_t num);
void screen_setcanvas(void* data, uintxy Gx, uintxy Gy, uintxy width, uintxy height);
#include "KSCimg.h"
#include "KSCfont.h"


//对象属性定义ksc_obj_t
typedef struct ksc_obj_t{
    void*     data;
    KSCCOLOR  colorck;
    ku8       width;
    ku8       height;
    ku8       sdx;
    ku8       sdy;
    ku8       d_and_r;
    ku8       _type;      // [高4位:用户私有] [低4位:draw_table索引]
} ksc_obj_t;
typedef ksc_obj_t KSC_obj_t;
typedef struct KSC_window {
    ksc_obj_t* objbuf;//对象缓冲区
    KSCCOLOR bk;
    uintxy  width;
    uintxy  height;
    uintxy  ssx;//屏幕左上角X轴位置
    uintxy  ssy;//屏幕左上角Y轴位置
    uint8_t  Mode;
    uint8_t objnum;
}KSC_window;

typedef struct k_draw_device k_draw_device;
typedef struct KSC_window KSC_window;

typedef void (*SCR_INIT)(void* data);
typedef void (*SCR_SETCANVAS)(void* data, uintxy Gx, uintxy Gy, uintxy width, uintxy height);
typedef void (*SCR_SETCOLORPIXELS)(void* data, const KSCCOLOR* color, uint16_t num);
typedef void (*SCR_WINDOW_SETCANVAS)(k_draw_device* dev, KSC_window* screen, uintxy Gx, uintxy Gy, uintxy width, uintxy height);
typedef void (*SCR_WINDOW_SETPIXELS)(k_draw_device* dev, KSC_window* screen, const KSCCOLOR* color, uint16_t num);
typedef struct k_draw_device{
    void*       data;            /* hardware driver context */
    SCR_INIT    init;
    SCR_SETCANVAS setcanvas;
    SCR_SETCOLORPIXELS setcolorpixels;
    SCR_WINDOW_SETCANVAS setwindows;
    SCR_WINDOW_SETPIXELS setpixels;
}k_draw_device;


typedef enum KSC_mes{
    KSC_OK = 0,
    KSC_ERR = 1,
    KSC_ERR_OUT_OF_RANGE = 2,

}KSC_mes;


#define _fillbox      0
#define _box          1
#define _line         2
#define _string       3
#define _image        4
#define _imagebig     5
#define _ibin         6
#define _circle       7
#define _fillcircle   8
#define _arc          9
#define _roundrect   10
#define _fillroundrect 11
#define _char        12
#define _rect _box
#define _fillrect _fillbox


#define _type_mask (0x0F)   // 低4位 → draw_table 索引
#define _r_mask (0x1F)
#define _d_mask (0xE0)

k_draw_device* k_draw_device_init(void);
k_draw_device* k_draw_device_find(const char* app_name);

void kscreenclear(k_draw_device* dev,KSC_window* screen);
void kobjdraw(k_draw_device* dev,KSC_window* screen,ksc_obj_t* obj);
void kobjsdraw(k_draw_device* dev,KSC_window* screen,ksc_obj_t* obj,uint8_t num);
void kscreendraw(k_draw_device* dev,KSC_window* screen);

k_draw_device* kscreenmount(void);
KSC_window* kscreeninit(k_draw_device* dev,uintxy ssx,uintxy ssy,uintxy width,uintxy height,KSCCOLOR bk);
void kscreenfree(k_draw_device* dev,KSC_window* screen);

// 在指定坐标设置像素点
KSC_mes ksetpixel(k_draw_device* dev,KSC_window* screen,KSCCOLOR color,uintxy x,uintxy y);
// 填充矩形区域
KSC_mes kfull(k_draw_device* dev,KSC_window* screen,KSCCOLOR color,uintxy x1,uintxy y1,uintxy w,uintxy h);
// 绘制任意方向线段
KSC_mes kline(k_draw_device* dev,KSC_window* screen,KSCCOLOR color,uintxy x1,uintxy y1,uintxy x2,uintxy y2);
// 绘制矩形边框
/* 自定义绘制函数类型 */
typedef void (*draw_fn)(k_draw_device* dev, KSC_window* screen, struct ksc_obj_t* obj);

/* draw_table 管理 */
void kobjdraw_init(k_draw_device* dev);
int  ksc_set_draw_func(uint8_t idx, draw_fn fn);
draw_fn ksc_get_draw_func(uint8_t idx);

KSC_mes kbox(k_draw_device* dev,KSC_window* screen,KSCCOLOR color,uintxy x1,uintxy y1,uintxy w,uintxy h);
// 填充矩形区域
KSC_mes kfillbox(k_draw_device* dev,KSC_window* screen,KSCCOLOR color,uintxy x1,uintxy y1,uintxy width,uintxy height);
// 绘制圆弧（可选方向）
KSC_mes karc(k_draw_device* dev,KSC_window* screen,KSCCOLOR color,uintxy x0,uintxy y0,uint8_t r,uint8_t Anglediraction);
// 绘制完整圆形
KSC_mes kcircle(k_draw_device* dev,KSC_window* screen,KSCCOLOR color,uintxy x0,uintxy y0,uint8_t r);
// 填充圆形区域
KSC_mes kfillcircle(k_draw_device* dev,KSC_window* screen,KSCCOLOR color,uintxy x0,uintxy y0,uint8_t r);
// 绘制圆角矩形
KSC_mes kroundrect(k_draw_device* dev,KSC_window* screen,KSCCOLOR color,uintxy x1,uintxy y1,uintxy width,uintxy height,uint8_t r);
// 绘制填充圆角矩形
KSC_mes kfillroundrect(k_draw_device* dev,KSC_window* screen,KSCCOLOR color,uintxy x1,uintxy y1,uintxy width,uintxy height,uint8_t r);
// 绘制图像
KSC_mes kdrawimage(k_draw_device* dev,KSC_window* screen,const uint16_t* img,uintxy x,uintxy y,uint8_t width,uint8_t height);
// 绘制图像（缩放）
KSC_mes kdrawimagebig(k_draw_device* dev,KSC_window* screen,const uint16_t* img,uintxy x,uintxy y,uint8_t width,uint8_t height,uint8_t scale);
// 绘制二值化图像
KSC_mes kimagebin(k_draw_device* dev,KSC_window* screen,const uint8_t* img,uintxy x,uintxy y
    ,uint8_t width,uint8_t height,KSCCOLOR colorck,KSCCOLOR colorbk);// 绘制单个字符
KSC_mes kchar(k_draw_device* dev,KSC_window* screen,char ch,uintxy x,uintxy y,KSCCOLOR colorck,KSCCOLOR colorbk);
// 绘制字符串
KSC_mes kstring(k_draw_device* dev,KSC_window* screen,const char* str,uintxy x,uintxy y,KSCCOLOR colorck,KSCCOLOR colorbk);
#if __USE_CHINESE__ >0
// 绘制中文字符串
KSC_mes kstringchinese(k_draw_device* dev,KSC_window* screen,const char* str,uintxy x,uintxy y,KSCCOLOR color1,KSCCOLOR color2);
#endif

#ifdef __cplusplus
}
#endif

#endif

#endif
