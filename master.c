#include "inc/master.h"
#include "apps/app_config.h"

#if 0 /* open app test — verified 2026-07-18 */
static int open_output(const void* data, uint32_t len, void* ctx)
{
    (void)ctx;
    return appwrite(ksc_console, (void*)data, len, 0x11);
}

static int test_open(void)
{
    kscprintf("--- open test ---\n");

    app_t* lfs = appget("littlefs");
    if (!lfs) { kscprintf("FAIL: appget littlefs\n"); return -1; }
    appopen(lfs);

    appcmd(lfs, "format");
    appcmd(lfs, "mount");

    int wr = appcmd(lfs, "writenew -p /test.txt -d \"Hello from open test\"");
    kscprintf("writenew result: %d\n", wr);

    lfs->output_fn = open_output;
    lfs->output_ctx = NULL;
    appcmd(lfs, "ls /");

    app_t* open_app = appget("open");
    if (!open_app) { kscprintf("FAIL: appget open\n"); return -1; }
    appopen(open_app);
    open_app->output_fn = open_output;
    open_app->output_ctx = NULL;
    int r = appcmd(open_app, "open -p /test.txt");
    kscprintf("open result: %d\n", r);

    appclose(open_app);
    appclose(lfs);
    kscprintf("--- open test done ---\n");
    return 0;
}
#endif

static int running = 1;

static void on_ctrl(void* user_data, int event)
{
    (void)user_data;
    if (event == CTRL_EVENT_QUIT) running = 0;
}

int main(void)
{
    sys_init();

#if 0
    test_open();
#endif

    kscprintf("=== KSCOS PC: list keyboard demo ===\n");
    kscprintf("E=up  C=down  S=left  F=right  D=OK  1=quit\n");

    app_t* list = appget("list");
    if (!list) { kscprintf("FAIL: appget list\n"); return 1; }
    appopen(list);

    for (int i = 1; i <= 16; i++)
        appcmd(list, "add -d \"Item X\"");

    appcmd(list, "setpos -x 0 -y 10 -w 200 -h 160 -t 20");
    appcmd(list, "setcolors -a 001F -b 0000 -c FFFF -d F800");
    appcmd(list, "setstyle -s 1");

    list->mode_data = (void*)on_ctrl;
    list->callback_data = NULL;
    appcmd(list, "init -k");

    while (running) sysdelay(100);

    appclose(list);
    kscprintf("demo closed\n");
    return 0;
}
