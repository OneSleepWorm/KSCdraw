/**
 * @file    ticker.c
 * @note    周期时间戳打印应用 (STM32)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  ticker
 * 依赖:    tim_clock_2 + uart_printf_1
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
 * 资源占用 (对比: 移除 ticker.o 后固件尺寸差值)
 * ============================================================
 *   ROM(Debug -O0):   352 B
 *   ROM(Release -Os): ~200 B
 *   RAM(静态):  0 B
 *   RAM(堆):    0 B (无 osmalloc)
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
    ddioctl(app->dd1, "[%lu] tick\r\n", t);
    return NULL;
}

static int ticker_open(app_t* app)
{
    ddopen(app->dd1);
    app->dd0->callback = ticker_cb;
    app->dd0->user_data = app;
    ddopen(app->dd0);
    ddwrite(app->dd0, NULL, 1000, 0);
    ddwrite(app->dd0, NULL, 1,    1);
    return 0;
}

static int ticker_close(app_t* app)
{
    ddwrite(app->dd0, NULL, 0, 1);
    return 0;
}

static const papp_ops_t ticker_ops = {
    .open  = ticker_open,
    .close = ticker_close,
};

REGISTER_APP("ticker", "2\0tim_clock_2\0uart_printf_1",
             &ticker_ops, "Periodic timestamp via UART");

#endif
