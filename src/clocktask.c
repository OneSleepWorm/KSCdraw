#include "../inc/KSCconfig.h"

#if __USE_CLOCK_TASK__
#include "../inc/clocktask.h"
#include "../inc/KSCOSsystem.h"

#define default_clock_task_cycle 1000
#if __USE_STM32__
#include "stm32f1xx_hal.h"
#include "../third_party/stm32/inc/stm32f1xx_hal_tim.h"
#include "../third_party/stm32/inc/stm32f1xx_hal_tim_ex.h"
#include "../inc/KSCOSsystem.h"

// 任务名称
const uint32_t stm_clock_task_id =0x00000001;
// 任务结束标志
static ki8 stm_clock_task_running_flag = 0;

#define KDelay HAL_Delay

TIM_HandleTypeDef htim2;
__volatile clock_task_t stm_clock_task;
__volatile clock_task_t* now_clock_task;

CTASK_INIT MX_TIM2_Init(clock_task_t* task)
{
    now_clock_task = task;
    __HAL_RCC_TIM2_CLK_ENABLE();        // 使能TIM2时钟

    htim2.Instance = TIM2;
    
    htim2.Init.Prescaler = KSCOSsystem_Clock / 10000 - 1;    // 预分频器：72MHz / 7200 = 10kHz
    htim2.Init.Period = 10*task->task_cycle - 1;      // 自动重载值：10kHz / 10000 = 1Hz (1秒)

    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
    {
        KSCOS_Error_Handler();
    }

    // 使能定时器更新中断
    HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    return 0;
}

// 弱回调函数，在中断发生时调用
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        now_clock_task->callback(now_clock_task->user_data);
    }
}

// (废弃 — 改用 pdrvs/tim_clocktask.c 的 register-level ISR)
// void TIM2_IRQHandler(void)
// {
//     HAL_TIM_IRQHandler(&htim2);
// }

CTASK_RUN clock_task_run(clock_task_t* task){
    HAL_TIM_Base_Start_IT(&htim2);
    stm_clock_task_running_flag = 1;
    return 0;
}

CTASK_STOP clock_task_stop(clock_task_t* task){
    HAL_TIM_Base_Stop_IT(&htim2);
    stm_clock_task_running_flag = 0;
    return 0;
}
static const CTASK_INIT_FUNC default_init = MX_TIM2_Init;
static const CTASK_CALLBACK_FUNC default_callback = KSCOS_default_Error_Handler;
const clock_task_t clock_default_task = (clock_task_t){default_init,clock_task_run,default_callback
    ,clock_task_stop,0,default_clock_task_cycle,0,0};

#endif
#if __USE_PC__
#include <windows.h>
#include <pthread.h>
// 任务周期，单位：毫秒
// 任务结束标志
static ki8 pc_clock_task_running_flag = 0;

// 任务线程函数
static void* task_funtion(void* task){
    clock_task_t* taskp = (clock_task_t*)task;
    while(taskp->state == 1){
        Sleep(taskp->task_cycle);
        taskp->callback(taskp->user_data);
    }
}

CTASK_RUN clock_task_run(clock_task_t* task){
        pthread_t thread_id;
        pthread_create(&thread_id, NULL
        , task_funtion, (void*)task);
        task->state = 1;
        pthread_detach(thread_id);
        return 0;
}

CTASK_STOP clock_task_stop(clock_task_t* task){
    task->state = 0;
    return 0;
}

// static ki8 pc_default_handler(void* data) { (void)data; return -1; }
static const ki8 clock_task_init(clock_task_t* task){
    return 0;
}
static const ki8 clock_task_callback(void* data){
    return 0;
}
#define default_init clock_task_init
#define default_callback clock_task_callback

const clock_task_t clock_default_task = (clock_task_t){clock_task_init
    ,clock_task_run ,clock_task_callback
    ,clock_task_stop,0,default_clock_task_cycle,0,0};

#endif
static uint8_t default_task_id = 1;
#define default_task_id() default_task_id++
clock_task_t  clock_task_create(CTASK_INIT_FUNC init,CTASK_CALLBACK_FUNC callback
    ,void* user_data,uint16_t task_cycle){
        if(init == NULL){
            init = default_init;
        }
        if(callback == NULL){
            callback = default_callback;
        }
               clock_task_t task={
            .task_id = default_task_id(),
            .init = init,
            .run = clock_task_run,
            .callback = callback,
            .stop = clock_task_stop,
            .user_data = user_data,
            .task_cycle = task_cycle,
        };
        return task;
}


#endif
