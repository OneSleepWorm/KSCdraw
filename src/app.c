#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include "../inc/kscsystem.h"
#include <string.h>

#ifndef CALLBACK_NULL_FUNC
#define CALLBACK_NULL_FUNC ((void_func_t)0)
#endif

/* ================================================================
 * 应用缓存 — 单例 + 引用计数
 * ================================================================ */
#define APP_STATE_OPENED  (0x01)

typedef struct app_cache_node {
    app_t*   app;
    int      get_refs;          /* appget 调用次数, 未 appfree */
    uint8_t  app_state;         /* bit0=opened, bit1=reserved */
    struct app_cache_node* next;
} app_cache_node_t;

static app_cache_node_t* _app_cache = NULL;

static app_cache_node_t* cache_find(const char* name)
{
    for (app_cache_node_t* p = _app_cache; p; p = p->next)
        if (strcmp(p->app->papp->base->app_name, name) == 0)
            return p;
    return NULL;
}

static app_cache_node_t* cache_find_by_app(const app_t* app)
{
    for (app_cache_node_t* p = _app_cache; p; p = p->next)
        if (p->app == app)
            return p;
    return NULL;
}

static int resolve_app_deps(const char* app_dep_str, app_t** slots)
{
    if (!app_dep_str || !app_dep_str[0] || app_dep_str[0] == '0') return 0;
    int count = app_dep_str[0] - '0';
    if (count > 4) count = 4;
    const char* p = app_dep_str + 2;
    int j;
    for (j = 0; j < count; j++) {
        size_t plen = strlen(p);
        if (plen == 0) break;
        app_t* ca = appget(p);
        if (!ca) return -1;
        slots[j] = ca;
        p += plen + 1;
    }
    return j;
}

/* ================================================================
 * 公共 API
 * ================================================================ */

app_t* appget(const char* name)
{
    if (!name) return NULL;

    /* 1. 查缓存 */
    app_cache_node_t* node = cache_find(name);
    if (node) {
        node->get_refs++;
        return node->app;
    }

    /* 2. 查 papp 表 */
    size_t app_count = ((const char*)__stop_app_table - (const char*)__start_app_table) / sizeof(papp_t);

    for (size_t i = 0; i < app_count; i++) {
        const papp_t* papp = &__start_app_table[i];
        if (!papp->base || strcmp(name, papp->base->app_name) != 0)
            continue;

        /* system app: 固定地址, 不走 osmalloc。A4: 入缓存前直接 open */
        int is_system = (strcmp(name, "system") == 0);

        app_t* app;
        if (is_system) {
            app = SYSTEMAPP;
            if (!app) return NULL;
            memset(app, 0, sizeof(app_t));
        } else {
            app = (app_t*)osmalloc(sizeof(app_t));
            if (!app) return NULL;
        }

        app->papp        = papp;
        app->app0        = NULL;
        app->app1        = NULL;
        app->app2        = NULL;
        app->app3        = NULL;
        app->app_ops     = papp->ops;
        app->user_func    = CALLBACK_NULL_FUNC;
        app->output_fn   = NULL;
        app->output_ctx  = NULL;
        app->app_data    = NULL;
        app->input_data   = NULL;
        app->output_data = NULL;
        app->mode_data   = NULL;

        if (papp->app_dep_str) {
            app_t* appslots[4] = {NULL, NULL, NULL, NULL};
            if (resolve_app_deps(papp->app_dep_str, appslots) < 0) {
                if (appslots[0]) appfree(appslots[0]);
                if (!is_system) osfree(app);
                return NULL;
            }
            app->app0 = appslots[0]; app->app1 = appslots[1];
            app->app2 = appslots[2]; app->app3 = appslots[3];
        }

        /* A4 补丁: system 在入缓存前直接 open (缓存节点未建, 不能走 appopen) */
        if (is_system && papp->ops->open)
            papp->ops->open(app);

        /* 3. 入缓存链表 (system 已 open, 其 malloc 分发可用) */
        node = (app_cache_node_t*)osmalloc(sizeof(app_cache_node_t));
        if (!node) {
            if (!is_system) osfree(app);
            return NULL;
        }
        node->app       = app;
        node->get_refs  = 1;
        node->app_state = is_system ? APP_STATE_OPENED : 0;
        node->next      = _app_cache;
        _app_cache      = node;

        return app;
    }
    return NULL;
}

int appopen(app_t* app)
{
    if (!app) return -1;
    if (!app->app_ops || !app->app_ops->open) return -1;

    /* system app: 已由 appget 自动 open (A4), 幂等返回 */
    if (app == SYSTEMAPP)
        return 0;

    app_cache_node_t* node = cache_find_by_app(app);
    if (!node) return -1;

    if (!(node->app_state & APP_STATE_OPENED)) {
        node->app_state |= APP_STATE_OPENED;
        return app->app_ops->open(app);
    }
    return 0;
}

__attribute__((used, noipa)) int appclose(app_t* app)
{
    if (!app) return -1;

    /* system app 禁止关闭 */
    if (app == SYSTEMAPP)
        return -1;

    if (!app->app_ops || !app->app_ops->close) return -1;

    app_cache_node_t* node = cache_find_by_app(app);
    if (!node) return -1;

    if (node->app_state & APP_STATE_OPENED) {
        node->app_state &= ~APP_STATE_OPENED;
        return app->app_ops->close(app);
    }
    return 0;
}

__attribute__((used, noipa)) int appread(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    if (!app || !app->app_ops || !app->app_ops->read) return -1;
    return app->app_ops->read(app, data, count, mode);
}

int appwrite(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    if (!app || !app->app_ops || !app->app_ops->write) return -1;
    return app->app_ops->write(app, data, count, mode);
}

#define APPCMD_LINE_MAX  128

int appcmd(app_t* app, const char* cmdline)
{
    if (!app || !app->app_ops || !app->app_ops->cmd) return -1;

    static char buf[APPCMD_LINE_MAX];
    static const char* argv[26];

    for (int i = 0; i < 26; i++) argv[i] = NULL;

    strncpy(buf, cmdline, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* p = buf;
    while (*p == ' ') p++;
    if (!*p) return -1;
    char* cmdname = p;
    while (*p && *p != ' ') p++;
    if (*p) *p++ = '\0';

    while (*p) {
        while (*p == ' ') *p++ = '\0';
        if (!*p) break;
        if (*p == '-' && p[1] >= 'a' && p[1] <= 'z') {
            int idx = p[1] - 'a';
            p += 2;
            while (*p == ' ') p++;
            if (!*p) {
                argv[idx] = "";
                break;
            }
            if (!(*p == '-' && (p[1] >= 'a' && p[1] <= 'z'))) {
                if (*p == '"') {
                    p++;
                    argv[idx] = p;
                    char* dst = (char*)argv[idx];
                    while (*p && *p != '"') {
                        if (*p == '\\') {
                            p++;
                            switch (*p) {
                                case '"':  *dst++ = '"'; break;
                                case '\\': *dst++ = '\\'; break;
                                case 'n':  *dst++ = '\n'; break;
                                case 'r':  *dst++ = '\r'; break;
                                case 't':  *dst++ = '\t'; break;
                                case '0':  *dst++ = '\0'; break;
                                default:   *dst++ = '\\'; *dst++ = *p; break;
                            }
                            if (*p) p++;
                        } else {
                            *dst++ = *p++;
                        }
                    }
                    if (*p == '"') p++;
                    *dst = '\0';
                } else if (*p == '\'') {
                    argv[idx] = ++p;
                    while (*p && *p != '\'') p++;
                    if (*p == '\'') *p++ = '\0';
                } else {
                    argv[idx] = p;
                    while (*p && *p != ' ') p++;
                }
            } else {
                argv[idx] = "";
            }
        } else {
            char* ep = p;
            while (*ep && *ep != ' ') ep++;
            int n = (int)(ep - p);
            if (n > 0)
                kscprintf("appcmd: unknown arg '%.*s' (use -p for path?)\r\n", n, p);
            break;
        }
    }

    return app->app_ops->cmd(app, cmdname, argv);
}

__attribute__((used, noipa)) int appcmd_argv(app_t* app, const char* cmdname, const char** argv)
{
    if (!app || !app->app_ops || !app->app_ops->cmd) return -1;
    return app->app_ops->cmd(app, cmdname, argv);
}

__attribute__((used, noipa)) void appfree(app_t* app)
{
    if (!app) return;

    app_cache_node_t* node = cache_find_by_app(app);
    if (node) {
        node->get_refs--;
        if (node->get_refs > 0) return;  /* 还有人引用 */
    }
    /* TODO: 真正的释放逻辑 — get_refs == 0 时从链表移除, 递归清理 */
}

/* ================================================================
 * app 系列函数导出保号表 — 强制保留所有 API 入口的全局符号
 *
 * 背景: STM32 用 -flto + --gc-sections, 未被外部引用的函数会被 LTO
 * 内联消除 (appfree 消失, appread/appclose/appcmd_argv 变局部符号),
 * 导致链接产物里无法稳定查到这些入口。此表通过 used+retain 强制
 * 保号, 使 app*_entry (链接脚本 PROVIDE) 对任何链接方都可见可链接。
 * 表体放在 .data, 不占用 FLASH 代码布局。
 * ================================================================ */
__attribute__((used))
static const void* const _app_api_keep[] = {
    (void*)appget, (void*)appopen, (void*)appclose,
    (void*)appread, (void*)appwrite, (void*)appcmd,
    (void*)appcmd_argv, (void*)appfree,
};
