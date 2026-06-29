#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#if __USE_STM32__
#include "stm32f1xx.h"

typedef struct {
    void_func_t cb[4];
    void*       ud[4];
    uint8_t     enabled;
} tim_ctx_t;

static app_t* tim_owners[4];

static TIM_TypeDef* tim_reg(uint8_t inst)
{
    static TIM_TypeDef* const map[] = {TIM1, TIM2, TIM3, TIM4};
    if (inst < 1 || inst > 4) return NULL;
    return map[inst - 1];
}

static const IRQn_Type irq_map[] = {
    TIM1_UP_IRQn, TIM2_IRQn, TIM3_IRQn, TIM4_IRQn,
};

static void rcc_enable(uint8_t inst)
{
    if (inst == 1)
        RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    else if (inst == 2)
        RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    else if (inst == 3)
        RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    else
        RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    (void)RCC->APB2ENR;
}

static void inst_init(app_t* app, tim_ctx_t* ctx, uint8_t inst)
{
    uint8_t bit = (uint8_t)(1 << (inst - 1));
    if (ctx->enabled & bit) return;

    ctx->cb[inst - 1] = app->callback;
    ctx->ud[inst - 1] = app->user_data;

    rcc_enable(inst);

    TIM_TypeDef* tim = tim_reg(inst);
    uint32_t period = app->user_data ? (uint32_t)(uintptr_t)app->user_data : 1000;
    uint32_t psc = KSCOSsystem_Clock / 10000 - 1;
    uint32_t arr = 10 * period - 1;
    tim->PSC = (uint16_t)psc;
    tim->ARR = (uint16_t)arr;
    tim->EGR |= TIM_EGR_UG;
    tim->CNT = 0;
    tim->SR = ~TIM_SR_UIF;

    NVIC_SetPriority(irq_map[inst - 1], 0);
    NVIC_EnableIRQ(irq_map[inst - 1]);

    tim_owners[inst - 1] = app;
    ctx->enabled |= bit;
}

static int tim_app_open(app_t* app)
{
    tim_ctx_t* ctx = (tim_ctx_t*)osmalloc(sizeof(tim_ctx_t));
    if (!ctx) return -1;
    for (int i = 0; i < 4; i++) {
        ctx->cb[i] = NULL;
        ctx->ud[i] = NULL;
    }
    ctx->enabled = 0;
    app->app_data = ctx;
    return 0;
}

static int tim_app_close(app_t* app)
{
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!ctx) return -1;
    for (int i = 0; i < 4; i++) {
        if (ctx->enabled & (1 << i)) {
            TIM_TypeDef* tim = tim_reg((uint8_t)(i + 1));
            if (tim) {
                tim->CR1 &= ~TIM_CR1_CEN;
                tim->DIER &= ~TIM_DIER_UIE;
            }
            NVIC_DisableIRQ(irq_map[i]);
            tim_owners[i] = NULL;
        }
    }
    osfree(ctx);
    app->app_data = NULL;
    return 0;
}

static int tim_app_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)data;
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!ctx) return -1;

    uint32_t inst = mode >> 4;
    uint32_t op   = mode & 0x0F;
    if (inst < 1 || inst > 4) return -1;

    inst_init(app, ctx, (uint8_t)inst);
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;

    TIM_TypeDef* tim = tim_reg((uint8_t)inst);

    switch (op) {
    case 0:
        return 1;

    case 1: {
        uint32_t psc = KSCOSsystem_Clock / 10000 - 1;
        uint32_t arr = 10 * count - 1;
        tim->PSC = (uint16_t)psc;
        tim->ARR = (uint16_t)arr;
        tim->EGR |= TIM_EGR_UG;
        return 1;
    }

    case 2:
        if (count) {
            tim->CNT = 0;
            tim->SR = ~TIM_SR_UIF;
            tim->DIER |= TIM_DIER_UIE;
            tim->CR1 |= TIM_CR1_CEN;
        } else {
            tim->CR1 &= ~TIM_CR1_CEN;
            tim->DIER &= ~TIM_DIER_UIE;
        }
        return 1;

    default:
        return -1;
    }
}

static int tim_app_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)count;
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!ctx || !data) return -1;

    uint32_t inst = mode;
    if (inst < 1 || inst > 4) return -1;
    if (!(ctx->enabled & (1 << (inst - 1)))) return -1;

    TIM_TypeDef* tim = tim_reg((uint8_t)inst);
    *(uint32_t*)data = (uint32_t)((tim->ARR + 1) / 10);
    return 4;
}

static const papp_ops_t tim_clock_ops = {
    .open  = tim_app_open,
    .close = tim_app_close,
    .write = tim_app_write,
    .read  = tim_app_read,
};

REGISTER_APP("tim_clock", "0", &tim_clock_ops,
    "TIM1-4 clock app (instance via mode=(inst<<4)|op)");

static void tim_irq_handler(int idx)
{
    app_t* app = tim_owners[idx];
    if (!app) return;
    tim_ctx_t* ctx = (tim_ctx_t*)app->app_data;
    if (!ctx) return;

    TIM_TypeDef* tim = tim_reg((uint8_t)(idx + 1));
    if (tim->SR & TIM_SR_UIF) {
        tim->SR = ~TIM_SR_UIF;
        if ((tim->CR1 & TIM_CR1_CEN) && ctx->cb[idx])
            ctx->cb[idx](ctx->ud[idx]);
    }
}

void TIM1_UP_IRQHandler(void) { tim_irq_handler(0); }
void TIM2_IRQHandler(void)    { tim_irq_handler(1); }
void TIM3_IRQHandler(void)    { tim_irq_handler(2); }
void TIM4_IRQHandler(void)    { tim_irq_handler(3); }

#endif
