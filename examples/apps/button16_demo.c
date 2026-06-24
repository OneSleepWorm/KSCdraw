/**
 * @file    button16_demo.c
 * @note    4×4 矩阵键盘扫描应用 — 使用示例 (UART)
 * 
 * ============================================================
 * 功能
 * ============================================================
 * 初始化 4×4 键盘，启动定时扫描(50ms)，
 * 通过 UART 打印按键状态变化和复杂事件。
 * 
 * ============================================================
 * 用法
 * ============================================================
 *   1. 替换 Core/Src/main.c 为本文件
 *   2. cmake --build --preset Debug
 *   3. 烧录后打开串口(115200)观察
 * 
 *   预期 UART 输出 (按下按键 0 然后松开):
 *     button16_demo: start
 *     GPIO init
 *     scan started, interval=50
 *     keys: 0x0001
 *     ev[0] PRESS
 *     keys: 0x0000
 *     ev[0] RELEASE
 *     ...
 *     button16_demo: done
 * 
 * ============================================================
 * 事件类型
 * ============================================================
 *   PRESS     按键按下
 *   RELEASE   按键松开
 *   HOLD      按住超过 200ms
 *   LONG      按住超过 1000ms
 *   DBLCLICK  400ms 内第二次按下
 * 
 * ============================================================
 * 参考
 * ============================================================
 *   button16.c — appwrite/appread mode 说明
 *   KSCconfig.h — __LITTLE_END_COLOR__ = 1
 */

#include "master.h"
#include "app.h"
#include "KSCOSsystem.h"

static const char* ev_type(uint32_t ev)
{
    switch (ev & 0xF) {
    case 0: return "PRESS";
    case 1: return "RELEASE";
    case 2: return "HOLD";
    case 3: return "LONG";
    case 4: return "DBLCLICK";
    default: return "?";
    }
}

int main(void)
{
    bus_init();
    sys_init();
    kscprintf("button16_demo: start\r\n");

    app_t* kpd = appget("button16");
    if (!kpd) { kscprintf("FAIL: no button16\r\n"); while (1); }
    if (appopen(kpd) < 0) { kscprintf("FAIL: appopen\r\n"); while (1); }

    /* init GPIO (mode=1) */
    appwrite(kpd, NULL, 0, 1);
    kscprintf("GPIO init\r\n");

    /* start scan timer: 50ms interval (mode=2) */
    uint32_t interval = 50;
    appwrite(kpd, &interval, 1, 2);
    kscprintf("scan started, interval=%lu\r\n", interval);

    uint32_t last_raw = 0xFFFF;

    for (int i = 0; i < 200; i++) {
        /* read raw key bitmap (mode=1) */
        uint32_t raw = 0;
        appread(kpd, &raw, 0, 1);
        if (raw != last_raw) {
            kscprintf("keys: 0x%04X\r\n", raw);
            last_raw = raw;
        }

        /* pop events (mode=3) */
        uint32_t ev;
        while (appread(kpd, &ev, 0, 3) > 0) {
            uint32_t key = (ev >> 4) & 0xF;
            uint32_t type = ev & 0xF;
            kscprintf("ev[%lu] %s\r\n", key, ev_type(ev));
            (void)type;
        }

        sysdelay(100);
    }

    appclose(kpd);
    kscprintf("button16_demo: done\r\n");

    while (1) sysdelay(1000);
}
