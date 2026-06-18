#include "inc/master.h"

static dd_t* console = NULL;

static void* timer_tick(void* arg) { (void)arg;
    static int count = 0;
    ddioctl(console, "tick %d\n", count++);
    return NULL;
}

int main(void)
{
    bus_init();

    console = bus_getdriver("sys", 0, "console");

    dd_t* tmr = bus_getdriver("tim", 0, "clock");
    if (!tmr) {
        ddioctl(console, "timer driver not found\n");
        return -1;
    }
    ddioctl(console, "timer driver: %s\n", tmr->dd_ops->ops_name);

    tmr->user_data = (void*)(uintptr_t)500;
    tmr->callback = timer_tick;
    ddopen(tmr);
    Sleep(3000);
    ddclose(tmr);
    Sleep(500);
    ddioctl(console, "timer stopped\n");

    dd_t* sys = bus_getdriver("sys", 0, "systime");
    if (sys) {
        uint32_t t = 0;
        ddread(sys, &t, sizeof(t), 0);
        ddioctl(console, "systime: %u\n", t);
    }

    return 0;
}
