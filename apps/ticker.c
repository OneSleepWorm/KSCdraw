/**
 * @file    ticker.c
 * @note    周期时间戳打印应用 (STM32)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  ticker
 * 依赖:    tim_clock (app) + uart_serial (app0)
 * 平台:    STM32 (__USE_STM32__)
 * 
 * ============================================================
 * 用途
 * ============================================================
 * 利用定时器周期性回调，通过 UART 打印当前系统时间戳。
 * 用于验证系统时钟和 UART 驱动是否正常工作。
 * 默认每 1000ms 输出一次 "[ticks] tick\r\n"。
 * 
 * ============================================================
 * 使用方法
 * ============================================================
 * 
 *   app_t* tk = appget("ticker");
 *   if (!tk) while(1);
 *   appopen(tk);  // 启动定时器，开始输出
 *   // ... 串口每秒输出: [1000] tick  [2000] tick ...
 *   appclose(tk);
 * 
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. 依赖 tim_clock_2 (TIM2) 和 uart_printf_1 (USART1)
 * 2. 定时回调中使用 ddioctl 打印，可能影响实时性
 * 3. 打开后立即开始输出，无需额外配置
 * 4. 打印格式: [tick_ms] tick\r\n
 *
 * ============================================================
 * 资源占用 (LTO差分法: 移除 ticker.c 后固件尺寸差值)
 * ============================================================
 *   ROM(Debug -O0):    272 B
 *   ROM(Release -Os):  192 B
 *   RAM(静态):   0 B
 *   RAM(堆):     0 B (无 osmalloc)
 *
 * ============================================================
 * 外部接口
 * ============================================================
 *   appget("ticker") → app_t*
 *   appopen(tk)      : 启动定时器 + 打开 UART, 立即输出
 *   appclose(tk)     : 停止定时器
 *   无 appread/appwrite/appioctl
 * 
 * 典型用法:
 *   app_t* tk = appget("ticker");
 *   appopen(tk);
 *   // 串口每秒输出: [1000] tick  [2000] tick ...
 *   appclose(tk);
 */

#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__

static void* ticker_cb(void* data)
{
    app_t* app = (app_t*)data;
    uint32_t t = sysgettime();
    appioctl(app->app0, "[%lu] tick\r\n", t);
    return NULL;
}

static int ticker_open(app_t* app)
{
    app_t* tim = appget("tim_clock");
    tim->callback = ticker_cb;
    tim->user_data = app;
    appopen(tim);
    appwrite(tim, NULL, 1000, 0x21);
    appwrite(tim, NULL, 1,    0x22);
    return 0;
}

static int ticker_close(app_t* app)
{
    app_t* tim = appget("tim_clock");
    appwrite(tim, NULL, 0, 0x22);
    return 0;
}

static const papp_ops_t ticker_ops = {
    .open  = ticker_open,
    .close = ticker_close,
};

REGISTER_APP_EX("ticker", "0", "1\0uart_serial",
                &ticker_ops, "Periodic timestamp via UART");

#endif
