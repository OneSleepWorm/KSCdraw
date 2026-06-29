#ifndef _KSCbasicdraw_H_
#define _KSCbasicdraw_H_

#include "KSCconfig.h"

#if __USE_LCD__ ==1
#ifdef __cplusplus
extern "C" {
#endif
void screen_init(void* data);
void screen_setcolorpixels(void* data, const KSCCOLOR* color, uint16_t num);
void screen_setcanvas(void* data, uintxy Gx, uintxy Gy, uintxy width, uintxy height);
#ifdef __cplusplus
}
#endif
#include "KSCimg.h"
#include "KSCfont.h"


//对象属性定义ksc_obj_t
typedef struct ksc_obj_t{
    void* data;//对象数据指针
    KSCCOLOR colorck;//对象颜色
    ku8 width;//对象宽度(这个参数有时无需预定义,根据对象类型而定)
    ku8 height;//对象高度
    ku8 sdx;//对象x偏移量(相对于屏幕左上角，下同)
    ku8 sdy;//对象y偏移量
    ku8 d_and_r;//对象半径和深度 低5位为半径，高3位为深度
    ku8 _type;//对象类型和状态 低4位为类型，高4位为状态
}ksc_obj_t;//size:12
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


#define _box 1
#define _string 2
#define _image 3
#define _roundrect 4
#define _fillroundrect 5
#define _fillbox 6
#define _fillcircle 7
#define _line 8
#define _imagebig 9
#define _circle 10
#define _char 11
#define _rect _box
#define _fillrect _fillbox


#define _type_mask (0x0F)
#define _flag_mask (0xF0)
#define _active   (0x80)
#define _visible  (0x40)
#define _dirty    (0x20)
#define _u0       (0x10)
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

#endif

#endif
