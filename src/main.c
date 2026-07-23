#include "app.h"
#include "KSCOSsystem.h"

int main(void)
{
    sys_init();

    kscprintf("=== KSCOS STM32 ready ===\r\n");

    app_t* term = appget("terminal");
    if (term) appopen(term);

    while (1) {
        uint8_t c;
        while (appread(ksc_console, &c, 1, 1) > 0)
            appwrite(term, &c, 1, 0);
    }
}
