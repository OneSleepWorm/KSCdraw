#include "../inc/app.h"
#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#include "string.h"

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
        app->app_ops     = papp->ops;
        app->callback    = CALLBACK_NULL_FUNC;
        app->app_data = NULL;
        app->user_data   = NULL;

        const char* dep = papp->dep_str;
        if (dep && dep[0] && dep[0] != '0') {
            int dep_count = dep[0] - '0';
            if (dep_count > 4) dep_count = 4;

            const char* p = dep + 2;
            dd_t** slots[4] = {&app->dd0, &app->dd1, &app->dd2, &app->dd3};

            for (int j = 0; j < dep_count; j++) {
                size_t plen = strlen(p);
                if (plen == 0) break;

                dd_t* dd = bus_getdriver(p);
                if (!dd) {
                    for (int k = 0; k < j; k++)
                        ddclose(*slots[k]);
                    osfree(app);
                    return NULL;
                }
                *slots[j] = dd;
                p += plen + 1;
            }
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
    dd_t* slots[4] = {app->dd0, app->dd1, app->dd2, app->dd3};
    for (int i = 0; i < 4; i++) {
        if (slots[i]) ddclose(slots[i]);
    }
    if (app->app_data) osfree(app->app_data);
    osfree(app);
}
