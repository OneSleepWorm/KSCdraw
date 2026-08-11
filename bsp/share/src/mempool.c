/**
 * @file    mempool.c
 * @note    多档块池 (size-class pool) — 固定大小分档, 零碎片
 *
 * 池体为静态数组, 地址运行时固定。分档:
 *   0: 32B  16块   1KB
 *   1: 64B  16块   1KB
 *   2: 128B 16块   2KB
 *   3: 256B 12块   3KB
 *   4: 512B 8块    4KB
 *   5: 1KB  4块    4KB
 *   (PC 额外) 6: 2KB  2块  4KB  — pc_uart_ctx_t 需要
 */

#include "../../inc/mempool.h"

#define CLASS_N  MEMPOOL_CLASS_MAX

#define BLK_0 32
#define BLK_1 64
#define BLK_2 128
#define BLK_3 256
#define BLK_4 512
#define BLK_5 1024

#define CNT_0 16
#define CNT_1 16
#define CNT_2 16
#define CNT_3 12
#define CNT_4 8
#define CNT_5 4

#if __USE_PC__
#define BLK_6 2048
#define CNT_6 2
#endif

static uint8_t pool_0[CNT_0][BLK_0];
static uint8_t pool_1[CNT_1][BLK_1];
static uint8_t pool_2[CNT_2][BLK_2];
static uint8_t pool_3[CNT_3][BLK_3];
static uint8_t pool_4[CNT_4][BLK_4];
static uint8_t pool_5[CNT_5][BLK_5];
#if __USE_PC__
static uint8_t pool_6[CNT_6][BLK_6];
#endif

static const uint32_t class_size[CLASS_N] = {BLK_0, BLK_1, BLK_2, BLK_3, BLK_4, BLK_5
#if __USE_PC__
    , BLK_6
#endif
};
static const uint32_t class_cnt[CLASS_N]  = {CNT_0, CNT_1, CNT_2, CNT_3, CNT_4, CNT_5
#if __USE_PC__
    , CNT_6
#endif
};

typedef struct blk_hdr {
    struct blk_hdr* next;
} blk_hdr_t;

static blk_hdr_t* free_list[CLASS_N];
static uint8_t*  pool_base[CLASS_N];
static uint32_t  used_count[CLASS_N];
static uint32_t  peak_count[CLASS_N];
static uint32_t  alloc_total[CLASS_N];
static uint32_t  free_total[CLASS_N];
static uint32_t  alloc_fail_cnt;
static uint32_t  bytes_used_now;
static uint32_t  bytes_used_peak;

static uint8_t* pool_of(int cls)
{
    switch (cls) {
    case 0: return (uint8_t*)pool_0;
    case 1: return (uint8_t*)pool_1;
    case 2: return (uint8_t*)pool_2;
    case 3: return (uint8_t*)pool_3;
    case 4: return (uint8_t*)pool_4;
    case 5: return (uint8_t*)pool_5;
#if __USE_PC__
    case 6: return (uint8_t*)pool_6;
#endif
    default: return NULL;
    }
}

void mempool_init(void)
{
    alloc_fail_cnt = 0;
    bytes_used_now = 0;
    bytes_used_peak = 0;
    for (int c = 0; c < CLASS_N; c++) {
        free_list[c] = NULL;
        used_count[c] = 0;
        peak_count[c] = 0;
        alloc_total[c] = 0;
        free_total[c] = 0;
        pool_base[c] = pool_of(c);
        for (uint32_t i = 0; i < class_cnt[c]; i++) {
            blk_hdr_t* h = (blk_hdr_t*)(pool_base[c] + i * class_size[c]);
            h->next = free_list[c];
            free_list[c] = h;
        }
    }
}

void* mempool_alloc(size_t size)
{
    for (int c = 0; c < CLASS_N; c++) {
        if (size <= class_size[c]) {
            if (free_list[c]) {
                blk_hdr_t* h = free_list[c];
                free_list[c] = h->next;
                used_count[c]++;
                alloc_total[c]++;
                if (used_count[c] > peak_count[c])
                    peak_count[c] = used_count[c];
                bytes_used_now += class_size[c];
                if (bytes_used_now > bytes_used_peak)
                    bytes_used_peak = bytes_used_now;
                return (void*)h;
            }
            alloc_fail_cnt++;
            return NULL;   /* 该档满 */
        }
    }
    alloc_fail_cnt++;
    return NULL;           /* 超过最大档 */
}

void mempool_free(void* ptr)
{
    if (!ptr) return;
    for (int c = 0; c < CLASS_N; c++) {
        uint32_t base = (uint32_t)(uintptr_t)pool_base[c];
        uint32_t p    = (uint32_t)(uintptr_t)ptr;
        uint32_t end  = base + class_cnt[c] * class_size[c];
        if (p >= base && p < end) {
            blk_hdr_t* h = (blk_hdr_t*)ptr;
            h->next = free_list[c];
            free_list[c] = h;
            used_count[c]--;
            free_total[c]++;
            bytes_used_now -= class_size[c];
            return;
        }
    }
}

int mempool_usage(int cls)
{
    if (cls < 0 || cls >= CLASS_N) return -1;
    return (int)used_count[cls];
}

void mempool_get_stat(mempool_stat_t* st)
{
    if (!st) return;
    st->alloc_fail = alloc_fail_cnt;
    st->bytes_total = 0;
    st->bytes_used = bytes_used_now;
    st->bytes_peak = bytes_used_peak;
    for (int c = 0; c < CLASS_N; c++) {
        st->cls[c].block_size = class_size[c];
        st->cls[c].total      = class_cnt[c];
        st->cls[c].used       = used_count[c];
        st->cls[c].peak       = peak_count[c];
        st->cls[c].alloc_cnt  = alloc_total[c];
        st->cls[c].free_cnt   = free_total[c];
        st->bytes_total += class_size[c] * class_cnt[c];
    }
}
