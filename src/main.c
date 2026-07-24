#include "app.h"
#include "KSCOSsystem.h"

int main(void)
{
    sys_init();

    kscprintf("=== KSCOS start stutil2===\r\n");

    app_t* gpio = appget("gpio_port");
    if (gpio) {
        appopen(gpio);
        appcmd(gpio, "cfg -p 44 -m 3");
    }

    // app_t* term = appget("terminal");
    // if (term) appopen(term);

    // while (1) {
    //     uint8_t c;
    //     while (appread(ksc_console, &c, 1, 1) > 0)
    //         appwrite(term, &c, 1, 0);

    //     if (gpio) {
    //         appcmd(gpio, "tog -p 45");
    //     }
    //     sysdelay(500);
    // }
}
