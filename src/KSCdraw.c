////////////////////////////////////////////////////////////////////////
/* @file KSCdraw.c
 * @brief 基本绘图函数
 * @author OneSleepWorm(一只瞌睡虫)
 * @date 2026/1/31
 * @version 1.1.0
 */
/////////////////////////////////////////////////////////////////

#include "../inc/KSCdraw.h"
#include <string.h>

#if __USE_LCD__ ==1

void k_window_setcanvas(k_draw_device* dev,KSC_window* screen,uintxy Gx,uintxy Gy, uintxy width,uintxy height){
    
    Gx += screen->ssx;
    Gy += screen->ssy;
    dev->setcanvas(Gx,Gy,width,height);
}

static k_draw_device sys_dev;
static uint8_t draw_buf[_STATICBUF_SIZE];

static inline intxy _abs(intxy x) {
    return x < 0 ? -x : x;
}

void memset_16(void* buf, size_t size_bytes, uint16_t pattern) {
    if (size_bytes < sizeof(uint16_t)) return;
    uint16_t* p = (uint16_t*)buf;
    size_t len = size_bytes / sizeof(uint16_t);
    p[0] = pattern;
    size_t i;
    for (i = 1; i < len; i <<= 1) {
        size_t copy_size = (i < len - i) ? i : len - i;
        memcpy(p + i, p, copy_size * sizeof(uint16_t));
    }
}

/* 公共函数：保留原接口，内部进行指针和位宽优化 */
void imgchange(const uint8_t* imgbuf,
               uint16_t* imgbuf2,
               const uint16_t length,//bit位长度
               uint16_t bit,
               const uint16_t* colortable)
{
    switch(bit) {
    case 1: {
        const uint8_t *pSrc = imgbuf;
        uint16_t *pDst = imgbuf2;
        const uint8_t *pSrcEnd = imgbuf + (length >> 3);
        uint32_t *pDst32 = (uint32_t*)pDst;

        while (pSrc < pSrcEnd) {
            uint8_t byte = *pSrc++;
            uint16_t c0 = colortable[(byte >> 7) & 0x01];
            uint16_t c1 = colortable[(byte >> 6) & 0x01];
            uint16_t c2 = colortable[(byte >> 5) & 0x01];
            uint16_t c3 = colortable[(byte >> 4) & 0x01];
            uint16_t c4 = colortable[(byte >> 3) & 0x01];
            uint16_t c5 = colortable[(byte >> 2) & 0x01];
            uint16_t c6 = colortable[(byte >> 1) & 0x01];
            uint16_t c7 = colortable[(byte >> 0) & 0x01];

            *pDst32++ = ((uint32_t)c1 << 16) | c0;
            *pDst32++ = ((uint32_t)c3 << 16) | c2;
            *pDst32++ = ((uint32_t)c5 << 16) | c4;
            *pDst32++ = ((uint32_t)c7 << 16) | c6;
        }
        pDst = (uint16_t*)pDst32;

        uint8_t remain = length & 0x07;
        if (remain) {
            uint8_t byte = *pSrc;
            for (uint8_t j = 0; j < remain; j++) {
                *pDst++ = colortable[(byte >> (7 - j)) & 0x01];
            }
        }
        break;
    }

    case 2: {
        const uint8_t *pSrc = imgbuf;
        uint16_t *pDst = imgbuf2;
        const uint8_t *pSrcEnd = imgbuf + (length >> 2);
        uint32_t *pDst32 = (uint32_t*)pDst;

        while (pSrc < pSrcEnd) {
            uint8_t byte = *pSrc++;
            uint16_t c0 = colortable[(byte >> 6) & 0x03];
            uint16_t c1 = colortable[(byte >> 4) & 0x03];
            uint16_t c2 = colortable[(byte >> 2) & 0x03];
            uint16_t c3 = colortable[(byte >> 0) & 0x03];

            *pDst32++ = ((uint32_t)c1 << 16) | c0;
            *pDst32++ = ((uint32_t)c3 << 16) | c2;
        }
        pDst = (uint16_t*)pDst32;

        uint8_t remain = length & 0x03;
        if (remain) {
            uint8_t byte = *pSrc;
            for (uint8_t j = 0; j < remain; j++) {
                *pDst++ = colortable[(byte >> (6 - j*2)) & 0x03];
            }
        }
        break;
    }

    case 4: {
        const uint8_t *pSrc = imgbuf;
        uint16_t *pDst = imgbuf2;
        const uint8_t *pSrcEnd = imgbuf + (length >> 1);
        uint32_t *pDst32 = (uint32_t*)pDst;

        while (pSrc < pSrcEnd) {
            uint8_t byte = *pSrc++;
            uint16_t c0 = colortable[(byte >> 4) & 0x0F];
            uint16_t c1 = colortable[(byte >> 0) & 0x0F];
            *pDst32++ = ((uint32_t)c1 << 16) | c0;
        }
        pDst = (uint16_t*)pDst32;

        if (length & 0x01) {
            *pDst++ = colortable[(*pSrc >> 4) & 0x0F];
        }
        break;
    }

    default:
        return;
    }
}

#define RightUpper 0x01
#define LeftUpper 0x02
#define RightLower 0x04
#define LeftLower 0x08

// void window_setcanvas(KSC_window* screen, uintxy x, uintxy y, uintxy w, uintxy h){
//     uintxy rx = x + screen->ssx;
//     uintxy ry = y + screen->ssy;
//     window_setcanvas(screen, rx, ry, w, h);
// }

KSC_mes ksetpixel(k_draw_device* dev,KSC_window* screen, KSCCOLOR color, uintxy x, uintxy y){
    if (!screen) return KSC_ERR;
    dev->setwindows(dev,screen, x, y, 1, 1);
    dev->setcolorpixels(&color, 1);
    return KSC_OK;
}

k_draw_device* kscreenmount(void){
    static k_draw_device dev={
    .init=screen_init,
    .setcanvas=screen_setcanvas,
    .setcolorpixels=screen_setcolorpixels,
    .setwindows=k_window_setcanvas,
    };
    k_draw_device* devp = &dev;
    devp->init();
    devp->setwindows=k_window_setcanvas;
    return devp;
}
k_draw_device* k_draw_device_init(void){
    sys_dev.init=screen_init;
    sys_dev.setcanvas=screen_setcanvas;
    sys_dev.setcolorpixels=screen_setcolorpixels;
    sys_dev.init();
    sys_dev.setwindows=k_window_setcanvas;
    return &sys_dev;
}

k_draw_device* k_draw_device_find(const char* app_name){
    return &sys_dev;
}

KSC_window* kscreeninit(k_draw_device* dev,uintxy ssx, uintxy ssy, uintxy width, uintxy height, KSCCOLOR bk){
    KSC_window* screen = (KSC_window*)malloc(sizeof(KSC_window));
    if (screen == NULL){
        return NULL;
    }

    screen->Mode = 1<<7; //标记结构体为malloc分配

    screen->ssx = ssx;
    screen->ssy = ssy;
    screen->width = width;
    screen->height = height;
    screen->bk = bk;
    // ksetscreen(screen);
    
    kfull(dev,screen, bk, 0, 0, width, height);
    return screen;
}

void kscreenfree(k_draw_device* dev,KSC_window* screen){
    if (!screen) return;

    if (screen->Mode & (1<<7)){
        free(screen);
    }
}

KSC_mes kfull(k_draw_device* dev,KSC_window* screen, KSCCOLOR color, uintxy x1, uintxy y1, uintxy w, uintxy h){
    if (!screen) return KSC_ERR;
    if (w == 0 || h == 0) return KSC_OK;
    // return KSC_OK;
    // dev->setwindows(dev,screen, x1, y1, w, h);
    dev->setwindows(dev,screen, x1, y1, w, h);
    uint16_t buf[_STATICBUF_SIZE>>1];
    for (uint16_t i = 0; i < _STATICBUF_SIZE>>1; i++){
        buf[i] = color;
    }
    
    uint32_t pixelnum = w * h;
    // uint16_t* buf = (uint16_t*)draw_buf;
    uint16_t staticbuf_pixel = (_STATICBUF_SIZE >> 1);

    while (pixelnum > staticbuf_pixel){
        dev->setcolorpixels(buf, staticbuf_pixel);
        pixelnum -= staticbuf_pixel;
    }
    dev->setcolorpixels(buf, (uint16_t)pixelnum);

    return KSC_OK;
}

KSC_mes kline(k_draw_device* dev,KSC_window* screen, KSCCOLOR color, uintxy x1, uintxy y1, uintxy x2, uintxy y2){
    if (!screen) return KSC_ERR;

    if (x1 == x2) {
        uintxy ymin = y1 < y2 ? y1 : y2;
        uintxy ymax = y1 < y2 ? y2 : y1;
        kfull(dev,screen, color, x1, ymin, 1, ymax - ymin + 1);
        return KSC_OK;
    }
    if (y1 == y2) {
        uintxy xmin = x1 < x2 ? x1 : x2;
        uintxy xmax = x1 < x2 ? x2 : x1;
        kfull(dev,screen, color, xmin, y1, xmax - xmin + 1, 1);
        return KSC_OK;
    }

    int dx = _abs((intxy)x2 - (intxy)x1);
    int dy = _abs((intxy)y2 - (intxy)y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        ksetpixel(dev,screen, color, x1, y1);

        if (x1 == x2 && y1 == y2) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
    return KSC_OK;
}

KSC_mes kbox(k_draw_device* dev,KSC_window* screen, KSCCOLOR color, uintxy x1, uintxy y1, uintxy w, uintxy h){
    if (!screen) return KSC_ERR;
    if (w == 0 || h == 0) return KSC_OK;

    kfull(dev,screen, color, x1, y1, w, 1);
    kfull(dev,screen, color, x1, y1, 1, h);
    kfull(dev,screen, color, x1 + w - 1, y1, 1, h);
    kfull(dev,screen, color, x1, y1 + h - 1, w, 1);
    return KSC_OK;
}

KSC_mes kfillbox(k_draw_device* dev,KSC_window* screen, KSCCOLOR color, uintxy x1, uintxy y1, uintxy w, uintxy height){
    return kfull(dev,screen, color, x1, y1, w, height);
}

KSC_mes karc(k_draw_device* dev,KSC_window* screen, KSCCOLOR color, uintxy x0, uintxy y0, uint8_t r, uint8_t Anglediraction){
    if (!screen) return KSC_ERR;
    if ((Anglediraction & 0xF0) != 0) return KSC_ERR;

    intxy x = r;
    intxy y = 0;
    int err = 0;

    while (x >= y) {
        if (Anglediraction & RightLower){
            ksetpixel(dev,screen, color, (uintxy)(x0 + x), (uintxy)(y0 + y));
            ksetpixel(dev,screen, color, (uintxy)(x0 + y), (uintxy)(y0 + x));
        }
        if (Anglediraction & LeftLower){
            ksetpixel(dev,screen, color, (uintxy)(x0 - y), (uintxy)(y0 + x));
            ksetpixel(dev,screen, color, (uintxy)(x0 - x), (uintxy)(y0 + y));
        }
        if (Anglediraction & LeftUpper){
            ksetpixel(dev,screen, color, (uintxy)(x0 - x), (uintxy)(y0 - y));
            ksetpixel(dev,screen, color, (uintxy)(x0 - y), (uintxy)(y0 - x));
        }
        if (Anglediraction & RightUpper){
            ksetpixel(dev,screen, color, (uintxy)(x0 + y), (uintxy)(y0 - x));   
            ksetpixel(dev,screen, color, (uintxy)(x0 + x), (uintxy)(y0 - y));
        }

        y += 1;
        if (err <= 0) {
            err += 2*y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }
    return KSC_OK;
}

KSC_mes kcircle(k_draw_device* dev,KSC_window* screen, KSCCOLOR color, uintxy x0, uintxy y0, uint8_t r){
    return karc(dev,screen, color, x0, y0, r, 0x0F);
}

KSC_mes kfillcircle(k_draw_device* dev,KSC_window* screen, KSCCOLOR color, uintxy x0, uintxy y0, uint8_t r){
    if (!screen) return KSC_ERR;
    if (r == 0) {
        return ksetpixel(dev,screen, color, x0, y0);
    }

    int x = 0;
    int y = r;
    int d = 3 - 2 * r;

    while (x <= y) {
        int old_x = x;
        int old_y = y;

        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;

        // y下降时,上一行闭合: 画出 y=old_y 的扫描线,宽度由 old_x 决定
        if (y < old_y) {
            int w = 2 * old_x + 1;
            kfull(dev,screen, color, x0 - old_x, y0 - old_y, w, 1);
            if (old_y > 0)
                kfull(dev,screen, color, x0 - old_x, y0 + old_y, w, 1);
        }

        // 对称行: 总是画出 x=old_x 的扫描线,宽度由 old_y 决定
        // 这覆盖了 rows 0..r/√2 (这些行永远不会触发 y 下降的闭合绘制)
        int ws = 2 * old_y + 1;
        kfull(dev,screen, color, x0 - old_y, y0 - old_x, ws, 1);
        if (old_x > 0)
            kfull(dev,screen, color, x0 - old_y, y0 + old_x, ws, 1);
    }

    return KSC_OK;
}

KSC_mes kdrawimage(k_draw_device* dev,KSC_window* screen, const uint16_t* img, uintxy x, uintxy y,
    uint8_t width, uint8_t height){
    if (!screen || !img) return KSC_ERR;
    dev->setwindows(dev,screen, x, y, width, height);
    uint16_t imgsize = width * height;
    dev->setcolorpixels(img, imgsize);
    return KSC_OK;
}

KSC_mes kdrawimagebig(k_draw_device* dev,KSC_window* screen, const uint16_t* img, uintxy x, uintxy y,
    uint8_t width, uint8_t height, uint8_t scale){
    if (!screen || !img) return KSC_ERR;
    if (scale == 0) return KSC_ERR;

    for (uint8_t h = 0; h < height; h++){
        for (uint8_t w = 0; w < width; w++){
            KSCCOLOR ncolor = *img++;
            kfull(dev,screen, ncolor, x + w * scale, y + h * scale, scale, scale);
        }
    }
    return KSC_OK;
}

KSC_mes kroundrect(k_draw_device* dev,KSC_window* screen, KSCCOLOR color, uintxy x1, uintxy y1, uintxy width, uintxy height, uint8_t r){
    if (!screen) return KSC_ERR;
    if (r == 0){
        return kbox(dev,screen, color, x1, y1, width, height);
    }
    if (2 * r > width){
        r = width / 2;
    }
    if (2 * r > height){
        r = height / 2;
    }
    uintxy x2 = x1 + width;
    uintxy y2 = y1 + height;

    karc(dev,screen, color, x1 + r, y1 + r, r, LeftUpper);
    karc(dev,screen, color, x2 - r, y1 + r, r, RightUpper);
    karc(dev,screen, color, x1 + r, y2 - r, r, LeftLower);
    karc(dev,screen, color, x2 - r, y2 - r, r, RightLower);

    kline(dev,screen, color, x1 + r, y1, x2 - r, y1);
    kline(dev,screen, color, x1 + r, y2, x2 - r, y2);
    kline(dev,screen, color, x1, y1 + r, x1, y2 - r);
    kline(dev,screen, color, x2, y1 + r, x2, y2 - r);
    return KSC_OK;
}

KSC_mes kfillroundrect(k_draw_device* dev,KSC_window* screen, KSCCOLOR color, uintxy x1, uintxy y1, uintxy width, uintxy height, uint8_t r){
    if (!screen) return KSC_ERR;
    if (r == 0){
        return kfillbox(dev,screen, color, x1, y1, width, height);
    }
    if (2 * r > width){
        r = width / 2;
    }
    if (2 * r > height){
        r = height / 2;
    }
    uintxy x2 = x1 + width;
    uintxy y2 = y1 + height;

    kfillbox(dev,screen, color, x1 + r, y1, width - 2 * r, height + 1);
    kfillbox(dev,screen, color, x1, y1 + r, width + 1, height - 2 * r);

    kfillcircle(dev,screen, color, x1 + r, y1 + r, r);
    kfillcircle(dev,screen, color, x2 - r, y1 + r, r);
    kfillcircle(dev,screen, color, x1 + r, y2 - r, r);
    kfillcircle(dev,screen, color, x2 - r, y2 - r, r);
    return KSC_OK;
}

KSC_mes kimagebin(k_draw_device* dev,KSC_window* screen, const uint8_t* img, uintxy x, uintxy y,
    uint8_t width, uint8_t height, KSCCOLOR colorck, KSCCOLOR colorbk){
    if (!screen || !img) return KSC_ERR;

    uint16_t pixelcount = width * height;
    uint16_t needed = pixelcount * COLORBYTE;
    if (needed > _STATICBUF_SIZE) return KSC_ERR;

    uint16_t* imgbuf = (uint16_t*)draw_buf;
    uint16_t colortable[2] = {colorbk, colorck};
    imgchange(img, imgbuf, pixelcount, 1, colortable);
    kdrawimage(dev,screen, imgbuf, x, y, width, height);
    return KSC_OK;
}

KSC_mes kchar(k_draw_device* dev,KSC_window* screen, char ch, uintxy x, uintxy y, KSCCOLOR colorck, KSCCOLOR colorbk){
    if (!screen) return KSC_ERR;
    KSC_Font1* font = &Systemfont0;
    const uint8_t* char_bitmap = font->Getfontfunc(ch);
    kimagebin(dev,screen, char_bitmap, x, y, font->width, font->height, colorck, colorbk);
    return KSC_OK;
}

KSC_mes kstring(k_draw_device* dev,KSC_window* screen, const char* str, uintxy x, uintxy y, KSCCOLOR colorck, KSCCOLOR colorbk){
    if (!screen || !str) return KSC_ERR;
    KSC_Font1* font = &Systemfont0;
    while (*str){
        kchar(dev,screen, *str, x, y, colorck, colorbk);
#if SYSTEMFONT == 7
        x += font->width - 1;
#else
        x += font->width;
#endif
        str++;
    }
    return KSC_OK;
}

#if __USE_CHINESE__ >0
KSC_mes kstringchinese(k_draw_device* dev,KSC_window* screen, const char* str, uintxy x, uintxy y, KSCCOLOR color1, KSCCOLOR color2){
    if (!screen || !str) return KSC_ERR;
    KSC_FontChinese* font = &SystemfontChinese;
    uint8_t* char_bitmap = NULL;
    uint32_t count = utf8get(str, 0, 0);
    for (uint8_t j = 0; j < count; j++){
        char_bitmap = font->Getfontfunc(str, j);
        kimagebin(dev,screen, char_bitmap, x + j * font->width, y,
            font->width, font->height, color1, color2);
    }
    return KSC_OK;
}
#endif
void kscreenclear(k_draw_device* dev,KSC_window* screen){
    if(!dev || !screen)return;
    kfull(dev,screen,screen->bk,0,0,screen->width,screen->height);
}

void kobjdraw(k_draw_device* dev,KSC_window* screen,ksc_obj_t* obj){
    if(!dev || !screen || !obj)return;
    //printf("obj->_type=%02X\n",obj->_type);
    switch (obj->_type&_type_mask)
    {
    case _circle: {
        /* code */
        uint8_t r = obj->d_and_r&_r_mask;
        kcircle(dev,screen,obj->colorck,obj->sdx+r,obj->sdy+r,r);
        break;
    }
    case _box:
        /* code */
        kbox(dev,screen,obj->colorck,obj->sdx,obj->sdy,obj->width,obj->height);
        break;
    case _string:
        /* code */
        kstring(dev,screen,(char*)obj->data,obj->sdx,obj->sdy,obj->colorck,screen->bk);
        break;
    case _image:
        /* code */
        kdrawimage(dev,screen,obj->data,obj->sdx,obj->sdy
        ,obj->width,obj->height);
        break;
    case _imagebig:
        /* code */
        kdrawimagebig(dev,screen,obj->data,obj->sdx,obj->sdy
        ,obj->width,obj->height,obj->d_and_r&_d_mask);
        break;
    case _line:
        /* code */
        kline(dev,screen,obj->colorck,obj->sdx,obj->sdy
            ,obj->width,obj->height);
        break;
    case _fillcircle: {
        /* code */
        uint8_t r = obj->d_and_r&_r_mask;
        kfillcircle(dev,screen,obj->colorck,obj->sdx+r,obj->sdy+r,r);
        break;
    }
    case _fillbox:
        /* code */
        kfillbox(dev,screen,obj->colorck,obj->sdx,obj->sdy,obj->width,obj->height);
        break;
    case _roundrect: {
        /* code */
        uint8_t r = obj->d_and_r&_r_mask;
        kroundrect(dev,screen,obj->colorck,obj->sdx,obj->sdy
            ,obj->width,obj->height,r);
        break;
    }
    case _fillroundrect:
        /* code */
        kfillroundrect(dev,screen,obj->colorck,obj->sdx,obj->sdy
            ,obj->width,obj->height,obj->d_and_r&_r_mask);
        break;
    case _char:
        /* code */
        kchar(dev,screen,*(char*)(obj->data),obj->sdx,obj->sdy,obj->colorck,screen->bk);
        break;
    default:
        break;
    }
}

void kobjsdraw(k_draw_device* dev,KSC_window* screen,ksc_obj_t* obj,uint8_t num){
    if(!dev || !screen || !obj)return;
    for(uint8_t i=0;i<num;i++){
        if(((obj+i)->_type & (_active|_visible)) != (_active|_visible))continue;
        kobjdraw(dev,screen,obj+i);
    }
}

void kscreendraw(k_draw_device* dev,KSC_window* screen){
    if(!dev || !screen)return;
    kfull(dev,screen,screen->bk,0,0,screen->width,screen->height);
    kobjsdraw(dev,screen,screen->objbuf,screen->objnum);
}


/* --- 默认设备屏幕接口 (已由 KSCGUI 接管, 此 stubs 仅保留给 PC 后端) --- */
/* 注: STM32 上 k_draw_device 的 setcanvas/setcolorpixels 由 kscgui.c
 *     的 gui_setcanvas/gui_pixels 覆盖, 不会走到这里。 */
#if __USE_STM32__
void screen_init(void) { }
void screen_setcanvas(uintxy Gx, uintxy Gy, uintxy width, uintxy height)
{
    (void)Gx; (void)Gy; (void)width; (void)height;
}
void screen_setcolorpixels(const KSCCOLOR* color, uint16_t num)
{
    (void)color; (void)num;
}
#endif

#if __USE_PC__
#include "../third_party/easyx/include/graphics.h"
#include <stdio.h>

static uint32_t color16to24(uint16_t color16)
{
    uint8_t r5 = (color16 >> 11) & 0x1F;
    uint8_t g6 = (color16 >> 5) & 0x3F;
    uint8_t b5 = (color16 & 0x1F);
    uint8_t r8 = (r5 << 3) | (r5 >> 2);
    uint8_t g8 = (g6 << 2) | (g6 >> 4);
    uint8_t b8 = (b5 << 3) | (b5 >> 2);
    return (b8 << 16) | (g8 << 8) | r8;
}

#define SCALE 3
void screen_init(void)
{
    initgraph(TFTx*SCALE,TFTy*SCALE);
    setlinecolor(BLACK);
    HWND hwnd = GetHWnd();
    MoveWindow(hwnd, 300, 100,TFTx*SCALE,TFTy*SCALE+10*SCALE, TRUE);
}
static uint16_t sSx,sSy,sEx,sEy,sCx,sCy;
void screen_setcanvas(uintxy Gx,uintxy Gy, uintxy width,uintxy height)
{
    sSx = Gx; sSy = Gy; sEx=Gx+width-1; sEy=Gy+height-1; sCx=Gx; sCy=Gy;
}
static void movecursor(void)
{
    sCx++;
    if(sCx > sEx){
        sCx = sSx;
        sCy++;
    }
}
void screen_setcolorpixels(const KSCCOLOR* color,uint16_t num)
{
    while(num--){
        KSCCOLOR ncolor = *color++;
        ncolor = (ncolor)>>8| (ncolor<<8);
        setfillcolor(color16to24(ncolor));
        solidrectangle(sCx*SCALE,sCy*SCALE,sCx*SCALE+SCALE,sCy*SCALE+SCALE);
        movecursor();
    }
}
#endif

#endif /* __USE_LCD__ */
