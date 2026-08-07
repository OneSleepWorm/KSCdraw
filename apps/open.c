#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdio.h>

#if __USE_STM32__ || __USE_PC__

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
        if (gui) {
            appopen(gui);
            r = appcmd(gui, "drawbmp");
        } else
            r = -1;
    } else {
        if (app->output_fn) {
            uint8_t buf[64];
            int n;
            do {
                lfs->output_data = buf;
                char ns[12];
                snprintf(ns, sizeof(ns), "%zu", sizeof(buf));
                const char* ra[26] = {0};
                ra['n' - 'a'] = ns;
                n = appcmd_argv(lfs, "fread", ra);
                lfs->output_data = NULL;
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

    return -1;
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

    lfs->output_data = data;
    char ns[12];
    snprintf(ns, sizeof(ns), "%lu", (unsigned long)count);
    const char* ra[26] = {0};
    ra['n' - 'a'] = ns;
    int r = appcmd_argv(lfs, "fread", ra);
    lfs->output_data = NULL;
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
