#ifndef KSC_MEMPOOL_H
#define KSC_MEMPOOL_H

#include <stdint.h>
#include <stddef.h>
#include "KSCconfig.h"

/* ================================================================
 * KSCOS 多档块池 (size-class pool)
 *
 * 固定块大小分档, 每档独立空闲链表。分配零碎片, 地址固定,
 * 归还即复用。池体为静态数组 (bsp/<平台>/mempool.c)。
 * ================================================================ */

/* PC 有 2KB 档 (pc_uart_ctx_t=1256B); STM32 只到 1KB (RAM 紧张) */
#if __USE_PC__
#define MEMPOOL_CLASS_MAX  7
#else
#define MEMPOOL_CLASS_MAX  6
#endif

/* 分配: 返回 ≥ size 的最小档一块, 失败返回 NULL */
void* mempool_alloc(size_t size);
/* 释放: 归还块到所属档 */
void  mempool_free(void* ptr);
/* 初始化池 (system open 时调用) */
void  mempool_init(void);
/* 查询某档使用统计 (调试) */
int   mempool_usage(int cls);

/* 单档统计 */
typedef struct {
    uint32_t block_size;    /* 块大小 */
    uint32_t total;         /* 总块数 */
    uint32_t used;          /* 当前已用 */
    uint32_t peak;          /* 历史峰值 (运行时最大占用) */
    uint32_t alloc_cnt;     /* 累计分配次数 */
    uint32_t free_cnt;      /* 累计释放次数 */
} mempool_class_stat_t;

/* 全池统计 */
typedef struct {
    mempool_class_stat_t cls[MEMPOOL_CLASS_MAX];
    uint32_t alloc_fail;    /* 分配失败次数 (池满/超档) */
    uint32_t bytes_total;   /* 池总字节 */
    uint32_t bytes_used;    /* 当前已用字节 */
    uint32_t bytes_peak;    /* 已用字节峰值 */
} mempool_stat_t;

/* 收集全池统计 (调试) */
void mempool_get_stat(mempool_stat_t* st);

#endif /* KSC_MEMPOOL_H */
