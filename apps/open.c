#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdio.h>

#if __USE_STM32__

typedef struct {
    const char* ext;
    const char* src_app;
    const char* subcmd;
    const char* default_target;  /* "uart" or "gui" */
} open_route_t;

static const open_route_t routes[] = {
    {".txt", "littlefs", "feed", "uart"},
    {".bmp", "littlefs", "feed", "gui"},
    {NULL,   NULL,       NULL,   NULL}
};

static int open_cmd(app_t* app, const char* cmdname, const char** argv)
{
    (void)app;
    (void)cmdname;
    const char* path = argv[APPCMD_ARG('p')];
    if (!path) return -1;

    const char* ext = strrchr(path, '.');
    if (!ext) ext = "";

    const open_route_t* route = NULL;
    for (const open_route_t* r = routes; r->ext; r++) {
        if (strcmp(ext, r->ext) == 0) { route = r; break; }
    }

    const char* src_name = route ? route->src_app : "littlefs";
    const char* subcmd   = route ? route->subcmd : "info";
    const char* def_tgt  = route ? route->default_target : "uart";

    app_t* src = appget(src_name);
    if (!src) return -1;

    /* Determine target flag */
    const char* tgt = NULL;
    if (APPCMD_HAS(argv, 't')) {
        tgt = argv[APPCMD_ARG('t')];
    } else if (APPCMD_HAS(argv, 'g')) {
        tgt = "gui";
    } else if (APPCMD_HAS(argv, 'u')) {
        tgt = "uart";
    } else {
        tgt = def_tgt;
    }

    /* Build forwarded argv (copy by index, override target) */
    const char* fwd_argv[28];
    for (int i = 0; i < 26; i++)
        fwd_argv[i] = argv[i];
    fwd_argv[26] = NULL;
    fwd_argv[27] = NULL;
    fwd_argv[APPCMD_ARG('g')] = NULL;
    fwd_argv[APPCMD_ARG('u')] = NULL;
    fwd_argv[APPCMD_ARG('t')] = tgt;

    /* Forward output_fn to source app */
    app_output_fn saved_ofn = src->output_fn;
    void* saved_octx = src->output_ctx;
    src->output_fn = app->output_fn;
    src->output_ctx = app->output_ctx;

    int r = appcmd_argv(src, subcmd, fwd_argv);

    src->output_fn = saved_ofn;
    src->output_ctx = saved_octx;
    return r;
}

static int open_open(app_t* app) { (void)app; return 0; }
static int open_close(app_t* app) { (void)app; return 0; }
static int open_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{ (void)app; (void)data; (void)count; (void)mode; return 0; }
static int open_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{ (void)app; (void)data; (void)count; (void)mode; return 0; }

static const papp_ops_t open_ops = {
    .open  = open_open,
    .close = open_close,
    .read  = open_read,
    .write = open_write,
    .cmd   = open_cmd,
};

REGISTER_APP_EX("open", NULL, "1\0littlefs", &open_ops,
    "Route file open by extension to source app");

#endif
