/**
 * @file    fastsystem.h
 * @note    system app convenience wrappers -- for developers, NOT API
 *
 * Role:
 *   These os-star/sys-star functions are NOT formal API. The only formal interface
 *   is the app series (appget/appopen/appcmd/...) + the fixed SYSTEMAPP
 *   handle. These are pure inline macros wrapping that fixed SYSTEMAPP
 *   handle + appwrite/appread, so developers can access mem/time/idle
 *   services without writing appwrite(SYSTEMAPP,...) boilerplate.
 *
 * vs global functions:
 *   - All static inline, expanded at compile time, produce NO global symbol.
 *   - Semantically forced through SYSTEMAPP handle (single-entry principle).
 *   - Depends on SYSTEMAPP (kscsystem.h) and app series (app.h).
 *
 * Precondition: SYSTEMAPP opened (after sys_init); otherwise calls are no-ops.
 */
#ifndef FASTSYSTEM_H
#define FASTSYSTEM_H

#include "../../../inc/app.h"
#include "../../../inc/kscsystem.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Memory -- dispatched via SYSTEMAPP (mempool)
 *   appwrite(SYSTEMAPP, &ptr, size, 0) = malloc
 *   appwrite(SYSTEMAPP, &ptr, sizeof(ptr), 1) = free
 * ================================================================ */

static inline void* os_malloc(size_t size)
{
    void* p = NULL;
    if (SYSTEMAPP && SYSTEMAPP->app_ops && SYSTEMAPP->app_ops->write)
        appwrite(SYSTEMAPP, &p, (uint32_t)size, 0);
    return p;
}

static inline void os_free(void* ptr)
{
    if (SYSTEMAPP && SYSTEMAPP->app_ops && SYSTEMAPP->app_ops->write)
        appwrite(SYSTEMAPP, &ptr, sizeof(ptr), 1);
}

static inline void* os_calloc(size_t num, size_t size)
{
    size_t bytes = num * size;
    void* p = os_malloc(bytes);
    if (p) {
        uint8_t* b = (uint8_t*)p;
        for (size_t i = 0; i < bytes; i++)
            b[i] = 0;
    }
    return p;
}

/* ================================================================
 * Time -- dispatched via SYSTEMAPP
 *   appwrite(SYSTEMAPP, &ms, 4, 2) = delay
 *   appread(SYSTEMAPP, &t, 4, 0)   = gettime
 *   appwrite(SYSTEMAPP, NULL, 0, 3) = idle
 * ================================================================ */

static inline void os_delay(uint32_t ms)
{
    if (SYSTEMAPP && SYSTEMAPP->app_ops && SYSTEMAPP->app_ops->write)
        appwrite(SYSTEMAPP, &ms, sizeof(ms), 2);
}

static inline uint32_t os_gettime(void)
{
    uint32_t t = 0;
    if (SYSTEMAPP && SYSTEMAPP->app_ops && SYSTEMAPP->app_ops->read)
        appread(SYSTEMAPP, &t, sizeof(t), 0);
    return t;
}

static inline void os_wait_idle(void)
{
    if (SYSTEMAPP && SYSTEMAPP->app_ops && SYSTEMAPP->app_ops->write)
        appwrite(SYSTEMAPP, NULL, 0, 3);
}

/* ================================================================
 * Legacy-name compatibility macros -- map old names (osmalloc/osfree/
 * sysdelay/...) to the new inline wrappers. No global symbols, existing
 * app source does NOT need the 157 call sites rewritten.
 * ================================================================ */
#define osmalloc(sz)      os_malloc(sz)
#define osfree(p)         os_free(p)
#define oscalloc(n, sz)   os_calloc(n, sz)
#define osdelay(ms)       os_delay(ms)
#define sysdelay(ms)      os_delay(ms)
#define sysgettime()      os_gettime()
#define oswait_idle()     os_wait_idle()

#ifdef __cplusplus
}
#endif

#endif /* FASTSYSTEM_H */
