/**
 * @file    dma.c
 * @note    DMA1 控制器驱动 (STM32)
 * 
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  dma
 * 依赖:    dma1
 * 平台:    STM32 (__USE_STM32__)
 *
 * ============================================================
 * 用途
 * ============================================================
 * 提供 DMA1 通道的使能/禁用、传输完成查询和清除标志。
 * 不涉及通道寄存器配置（CCR/CPAR/CMAR/CNDTR 由各外设驱动
 * 直接操作），dma 驱动只负责通道层面的启停和状态查询。
 *
 * ============================================================
 * 使用方法
 * ============================================================
 *
 *   dd_t* dma = bus_getdriver("dma");
 *   if (!dma) while(1);
 *   ddopen(dma);
 *
 *   // 使能通道 5 (mode=2, count=通道号)
 *   uint32_t ch = 5;
 *   ddwrite(dma, &ch, 5, 2);
 *
 *   // 检查传输完成 (read mode=2)
 *   ddread(dma, &ch, 5, 2);  // ch = 1 完成, 0 未完成
 *
 *   // 查询剩余计数 (read mode=1)
 *   ddread(dma, &ch, 5, 1);  // ch = CNDTR
 *
 *   // 清除 TCIF (write mode=4)
 *   ddwrite(dma, &ch, 5, 4);
 *
 *   // 禁用通道 (write mode=3)
 *   ddwrite(dma, &ch, 5, 3);
 *
 * ============================================================
 * ddwrite / ddread mode 表
 * ============================================================
 * 操作 | mode | data      | count  | 功能
 * ------+------+-----------+--------+---------------------------
 * write |  2   | uint32_t* | ch 1-7 | 使能通道 (CCR_EN=1)
 * write |  3   | uint32_t* | ch 1-7 | 禁用通道 (CCR_EN=0)
 * write |  4   | uint32_t* | ch 1-7 | 清除 TCIF
 * read  |  1   | uint32_t* | ch 1-7 | 读 CNDTR → *data
 * read  |  2   | uint32_t* | ch 1-7 | 查 TCIF → *data (1/0)
 *
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. 通道编号 1-7 (1-based), 与数据手册一致
 * 2. 通道寄存器 (CCR/CPAR/CMAR/CNDTR) 由外设驱动直接配置
 * 3. DMA 时钟由总线自动使能 (RCC_AHBENR.DMA1EN)
 * 4. 使用前确保外设已请求 DMA (如 SPI_CR2.TXDMAEN)
 * 5. DMA1 请求映射 (medium-density):
 *    Ch1: ADC1  Ch2: SPI1_RX  Ch3: SPI1_TX
 *    Ch4: SPI2_RX  Ch5: SPI2_TX  Ch6/7: TIM/USART
 */

#include "../inc/dd.h"
#if __USE_STM32__
#include "stm32f1xx.h"

static DMA_Channel_TypeDef* ch_regs(uint32_t base, uint32_t ch)
{
    return (DMA_Channel_TypeDef*)(base + 0x08 + (ch - 1) * 0x14);
}

static uint32_t tcif_bit(uint32_t ch)  { return 1U << ((ch - 1) * 4 + 1); }
static uint32_t ctcif_bit(uint32_t ch) { return 1U << ((ch - 1) * 4 + 1); }

static int dma_open(dd_t* dd)
{
    (void)dd;
    return 0;
}

static int dma_close(dd_t* dd)
{
    (void)dd;
    return 0;
}

static int dma_write(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    uint32_t base = dd->dev0->private->device_register;
    DMA_TypeDef* dma = (DMA_TypeDef*)base;

    if (mode == 2 && count >= 1 && count <= 7) {
        uint32_t ch = *(uint32_t*)data;
        if (ch > 6) return 0;
        DMA_Channel_TypeDef* chp = ch_regs(base, ch);
        chp->CCR |= DMA_CCR_EN;
        return 1;
    }

    if (mode == 3 && count >= 1 && count <= 7) {
        uint32_t ch = *(uint32_t*)data;
        if (ch > 6) return 0;
        DMA_Channel_TypeDef* chp = ch_regs(base, ch);
        chp->CCR &= ~DMA_CCR_EN;
        return 1;
    }

    if (mode == 4 && count >= 1 && count <= 7) {
        uint32_t ch = *(uint32_t*)data;
        if (ch > 6) return 0;
        dma->IFCR = ctcif_bit(ch);
        return 1;
    }

    return 0;
}

static int dma_read(dd_t* dd, void* data, uint32_t count, uint32_t mode)
{
    uint32_t base = dd->dev0->private->device_register;

    if (mode == 1 && data && count >= 1 && count <= 7) {
        uint32_t ch = *(uint32_t*)data;
        if (ch > 6) return 0;
        DMA_Channel_TypeDef* chp = ch_regs(base, ch);
        *(uint32_t*)data = chp->CNDTR;
        return 4;
    }

    if (mode == 2 && data && count >= 1 && count <= 7) {
        DMA_TypeDef* dma = (DMA_TypeDef*)base;
        uint32_t ch = *(uint32_t*)data;
        if (ch > 6) return 0;
        *(uint32_t*)data = (dma->ISR & tcif_bit(ch)) ? 1 : 0;
        return 4;
    }

    return 0;
}

static const pdrv_ops_t dma_ops = {
    .open  = dma_open,
    .close = dma_close,
    .write = dma_write,
    .read  = dma_read,
};

REGISTER_DRIVER("dma", "1\0dma1", &dma_ops, "DMA1 controller");

#endif
