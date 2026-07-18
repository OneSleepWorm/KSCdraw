/**
 * @file    button16.c
 * @note    4×4 矩阵键盘扫描应用 (STM32 + PC)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  button16
 * 平台:    STM32 (__USE_STM32__) + PC (__USE_PC__)
 *
 * ============================================================
 * 官方 4×4 键位布局 (1-indexed)
 * ============================================================
 *
 *    ┌────┬────┬────┬────┐
 *    │  1 │  2 │  3 │  4 │   ← 1=退出
 *    ├────┼────┼────┼────┤
 *    │  5 │  6 │  7↑│  8 │   ← 7=上
 *    ├────┼────┼────┼────┤
 *    │  9 │ 10←│ 11○│ 12→│   ← 10=左  11=确认/暂停  12=右
 *    ├────┼────┼────┼────┤
 *    │ 13 │ 14 │ 15↓│ 16 │   ← 15=下
 *    └────┴────┴────┴────┘
 *
 *   2 3 4 5 9 13 = 自定义扩展按键
 *
 * ============================================================
 * 使用方法
 * ============================================================
 *   app_t* kpd = appget("button16");
 *   appopen(kpd);
 *   appwrite(kpd, NULL, 0, 1);          // 初始化
 *   uint32_t iv = 50;
 *   appwrite(kpd, &iv, 1, 2);           // 启动扫描
 *   uint32_t ev;
 *   appread(kpd, &ev, 0, 3);            // 弹事件
 *   appclose(kpd);
 *
 * 完整接口见 `appwrite / appread mode 表` 及 `cmd 表`。
 */

#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdlib.h>

/* ========================================================================
 *  共享常量
 * ======================================================================== */

#define KEY_PRESS     0
#define KEY_RELEASE   1
#define KEY_HOLD      2
#define KEY_LONG      3
#define KEY_DBLCLICK  4

#define STATE_IDLE    0
#define STATE_DOWN    1
#define STATE_HOLD    2

#define TICK_MS       50
#define HOLD_TICKS_DEF 4
#define HOLD_GAP_DEF  1
#define LONG_TICKS    20
#define DBL_TICKS     8

#define EV_QUEUE_SIZE 16

#define EV_PACK(key, type) ((1U << 31) | ((uint32_t)(key) << 4) | (uint32_t)(type))

/* ========================================================================
 *  共享类型
 * ======================================================================== */

typedef struct {
    uint32_t ev_buf[EV_QUEUE_SIZE];
    uint8_t  ev_head;
    uint8_t  ev_tail;
    uint8_t  ev_count;
    uint8_t  states[16];
    uint8_t  flags[16];
    uint8_t  down_ticks[16];
    uint8_t  click_timers[16];
    uint32_t prev_raw;
    uint8_t  hold_ticks;
    uint8_t  hold_gap;
} btn16_cpx_t;

typedef struct {
    uint32_t interval_ms;
    uint8_t  hw_inited;
    uint8_t  timer_started;
    uint8_t  complex_mode;
    uint8_t  _pad;
    uint32_t latest_keys;
    btn16_cpx_t* cpx;
    uint32_t rd_val;
} btn16_data_t;

/* ========================================================================
 *  共享 — 事件队列
 * ======================================================================== */

static void ev_push(btn16_cpx_t* c, uint32_t ev)
{
    if (c->ev_count < EV_QUEUE_SIZE) {
        c->ev_buf[c->ev_head] = ev;
        c->ev_head = (c->ev_head + 1) & (EV_QUEUE_SIZE - 1);
        c->ev_count++;
    }
}

static uint32_t ev_pop(btn16_cpx_t* c)
{
    if (c->ev_count == 0) return 0;
    uint32_t ev = c->ev_buf[c->ev_tail];
    c->ev_tail = (c->ev_tail + 1) & (EV_QUEUE_SIZE - 1);
    c->ev_count--;
    return ev;
}

/* ========================================================================
 *  STM32 平台 — 键扫描
 * ======================================================================== */

#if __USE_STM32__

#include "stm32f1xx.h"

static uint32_t keypad_scan(app_t* gpio)
{
    uint32_t raw = 0;
    for (int row = 0; row < 4; row++) {
        uint32_t set = 1 << row;
        appwrite(gpio, &set, 0x0F, 3);
        uint32_t idr;
        appread(gpio, &idr, 0, 1);
        raw |= ((idr >> 4) & 0x0F) << (row * 4);
    }
    return raw;
}

/* ========================================================================
 *  PC 平台 — 键扫描
 * ======================================================================== */

#elif __USE_PC__

/*   0:'1'=quit  1:'2'  2:'3'  3:'4'
 *   4:'Q'  5:'W'  6:'E'=up  7:'R'
 *   8:'A'  9:'S'=left 10:'D'=OK 11:'F'=right
 *  12:'Z' 13:'X' 14:'C'=down 15:'V'
 *
 *  2-indexed: 在 PC 上模拟 STM32 官方布局, 键位索引 = pc_key_map[i]
 */
static const int pc_key_map[16] = {
    '1', '2', '3', '4',
    'Q', 'W', 'E', 'R',
    'A', 'S', 'D', 'F',
    'Z', 'X', 'C', 'V'
};

static uint32_t keypad_scan(app_t* gpio)
{
    (void)gpio;
    /* ensure message queue exists for GetAsyncKeyState */
    static int msgq_init = 0;
    if (!msgq_init) {
        MSG msg;
        PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);
        msgq_init = 1;
    }
    uint32_t raw = 0;
    for (int i = 0; i < 16; i++)
        if (GetAsyncKeyState(pc_key_map[i]) & 0x8000)
            raw |= (1U << i);
    return raw;
}

#endif

/* ========================================================================
 *  共享 — 定时器扫描回调 (复杂模式 + 简单模式)
 * ======================================================================== */

static void* scan_cb(void* data)
{
    app_t* app = (app_t*)data;
    btn16_data_t* d = (btn16_data_t*)app->app_data;
#if __USE_STM32__
    if (!d->hw_inited) return NULL;
#endif

    uint32_t raw = keypad_scan(app->app0);

    if (d->complex_mode && d->cpx) {
        btn16_cpx_t* c = d->cpx;
        uint32_t released = d->latest_keys & ~raw;
        uint32_t pressed  = ~d->latest_keys & raw;
        d->latest_keys = raw;

        for (int i = 0; i < 16; i++) {
            uint32_t bit = 1U << i;

            if (pressed & bit) {
                if (c->click_timers[i] > 0 && c->click_timers[i] < DBL_TICKS) {
                    ev_push(c, EV_PACK(i, KEY_DBLCLICK));
                    c->click_timers[i] = 0;
                } else {
                    ev_push(c, EV_PACK(i, KEY_PRESS));
                    c->click_timers[i] = 1;
                }
                c->states[i] = STATE_DOWN;
                c->down_ticks[i] = 0;
                c->flags[i] &= ~1;
            }

            if (released & bit) {
                if (c->states[i] != STATE_HOLD)
                    c->click_timers[i] = 1;
                c->states[i] = STATE_IDLE;
                ev_push(c, EV_PACK(i, KEY_RELEASE));
            }

            if (c->states[i] == STATE_DOWN) {
                c->down_ticks[i]++;
                if (c->down_ticks[i] >= c->hold_ticks) {
                    c->states[i] = STATE_HOLD;
                    ev_push(c, EV_PACK(i, KEY_HOLD));
                }
            }

            if (c->states[i] == STATE_HOLD) {
                c->down_ticks[i]++;
                if (c->down_ticks[i] - c->hold_ticks >= c->hold_gap) {
                    c->down_ticks[i] = c->hold_ticks;
                    ev_push(c, EV_PACK(i, KEY_HOLD));
                }
                if (c->down_ticks[i] >= LONG_TICKS && !(c->flags[i] & 1)) {
                    c->flags[i] |= 1;
                    ev_push(c, EV_PACK(i, KEY_LONG));
                }
            }

            if (c->states[i] == STATE_IDLE && c->click_timers[i] > 0) {
                c->click_timers[i]++;
                if (c->click_timers[i] >= DBL_TICKS)
                    c->click_timers[i] = 0;
            }
        }
    } else {
        d->latest_keys = raw;
    }
    return NULL;
}

/* ========================================================================
 *  共享 — App 生命周期
 * ======================================================================== */

static int btn16_open(app_t* app)
{
    btn16_data_t* d = (btn16_data_t*)osmalloc(sizeof(btn16_data_t));
    if (!d) return -1;
    memset(d, 0, sizeof(btn16_data_t));
    d->complex_mode = 1;
    d->cpx = (btn16_cpx_t*)osmalloc(sizeof(btn16_cpx_t));
    if (d->cpx) {
        memset(d->cpx, 0, sizeof(btn16_cpx_t));
        d->cpx->hold_ticks = HOLD_TICKS_DEF;
        d->cpx->hold_gap   = HOLD_GAP_DEF;
    }
    app->app_data = d;
    app->callback_data = &d->rd_val;
    return 0;
}

static int btn16_close(app_t* app)
{
    btn16_data_t* d = (btn16_data_t*)app->app_data;
    if (d->timer_started) {
        app_t* tim = appget("tim_clock");
        appwrite(tim, NULL, 0, 0x32);
    }
    if (d->cpx) osfree(d->cpx);
    if (app->app_data) osfree(app->app_data);
    app->callback_data = NULL;
    return 0;
}

/* ========================================================================
 *  共享 — appread
 * ======================================================================== */

static int btn16_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)count;
    btn16_data_t* d = (btn16_data_t*)app->app_data;
    if (mode == 1 && data) {
        *(uint32_t*)data = d->latest_keys;
        return 4;
    }
    if (mode == 2 && data) {
        *(uint32_t*)data = d->interval_ms;
        return 4;
    }
    if (mode == 3 && data && d->complex_mode && d->cpx) {
        uint32_t ev = ev_pop(d->cpx);
        if (ev == 0) return 0;
        *(uint32_t*)data = ev;
        return 4;
    }
    return 0;
}

/* ========================================================================
 *  共享 — appwrite (mode=1 需平台 #if)
 * ======================================================================== */

static int btn16_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    btn16_data_t* d = (btn16_data_t*)app->app_data;

    if (mode == 1 && !d->hw_inited) {
#if __USE_STM32__
        if (app->app0) appopen(app->app0);
        for (int p = 0; p < 8; p++) {
            uint32_t nib = (p < 4) ? 0x3 : 0x8;
            appwrite(app->app0, NULL, (p << 4) | nib, 1);
        }
        uint32_t zero = 0;
        appwrite(app->app0, &zero, 0x00FF, 3);
        kscprintf("button16: gpio_port init done, PORTA pins 0-7\r\n");
#endif
        uint32_t raw = keypad_scan(app->app0);
        d->latest_keys = raw;
        if (d->cpx) d->cpx->prev_raw = raw;
        d->hw_inited = 1;
        return 1;
    }

    if (mode == 2 && !d->timer_started && data && count == 1) {
        d->interval_ms = *(uint32_t*)data;
        app_t* tim = appget("tim_clock");
        tim->callback  = scan_cb;
        tim->user_data = app;
        appopen(tim);
        appwrite(tim, NULL, d->interval_ms, 0x31);
        appwrite(tim, NULL, 1, 0x32);
        d->timer_started = 1;
        return 1;
    }

    if (mode == 4 && data && count == 1) {
        uint32_t enable = *(uint32_t*)data;
        if (enable && !d->complex_mode) {
            d->cpx = (btn16_cpx_t*)osmalloc(sizeof(btn16_cpx_t));
            if (d->cpx) {
                memset(d->cpx, 0, sizeof(btn16_cpx_t));
                d->complex_mode = 1;
                uint32_t raw = keypad_scan(app->app0);
                d->latest_keys = raw;
                d->cpx->prev_raw = raw;
            }
        } else if (!enable && d->complex_mode) {
            osfree(d->cpx);
            d->cpx = NULL;
            d->complex_mode = 0;
        }
        return 1;
    }

    if (mode == 5 && data && d->complex_mode && d->cpx) {
        if (count >= 1) d->cpx->hold_ticks = (uint8_t)((uint32_t*)data)[0];
        if (count >= 2) d->cpx->hold_gap   = (uint8_t)((uint32_t*)data)[1];
        return 1;
    }

    return 0;
}

/* ========================================================================
 *  共享 — cmd 处理 (cmd_init 需平台 #if)
 * ======================================================================== */

static int cmd_init(app_t* app, const char** argv)
{
    (void)argv;
    btn16_data_t* d = (btn16_data_t*)app->app_data;
    if (!app || !d || d->hw_inited) return -1;
#if __USE_STM32__
    if (app->app0) appopen(app->app0);
    for (int p = 0; p < 8; p++) {
        uint32_t nib = (p < 4) ? 0x3 : 0x8;
        appwrite(app->app0, NULL, (p << 4) | nib, 1);
    }
    uint32_t zero = 0;
    appwrite(app->app0, &zero, 0x00FF, 3);
#endif
    uint32_t raw = keypad_scan(app->app0);
    d->latest_keys = raw;
    if (d->cpx) d->cpx->prev_raw = raw;
    d->hw_inited = 1;
    return 1;
}

static int cmd_scan(app_t* app, const char** argv)
{
    btn16_data_t* d = (btn16_data_t*)app->app_data;
    if (!app || !d || !APPCMD_HAS(argv, 't')) return -1;
    uint32_t ms = strtoul(argv[APPCMD_ARG('t')], NULL, 0);
    if (ms < 10) return -1;
    app_t* tim = appget("tim_clock");
    d->interval_ms = ms;
    tim->callback = scan_cb;
    tim->user_data = app;
    appopen(tim);
    appwrite(tim, NULL, d->interval_ms, 0x31);
    appwrite(tim, NULL, 1, 0x32);
    d->timer_started = 1;
    return 1;
}

static int cmd_stop(app_t* app, const char** argv)
{
    (void)argv;
    btn16_data_t* d = (btn16_data_t*)app->app_data;
    if (!app || !d) return -1;
    if (d->timer_started) {
        app_t* tim = appget("tim_clock");
        appwrite(tim, NULL, 0, 0x32);
        d->timer_started = 0;
    }
    return 1;
}

static int cmd_cpx(app_t* app, const char** argv)
{
    btn16_data_t* d = (btn16_data_t*)app->app_data;
    if (!app || !d || !APPCMD_HAS(argv, 'e')) return -1;
    uint32_t en = strtoul(argv[APPCMD_ARG('e')], NULL, 0);
    if (en && !d->complex_mode) {
        d->cpx = (btn16_cpx_t*)osmalloc(sizeof(btn16_cpx_t));
        if (d->cpx) {
            memset(d->cpx, 0, sizeof(btn16_cpx_t));
            d->complex_mode = 1;
            uint32_t raw = keypad_scan(app->app0);
            d->latest_keys = raw;
            d->cpx->prev_raw = raw;
        }
    } else if (!en && d->complex_mode) {
        osfree(d->cpx);
        d->cpx = NULL;
        d->complex_mode = 0;
    }
    return 1;
}

static int cmd_hold(app_t* app, const char** argv)
{
    btn16_data_t* d = (btn16_data_t*)app->app_data;
    if (!app || !d || !APPCMD_HAS(argv, 't')) return -1;
    if (!d->complex_mode || !d->cpx) return -1;
    d->cpx->hold_ticks = (uint8_t)strtoul(argv[APPCMD_ARG('t')], NULL, 0);
    if (APPCMD_HAS(argv, 'g'))
        d->cpx->hold_gap = (uint8_t)strtoul(argv[APPCMD_ARG('g')], NULL, 0);
    return 1;
}

static int cmd_rd(app_t* app, const char** argv)
{
    btn16_data_t* d = (btn16_data_t*)app->app_data;
    if (!app || !d) return -1;
    if (APPCMD_HAS(argv, 'k')) {
        if (app->callback_data)
            *(uint32_t*)app->callback_data = d->latest_keys;
        return 1;
    }
    if (APPCMD_HAS(argv, 'i')) {
        if (app->callback_data)
            *(uint32_t*)app->callback_data = d->interval_ms;
        return 1;
    }
    if (APPCMD_HAS(argv, 'e') && d->complex_mode && d->cpx) {
        uint32_t ev = ev_pop(d->cpx);
        if (app->callback_data)
            *(uint32_t*)app->callback_data = ev;
        return ev ? 1 : 0;
    }
    return -1;
}

typedef int (*btn_cmd_h)(app_t*, const char**);
typedef struct { const char* name; btn_cmd_h handler; } btn_cmd_t;

static const btn_cmd_t btn_cmds[] = {
    {"init",  cmd_init},
    {"scan",  cmd_scan},
    {"stop",  cmd_stop},
    {"cpx",   cmd_cpx},
    {"hold",  cmd_hold},
    {"rd",    cmd_rd},
    {NULL, NULL}
};

static int btn16_app_cmd(app_t* app, const char* cmd, const char** argv)
{
    if (!app) return -1;
    for (const btn_cmd_t* e = btn_cmds; e->name; e++) {
        if (strcmp(cmd, e->name) == 0)
            return e->handler(app, argv);
    }
    return -1;
}

static const papp_ops_t btn16_ops = {
    .open  = btn16_open,
    .close = btn16_close,
    .read  = btn16_read,
    .write = btn16_write,
    .cmd   = btn16_app_cmd,
};

#if __USE_STM32__
REGISTER_APP_EX("button16", "0", "1\0gpio_port",
                &btn16_ops, "4x4 matrix keypad scanner");
#elif __USE_PC__
REGISTER_APP_EX("button16", "0", "0",
                &btn16_ops, "4x4 matrix keypad scanner (PC: GetAsyncKeyState)");
#endif
