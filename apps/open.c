#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdio.h>

#if __USE_STM32__ || __USE_PC__

/* ── route table (old style, backward compat) ── */

typedef struct {
    const char* ext;
    const char* src_app;
    const char* subcmd;
    const char* default_target;
} open_route_t;

static const open_route_t routes[] = {
    {".txt", "littlefs", "feed", "uart"},
    {".bmp", "littlefs", "feed", "gui"},
    {NULL,   NULL,       NULL,   NULL}
};

/* ── "file" subcommand: open file and let GUI/app pull via appread ── */

static int cmd_open_file(app_t* app, const char* cmdname, const char** argv)
{
    (void)cmdname;
    const char* path = argv[APPCMD_ARG('p')];
    if (!path) return -1;

    app_t* lfs = appget("littlefs");
    if (!lfs) return -1;

    const char* oa[26] = {0};
    oa['p' - 'a'] = path;
    if (appcmd_argv(lfs, "open", oa) < 0)
        return -1;

    const char* ext = strrchr(path, '.');
    if (!ext) ext = "";

    int r = 0;

    if (strcmp(ext, ".bmp") == 0) {
        app_t* gui = appget("KSCGUI");
        if (gui)
            r = appcmd(gui, "drawbmp");
        else
            r = -1;
    } else {
        if (app->output_fn) {
            uint8_t buf[64];
            int n;
            do {
                lfs->callback_data = buf;
                char ns[12];
                snprintf(ns, sizeof(ns), "%zu", sizeof(buf));
                const char* ra[26] = {0};
                ra['n' - 'a'] = ns;
                n = appcmd_argv(lfs, "fread", ra);
                lfs->callback_data = NULL;
                if (n > 0)
                    app->output_fn(buf, n, app->output_ctx);
            } while (n > 0);
        }
    }

    appcmd_argv(lfs, "close", NULL);
    return r;
}

/* ── appcmd dispatch ── */

static int open_cmd(app_t* app, const char* cmdname, const char** argv)
{
    if (strcmp(cmdname, "file") == 0)
        return cmd_open_file(app, cmdname, argv);

    /* --- old route table (backward compat) --- */

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

    const char* fwd_argv[28];
    for (int i = 0; i < 26; i++)
        fwd_argv[i] = argv[i];
    fwd_argv[26] = NULL;
    fwd_argv[27] = NULL;
    fwd_argv[APPCMD_ARG('g')] = NULL;
    fwd_argv[APPCMD_ARG('u')] = NULL;
    fwd_argv[APPCMD_ARG('t')] = tgt;

    app_output_fn saved_ofn = src->output_fn;
    void* saved_octx = src->output_ctx;
    src->output_fn = app->output_fn;
    src->output_ctx = app->output_ctx;

    int r = appcmd_argv(src, subcmd, fwd_argv);

    src->output_fn = saved_ofn;
    src->output_ctx = saved_octx;
    return r;
}

/* ── app lifecycle ── */

static int open_open(app_t* app) { (void)app; return 0; }
static int open_close(app_t* app) { (void)app; return 0; }

static int open_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    (void)app;
    (void)mode;
    app_t* lfs = appget("littlefs");
    if (!lfs) return 0;

    lfs->callback_data = data;
    char ns[12];
    snprintf(ns, sizeof(ns), "%u", count);
    const char* ra[26] = {0};
    ra['n' - 'a'] = ns;
    int r = appcmd_argv(lfs, "fread", ra);
    lfs->callback_data = NULL;
    return r;
}

static int open_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{ (void)app; (void)data; (void)count; (void)mode; return 0; }

static const papp_ops_t open_ops = {
    .open  = open_open,
    .close = open_close,
    .read  = open_read,
    .write = open_write,
    .cmd   = open_cmd,
};

REGISTER_APP_EX("open", NULL, "2\0littlefs\0KSCGUI", &open_ops,
    "Route file open by extension to source app; 'file' subcmd for pull model");

#endif
