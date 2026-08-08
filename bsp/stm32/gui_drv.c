/**
 * @file    gui_drv.c
 * @note    KSCGUI 平台驱动 — STM32 (ST7789 via super_spi)
 *
 * 实现 kscgui.h 声明的 kscgui_drv_open/close, 填充 k_draw_device ops 表
 * (init/setcanvas/setcolorpixels/setwindows/setpixels) 并初始化 SPI 屏。
 * 平台私有上下文挂到 kscgui_ctx_t.drv。
 */

#include "../../inc/app.h"
#include "../../inc/KSCOSsystem.h"
#include "../../inc/kscgui.h"
#include "../../apps/app_config.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
    app_t*      sspi;           /* super_spi */
    app_t*      gpio;           /* gpio_port (DC/RST) */
    int         sspi_inst;      /* active SPI instance: 1 or 2 */
    int         sspi_dev;       /* device ID on active instance */
    int         spi_dev[2];     /* [0]=SPI1 dev, [1]=SPI2 dev */
    uint8_t     pixbuf[512];
} kscgui_drv_stm32_t;

static void gui_setcanvas(void* data, uintxy Gx, uintxy Gy, uintxy width, uintxy height)
{
    kscgui_ctx_t* ctx = (kscgui_ctx_t*)data;
    kscgui_drv_stm32_t* drv = (kscgui_drv_stm32_t*)ctx->drv;
    app_t* sspi = drv->sspi;
    uint16_t ex = Gx + width - 1;
    uint16_t ey = Gy + height - 1;
    uint8_t ca[] = {Gx >> 8, Gx & 0xFF, ex >> 8, ex & 0xFF};
    uint8_t ra[] = {Gy >> 8, Gy & 0xFF, ey >> 8, ey & 0xFF};
    uint8_t cmd;
    cmd = 0x2A; appwrite(sspi, &cmd, 1, SSPI_MODE(drv->sspi_inst, drv->sspi_dev, SSPI_SEND_CMD));
    appwrite(sspi, ca, 4, SSPI_MODE(drv->sspi_inst, drv->sspi_dev, SSPI_SEND_DAT));
    cmd = 0x2B; appwrite(sspi, &cmd, 1, SSPI_MODE(drv->sspi_inst, drv->sspi_dev, SSPI_SEND_CMD));
    appwrite(sspi, ra, 4, SSPI_MODE(drv->sspi_inst, drv->sspi_dev, SSPI_SEND_DAT));
    cmd = 0x2C; appwrite(sspi, &cmd, 1, SSPI_MODE(drv->sspi_inst, drv->sspi_dev, SSPI_SEND_CMD));
}

static void gui_pixels(void* data, const KSCCOLOR* colors, uint16_t num)
{
    kscgui_ctx_t* ctx = (kscgui_ctx_t*)data;
    kscgui_drv_stm32_t* drv = (kscgui_drv_stm32_t*)ctx->drv;
    app_t* sspi = drv->sspi;
    uint8_t* pixbuf = drv->pixbuf;
    uint16_t batch = sizeof(drv->pixbuf) / 2;
    while (num) {
        uint16_t n = (num > batch) ? batch : num;
        uint8_t* p = pixbuf;
        for (uint16_t i = 0; i < n; i++) {
            KSCCOLOR c = colors[i];
            *p++ = c >> 8;
            *p++ = c & 0xFF;
        }
        appwrite(sspi, pixbuf, n * 2, SSPI_MODE(drv->sspi_inst, drv->sspi_dev, SSPI_SEND_DAT_DMA));
        colors += n;
        num -= n;
    }
}

static void gui_window_setcanvas(k_draw_device* dev, KSC_window* screen,
    uintxy Gx, uintxy Gy, uintxy width, uintxy height)
{
    gui_setcanvas(dev->data, Gx + screen->ssx, Gy + screen->ssy, width, height);
}

static void gui_window_setpixels(k_draw_device* dev, KSC_window* screen,
    const KSCCOLOR* color, uint16_t num)
{
    (void)screen;
    gui_pixels(dev->data, color, num);
}

static void gui_init_st7789(void* data)
{
    kscgui_ctx_t* ctx = (kscgui_ctx_t*)data;
    kscgui_drv_stm32_t* drv = (kscgui_drv_stm32_t*)ctx->drv;
    app_t* sspi = drv->sspi;
    app_t* gpio = drv->gpio;
    if (!sspi || !gpio) return;

    /* GPIO 配置: DC/RST 直连引脚为推挽输出 */
    appcmd(gpio, "cfg -p 28 -m 3");
    appcmd(gpio, "cfg -p 24 -m 3");
    appcmd(gpio, "cfg -p 25 -m 3");
    appcmd(gpio, "set -p 28 -v 1");
    appcmd(gpio, "set -p 24 -v 1");
    appcmd(gpio, "set -p 25 -v 1");

    appcmd(sspi, "init -i 2");

    /* 硬件复位: RST 拉低→拉高 */
    appcmd(gpio, "set -p 25 -v 0");
    sysdelay(100);
    appcmd(gpio, "set -p 25 -v 1");
    sysdelay(150);

    appwrite(sspi, NULL, 0, SSPI_MODE(drv->sspi_inst, drv->sspi_dev, SSPI_PULSE_R1));
    static const uint8_t init[] = {
        0x11,0,  0x00,0,  0x3A,1,0x05,  0xC5,1,0x1A,
        0x36,1,0x00,  0xB2,5,0x05,0x05,0x00,0x33,0x33,
        0xB7,1,0x05,  0xBB,1,0x3F,  0xC0,1,0x2C,
        0xC2,1,0x01,  0xC3,1,0x0F,  0xC4,1,0x20,
        0xC6,1,0x01,  0xD0,2,0xA4,0xA1,
        0xE8,1,0x03,  0xE9,3,0x09,0x09,0x08,
        0xE0,14,0xD0,0x05,0x09,0x09,0x08,0x14,0x28,0x33,0x3F,0x07,0x13,0x14,0x28,0x30,
        0xE1,14,0xD0,0x05,0x09,0x09,0x08,0x03,0x24,0x32,0x32,0x3B,0x14,0x13,0x28,0x2F,
        0x20,0,  0x00,0,  0x29,0,  0xFF
    };
    const uint8_t* p = init;
    while (*p != 0xFF) {
        uint8_t cmd = *p++;
        uint8_t n = *p++;
        if (cmd == 0x00) { sysdelay(120); continue; }
        appwrite(sspi, &cmd, 1, SSPI_MODE(drv->sspi_inst, drv->sspi_dev, SSPI_SEND_CMD));
        if (n) appwrite(sspi, (void*)p, n, SSPI_MODE(drv->sspi_inst, drv->sspi_dev, SSPI_SEND_DAT));
        p += n;
    }
}

int kscgui_drv_open(kscgui_ctx_t* ctx, app_t* app)
{
    kscgui_drv_stm32_t* drv = (kscgui_drv_stm32_t*)osmalloc(sizeof(kscgui_drv_stm32_t));
    if (!drv) return -1;
    memset(drv, 0, sizeof(*drv));

    drv->sspi = appget("super_spi");
    drv->gpio = appget("gpio_port");
    if (drv->gpio) appopen(drv->gpio);
    if (!drv->sspi) { osfree(drv); return -1; }
    if (appopen(drv->sspi) < 0) { osfree(drv); return -1; }

    drv->spi_dev[0] = appcmd(drv->sspi, "reg -i 1");
    if (drv->spi_dev[0] < 0) { osfree(drv); return -1; }
    sspi_setpin(drv->sspi, 1, drv->spi_dev[0], SSPI_CS,  4);
    sspi_setpin(drv->sspi, 1, drv->spi_dev[0], SSPI_DC,  2);
    sspi_setpin(drv->sspi, 1, drv->spi_dev[0], SSPI_R1,  3);

    drv->spi_dev[1] = appcmd(drv->sspi, "reg -i 2");
    if (drv->spi_dev[1] < 0) { osfree(drv); return -1; }
    sspi_setpin(drv->sspi, 2, drv->spi_dev[1], SSPI_CS, 12);
    sspi_setpin(drv->sspi, 2, drv->spi_dev[1], SSPI_DC,  8);
    sspi_setpin(drv->sspi, 2, drv->spi_dev[1], SSPI_R1,  9);

    drv->sspi_inst = 2;
    drv->sspi_dev  = drv->spi_dev[1];

    ctx->drv = drv;
    ctx->dev.data = ctx;
    ctx->dev.init = gui_init_st7789;
    ctx->dev.setcanvas = gui_setcanvas;
    ctx->dev.setcolorpixels = gui_pixels;
    ctx->dev.setwindows = gui_window_setcanvas;
    ctx->dev.setpixels = gui_window_setpixels;
    kobjdraw_init(&ctx->dev);
    return 0;
}

void kscgui_drv_close(kscgui_ctx_t* ctx)
{
    kscgui_drv_stm32_t* drv = (kscgui_drv_stm32_t*)ctx->drv;
    if (drv) {
        if (drv->sspi) appclose(drv->sspi);
        osfree(drv);
        ctx->drv = NULL;
    }
}
