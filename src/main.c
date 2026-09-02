#include "app.h"
#include "KSCOSsystem.h"

int main(void)
{
    appget("system");
    appget("console");
    sys_init();

    kscprintf("=== KSCOS start ===\r\n");
    kscprintf("hello world\r\n");

    while (1) {
        kscterminal();
    }
}
