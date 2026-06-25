#include "master.h"
#include "app.h"
#include "kscgui.h"
#include "KSCOSsystem.h"

#define SCR_W 240
#define SCR_H 320

/* ========== Upper window — basic primitives via objects ========== */

#define WIN0_H 160
static ksc_obj_t upper_objs[] = {
    /* 0: circle center=(30,30) r=15, white */
    { .sdx=15, .sdy=15, .colorck=0xFFFF, .d_and_r=15, ._type=_circle|_active|_visible },
    /* 1: filled circle center=(80,30) r=15, red */
    { .sdx=65, .sdy=15, .colorck=0xF800, .d_and_r=15, ._type=_fillcircle|_active|_visible },
    /* 2: rect outline at (120,10) 45x40, green */
    { .sdx=120, .sdy=10, .width=45, .height=40, .colorck=0x07E0, ._type=_box|_active|_visible },
    /* 3: fill rect at (175,10) 55x40, blue */
    { .sdx=175, .sdy=10, .width=55, .height=40, .colorck=0x001F, ._type=_fillbox|_active|_visible },

    /* 4: line from (10,58) to (230,58), yellow */
    { .sdx=10, .sdy=58, .width=230, .height=58, .colorck=0xFFE0, ._type=_line|_active|_visible },

    /* 5: fill round rect at (10,68) 220x50 r=8, maroon */
    { .sdx=10, .sdy=68, .width=220, .height=50, .colorck=0x7800, .d_and_r=8, ._type=_fillroundrect|_active|_visible },
    /* 6: round rect border at (10,68) 220x50 r=8, white */
    { .sdx=10, .sdy=68, .width=220, .height=50, .colorck=0xFFFF, .d_and_r=8, ._type=_roundrect|_active|_visible },

    /* 7: string "Upper: OBJ primitives" at (20,80), white */
    { .sdx=20, .sdy=80, .colorck=0xFFFF, .data="Upper: OBJ primitives", ._type=_string|_active|_visible },
    /* 8: string "circle rect line round r" at (20,96), cyan */
    { .sdx=20, .sdy=96, .colorck=0x07FF, .data="circle rect line round r", ._type=_string|_active|_visible },

    /* 9: char 'K' at (170,125), white */
    { .sdx=170, .sdy=125, .colorck=0xFFFF, .data="K", ._type=_char|_active|_visible },
    /* 10: string label at (20,125), yellow */
    { .sdx=20, .sdy=125, .colorck=0xFFE0, .data="Char:", ._type=_string|_active|_visible },
};
#define UPPER_CNT (sizeof(upper_objs) / sizeof(upper_objs[0]))

/* ========== Lower window — 9-key keypad via objects ========== */

#define WIN1_Y    WIN0_H
#define WIN1_H    (SCR_H - WIN0_H)
#define KEY_W     60
#define KEY_H     40
#define KEY_GAP_X 10
#define KEY_GAP_Y 10
#define KEY_OFF_X 20
#define KEY_OFF_Y 15
#define KEY_X(c)  (KEY_OFF_X + (c) * (KEY_W + KEY_GAP_X))
#define KEY_Y(r)  (KEY_OFF_Y + (r) * (KEY_H + KEY_GAP_Y))
#define LABEL_X(c) (KEY_X(c) + (KEY_W - 8) / 2)
#define LABEL_Y(r) (KEY_Y(r) + (KEY_H - 8) / 2)

static const char key_labels[9] = { '1','2','3','4','5','6','7','8','9' };

static ksc_obj_t key_objs[18];

static void build_keypad(void)
{
    for (uint8_t i = 0; i < 9; i++) {
        uint8_t row = i / 3;
        uint8_t col = i % 3;
        uint8_t bi = i * 2;      /* fillbox index */
        uint8_t si = i * 2 + 1;  /* string index */

        key_objs[bi].sdx = KEY_X(col);
        key_objs[bi].sdy = KEY_Y(row);
        key_objs[bi].width = KEY_W;
        key_objs[bi].height = KEY_H;
        key_objs[bi].colorck = 0x3AEF;   /* blue-gray key face */
        key_objs[bi]._type = _fillbox | _active | _visible;

        key_objs[si].sdx = LABEL_X(col);
        key_objs[si].sdy = LABEL_Y(row);
        key_objs[si].colorck = 0xFFFF;
        key_objs[si].data = (void*)&key_labels[i];
        key_objs[si]._type = _char | _active | _visible;
    }
}
#define KEY_CNT (sizeof(key_objs) / sizeof(key_objs[0]))

/* ================================================================ */

int main(void)
{
    bus_init();
    sys_init();
    kscprintf("KSCGUI object demo start\r\n");

    app_t* gui = appget("KSCGUI");
    if (!gui) { kscprintf("FAIL: no KSCGUI\r\n"); while (1); }
    if (appopen(gui) < 0) { kscprintf("FAIL: appopen\r\n"); while (1); }

    /* --- init display (default SPI2) --- */
    appioctl(gui, "init");
    sysdelay(100);

    /* --- create two windows --- */
    int w0 = appioctl(gui, "wcreate", 0, 0, SCR_W, WIN0_H, 0x0841);  /* dark gray */
    int w1 = appioctl(gui, "wcreate", 0, WIN1_Y, SCR_W, WIN1_H, 0x0008); /* dark blue */
    kscprintf("win0=%d win1=%d\r\n", w0, w1);

    /* --- build keypad object array once --- */
    build_keypad();

    /* --- render upper window --- */
    appioctl(gui, "wselect", w0);
    appioctl(gui, "wclear");
    appioctl(gui, "setobjs", (int)UPPER_CNT, upper_objs);
    appioctl(gui, "drawobjs", (int)UPPER_CNT);

    /* --- render lower window --- */
    appioctl(gui, "wselect", w1);
    appioctl(gui, "wclear");
    appioctl(gui, "setobjs", (int)KEY_CNT, key_objs);
    appioctl(gui, "drawobjs", (int)KEY_CNT);

    kscprintf("KSCGUI object demo done\r\n");

    while (1) sysdelay(1000);
}
