/**
 * @file    button16.c
 * @note    4×4 矩阵键盘扫描应用 (STM32)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  button16
 * 依赖:    gpio_port (app) + tim_clock (app)
 * 平台:    STM32 (__USE_STM32__)
 * 
 * ============================================================
 * 用途
 * ============================================================
 * 扫描 4×4 矩阵键盘，支持两种读取模式：
 *   - 简单模式: 返回 16-bit raw 位图 (每键 1 bit)
 *   - 复杂模式: 提供事件队列，支持 PRESS/RELEASE/HOLD/LONG/DBLCLICK
 * 
 * 行 (Row0-3) 接 GPIO 低 4 位 (输出)
 * 列 (Col0-3) 接 GPIO 高 4 位 (输入)
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   app_t* kpd = appget("button16");
 *   if (!kpd) while(1);
 *   appopen(kpd);
 * 
 *   // 初始化硬件 (mode=1) + 启动定时扫描 (mode=2)
 *   uint32_t interval = 50;  // 50ms 扫描间隔
 *   appwrite(kpd, NULL, 0, 1);
 *   appwrite(kpd, &interval, 1, 2);
 * 
 *   // 简单模式: 读 raw key state (mode=1)
 *   uint32_t keys;
 *   appread(kpd, &keys, 0, 1);
 *   // keys bit n = 1 表示按键 n 按下
 * 
 *   // 复杂模式: 读事件队列 (mode=3)
 *   uint32_t ev;
 *   int ret = appread(kpd, &ev, 0, 3);
 *   if (ret > 0) {
 *       uint8_t key = (ev >> 4) & 0xF;   // 键号 0-15
 *       uint8_t type = ev & 0xF;          // 事件类型
 *       // type: 0=PRESS 1=RELEASE 2=HOLD 3=LONG 4=DBLCLICK
 *   }
 * 
 * ============================================================
 * appwrite / appread mode 表
 * ============================================================
 * 操作  | mode | data       | count | 功能
 * ------+------+------------+-------+-------------------------------
 * write |  1   | NULL       | 0     | 初始化 GPIO (推挽输出 + 上拉输入)
 * write |  2   | uint32_t*  | 1     | 设扫描间隔(ms), 启动定时器
 * write |  4   | uint32_t*  | 1     | 1=开启复杂模式 0=关闭
 * write |  5   | uint32_t[2]| 1-2   | {hold_ticks, hold_gap} HOLD参数
 * read  |  1   | uint32_t*  | 0     | 返回 latest_keys (16-bit raw)
 * read  |  2   | uint32_t*  | 0     | 返回 interval_ms
 * read  |  3   | uint32_t*  | 0     | 弹出事件 (复杂模式)
 * 
 * ============================================================
 * 事件打包格式 (mode=3)
 * ============================================================
 *   bit 31   = 1 (标志位)
 *   bits 7-4 = 键号 (0-15)
 *   bits 3-0 = 事件类型
 *             0: PRESS   1: RELEASE  2: HOLD
 *             3: LONG    4: DBLCLICK
 * 
 * ============================================================
 * 按键定义
 * ============================================================
 *   行/列映射: Row0-3 = GPIO bits 0-3 (输出)
 *              Col0-3 = GPIO bits 4-7 (输入上拉)
 *   按键编号:  key = row*4 + col  (0-15)
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. 必须先 appwrite(mode=1) 初始化硬件，再 mode=2 启动定时器
 * 2. 复杂模式默认启用；禁用后节省内存 (cpx 结构体 ~60B)
 * 3. 事件队列 16 槽，溢出时新事件丢弃
 * 4. 扫描间隔建议 30-100ms，精度由 tim_clock_3 决定
 * 5. 去抖算法: 按下后 hold_ticks(default 4)=200ms 进入 HOLD,
 *    进入 HOLD 后每 hold_gap(default 1) tick 发一次 HOLD,
 *    LONG_TICKS(20)=1000ms 触发 LONG,
 *    DBL_TICKS(8)=400ms 窗口内第二次按下触发 DBLCLICK
 *    HOLD 参数通过 appwrite(NULL, p, 2, 5) 配置: p[0]=hold_ticks, p[1]=hold_gap
 *
 * ============================================================
 * 资源占用 (LTO差分法: 移除 button16.c 后固件尺寸差值)
 * ============================================================
 *   ROM(Debug -O0):   1,828 B
 *   ROM(Release -Os):   988 B
 *   RAM(静态):   0 B
 *   RAM(堆):     btn16_data_t (~20 B) + btn16_cpx_t (~150 B) = ~170 B
 *
 * ============================================================
 * 外部接口
 * ============================================================
 *   appget("button16") → app_t*
 *   appopen(kpd)       : 分配数据, 默认启用复杂模式
 *   appwrite(mode=1)   : 初始化 GPIO (推挽输出+上拉输入)
 *   appwrite(mode=2)   : 设置扫描间隔并启动 TIM3
 *   appwrite(mode=4)   : 切换复杂模式 (1=开 0=关)
 *   appwrite(mode=5)   : 设置 {hold_ticks, hold_gap}
 *   appread(mode=1)    : 读 latest_keys (16-bit raw bitmap)
 *   appread(mode=2)    : 读 interval_ms
 *   appread(mode=3)    : 弹出一个事件 (复杂模式)
 *   appclose(kpd)      : 停止扫描, 释放内存
 * ============================================================
 * 推荐应用布局
 * ============================================================
 *   0=quit | 1 | 2 | 3 |
 *      5-7,9-11,13-15=direction
 *   4  | 5 |   6=up | 7 | == 
 *   8  | 9=left | 10=ok| 11=right|
 *  12  | 13|   14=down| 15|
 */

#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdlib.h>
#if __USE_STM32__

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

static void* scan_cb(void* data)
{
    app_t* app = (app_t*)data;
    btn16_data_t* d = (btn16_data_t*)app->app_data;
    if (!d->hw_inited) return NULL;

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

static int btn16_open(app_t* app)
{
    btn16_data_t* d = osmalloc(sizeof(btn16_data_t));
    if (!d) return -1;
    d->interval_ms   = 0;
    d->hw_inited     = 0;
    d->timer_started = 0;
    d->complex_mode  = 1;
    d->latest_keys   = 0;
    d->cpx = osmalloc(sizeof(btn16_cpx_t));
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

static int btn16_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    btn16_data_t* d = (btn16_data_t*)app->app_data;
    if (mode == 1 && !d->hw_inited) {
        if (app->app0) appopen(app->app0);
        for (int p = 0; p < 8; p++) {
            uint32_t nib = (p < 4) ? 0x3 : 0x8;
            appwrite(app->app0, NULL, (p << 4) | nib, 1);
        }
        uint32_t zero = 0;
        appwrite(app->app0, &zero, 0x00FF, 3);
        kscprintf("button16: gpio_port init done, PORTA pins 0-7\r\n");
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
            d->cpx = osmalloc(sizeof(btn16_cpx_t));
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

static int cmd_init(app_t* app, const char** argv)
{
    (void)argv;
    btn16_data_t* d = (btn16_data_t*)app->app_data;
    if (!app || !d || d->hw_inited) return -1;
    if (app->app0) appopen(app->app0);
    for (int p = 0; p < 8; p++) {
        uint32_t nib = (p < 4) ? 0x3 : 0x8;
        appwrite(app->app0, NULL, (p << 4) | nib, 1);
    }
    uint32_t zero = 0;
    appwrite(app->app0, &zero, 0x00FF, 3);
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
        d->cpx = osmalloc(sizeof(btn16_cpx_t));
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

REGISTER_APP_EX("button16", "0", "1\0gpio_port",
                &btn16_ops, "4x4 matrix keypad scanner");

#endif
