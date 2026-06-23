#include "../inc/app.h"
#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#include "string.h"

static int resolve_deps(const char* dep_str, dd_t** slots)
{
    if (!dep_str || !dep_str[0] || dep_str[0] == '0') return 0;
    int count = dep_str[0] - '0';
    if (count > 4) count = 4;
    const char* p = dep_str + 2;
    int j;
    for (j = 0; j < count; j++) {
        size_t plen = strlen(p);
        if (plen == 0) break;
        dd_t* dd = bus_getdriver(p);
        if (!dd) return -1;
        slots[j] = dd;
        p += plen + 1;
    }
    return j;
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

app_t* appget(const char* name)
{
    if (!name) return NULL;

    size_t app_count = ((const char*)__stop_papp_table - (const char*)__start_papp_table) / sizeof(papp_t);

    for (size_t i = 0; i < app_count; i++) {
        const papp_t* papp = &__start_papp_table[i];
        if (!papp->base || strcmp(name, papp->base->app_name) != 0)
            continue;

        app_t* app = osmalloc(sizeof(app_t));
        if (!app) return NULL;

        app->papp        = papp;
        app->dd0         = NULL;
        app->dd1         = NULL;
        app->dd2         = NULL;
        app->dd3         = NULL;
        app->app0        = NULL;
        app->app1        = NULL;
        app->app2        = NULL;
        app->app3        = NULL;
        app->app_ops     = papp->ops;
        app->callback    = CALLBACK_NULL_FUNC;
        app->app_data    = NULL;
        app->user_data   = NULL;

        dd_t* ddslots[4] = {NULL, NULL, NULL, NULL};
        if (resolve_deps(papp->dep_str, ddslots) < 0) {
            osfree(app);
            return NULL;
        }
        app->dd0 = ddslots[0]; app->dd1 = ddslots[1];
        app->dd2 = ddslots[2]; app->dd3 = ddslots[3];

        if (papp->app_dep_str) {
            app_t* appslots[4] = {NULL, NULL, NULL, NULL};
            if (resolve_app_deps(papp->app_dep_str, appslots) < 0) {
                for (int k = 0; k < 4; k++) if (ddslots[k]) ddclose(ddslots[k]);
                if (appslots[0]) appfree(appslots[0]);
                osfree(app);
                return NULL;
            }
            app->app0 = appslots[0]; app->app1 = appslots[1];
            app->app2 = appslots[2]; app->app3 = appslots[3];
        }

        return app;
    }
    return NULL;
}

int appopen(app_t* app)
{
    if (!app || !app->app_ops || !app->app_ops->open) return -1;
    return app->app_ops->open(app);
}

int appclose(app_t* app)
{
    if (!app || !app->app_ops || !app->app_ops->close) return -1;
    return app->app_ops->close(app);
}

int appread(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    if (!app || !app->app_ops || !app->app_ops->read) return -1;
    return app->app_ops->read(app, data, count, mode);
}

int appwrite(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    if (!app || !app->app_ops || !app->app_ops->write) return -1;
    return app->app_ops->write(app, data, count, mode);
}

int appioctl(app_t* app, const char* fmt, ...)
{
    if (!app || !app->app_ops || !app->app_ops->ioctl) return -1;
    va_list ap;
    va_start(ap, fmt);
    int ret = app->app_ops->ioctl(app, fmt, ap);
    va_end(ap);
    return ret;
}

void appfree(app_t* app)
{
    if (!app) return;
    dd_t* ddslots[4] = {app->dd0, app->dd1, app->dd2, app->dd3};
    for (int i = 0; i < 4; i++) {
        if (ddslots[i]) ddclose(ddslots[i]);
    }
    app_t* appslots[4] = {app->app0, app->app1, app->app2, app->app3};
    for (int i = 0; i < 4; i++) {
        if (appslots[i]) appfree(appslots[i]);
    }
    if (app->app_data) osfree(app->app_data);
    osfree(app);
}
