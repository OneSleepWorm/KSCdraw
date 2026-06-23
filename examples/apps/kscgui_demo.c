#include "master.h"
#include "app.h"
#include "KSCOSsystem.h"
#include "KSCimg.h"

static const uint8_t smiley[8] = {
    0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C
};

int main(void)
{
    bus_init();
    sys_init();
    kscprintf("KSCGUI demo start\r\n");

    app_t* gui = appget("KSCGUI2");
    if (!gui) { kscprintf("FAIL: no KSCGUI2\r\n"); while (1); }
    if (appopen(gui) < 0) { kscprintf("FAIL: appopen\r\n"); while (1); }

    /*--- init ---*/
    appioctl(gui, "init");
    appioctl(gui, "clear", 0x0000);
    sysdelay(200);

    /*--- string + char ---*/
    appioctl(gui, "string", 10, 5, "KSCGUI Demo", 0xFFFF, 0x0000);
    appioctl(gui, "string", 10, 15, "abcdefghijklmnopqrstuvwxyz", 0x7BEF, 0x0000);
    sysdelay(300);

    /*--- fill ---*/
    appioctl(gui, "fill", 10, 30, 50, 30, 0xF800);
    appioctl(gui, "fill", 65, 30, 50, 30, 0x07E0);
    appioctl(gui, "fill", 120, 30, 50, 30, 0x001F);
    appioctl(gui, "fill", 175, 30, 55, 30, 0xFFE0);
    sysdelay(300);

    /*--- rect ---*/
    appioctl(gui, "rect", 10, 65, 50, 30, 0xFFFF);
    appioctl(gui, "rect", 65, 65, 50, 30, 0xFFFF);
    appioctl(gui, "rect", 120, 65, 50, 30, 0xFFFF);
    appioctl(gui, "rect", 175, 65, 55, 30, 0xFFFF);
    sysdelay(300);

    /*--- circle + fcircle ---*/
    appioctl(gui, "circle", 35, 130, 20, 0xFFFF);
    appioctl(gui, "fcircle", 90, 130, 20, 0xF800);
    sysdelay(300);

    /*--- line ---*/
    appioctl(gui, "line", 120, 110, 220, 150, 0x07E0);
    appioctl(gui, "line", 120, 150, 220, 110, 0x07E0);
    appioctl(gui, "line", 120, 110, 120, 150, 0xFFFF);
    appioctl(gui, "line", 220, 110, 220, 150, 0xFFFF);
    sysdelay(300);

    /*--- arc ---*/
    appioctl(gui, "arc", 35, 190, 20, 0x01 | 0x02, 0xFFE0);
    appioctl(gui, "arc", 90, 190, 20, 0x04 | 0x08, 0x07FF);
    sysdelay(300);

    /*--- rrect + frrect ---*/
    appioctl(gui, "rrect", 120, 170, 100, 40, 8, 0xFFFF);
    appioctl(gui, "frrect", 125, 175, 90, 30, 6, 0x1F00);
    sysdelay(300);

    /*--- pixel ---*/
    for (uint8_t i = 0; i < 30; i++) {
        appioctl(gui, "pixel", 10 + i * 2, 220, 0xFFFF);
        appioctl(gui, "pixel", 11 + i * 2, 221, 0xF800);
    }
    sysdelay(300);

    /*--- image (1:1, 16x16 icon, skip 8-byte header) ---*/
    appioctl(gui, "image", 10, 230, 16, 16, Wechat + 8);
    appioctl(gui, "image", 30, 230, 16, 16, QQ + 8);
    appioctl(gui, "image", 50, 230, 16, 16, Setting + 8);
    appioctl(gui, "image", 70, 230, 16, 16, Clock + 8);
    sysdelay(300);

    /*--- ibig (scaled) ---*/
    appioctl(gui, "ibig", 100, 230, 40, 40, 2, OneSleepWorm);
    sysdelay(300);

    /*--- ibin (1-bit) ---*/
    appioctl(gui, "ibin", 10, 280, 8, 8, smiley, 0xFFFF, 0x0000);
    appioctl(gui, "ibin", 10, 292, 8, 8, smiley, 0x07E0, 0x0000);
    appioctl(gui, "ibin", 10, 304, 8, 8, smiley, 0xF800, 0x0000);

    appioctl(gui, "string", 30, 280, "ibin", 0xFFFF, 0x0000);
    appioctl(gui, "string", 30, 290, "image", 0xFFFF, 0x0000);
    appioctl(gui, "string", 30, 300, "ibig", 0xFFFF, 0x0000);

    kscprintf("KSCGUI demo done\r\n");

    while (1) sysdelay(1000);
}
