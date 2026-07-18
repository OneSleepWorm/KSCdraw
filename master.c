#include "inc/master.h"

int main(void)
{
    sys_init();

    k_draw_device* dev = kscreenmount();
    KSC_window* screen = kscreeninit(dev, 0, 0, TFTx, TFTy, wwhite);

    kstring(dev, screen, "KSCOS on PC", 10, 10, rred, wwhite);

    while (1) {

    }

    return 0;
}
