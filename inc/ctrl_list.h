#ifndef CTRL_LIST_H
#define CTRL_LIST_H

#include <stdint.h>

#define CTRL_EVENT_CONFIRM  0
#define CTRL_EVENT_QUIT     1

typedef void (*ctrl_event_cb_t)(void* user_data, int event);

typedef struct {
    uint8_t up;
    uint8_t down;
    uint8_t ok;
    uint8_t quit;
} ctrl_keymap_t;

#endif
