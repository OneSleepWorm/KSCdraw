#include "inc/master.h"
#include "apps/app_config.h"

int main(void)
{
    setbuf(stdout, NULL);
    sys_init();

    kscprintf("term start\n");
    char buf[64];
    kscread(buf);
    ksccmd(buf);
    kscprintf("term end\n");

    #if 1/* echo back to terminal */
    while (1) {
        kscread(buf);
        ksccmd(buf);
        memset(buf, 0, sizeof(buf));
        Sleep(10);
    }
    #endif/* echo back to terminal */
}
