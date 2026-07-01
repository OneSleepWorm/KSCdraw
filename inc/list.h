#ifndef __LIST_H__
#define __LIST_H__

#include "KSCdraw.h"

typedef struct {
    uint8_t  x, y, w, h, item_h;
} list_pos_t;

typedef struct {
    KSCCOLOR sel_bg, bg, fg, sel_fg;
} list_colors_t;

#define LIST_STYLE_NONE      0
#define LIST_STYLE_FILLROW   1
#define LIST_STYLE_FILLBAR   2
#define LIST_STYLE_TEXTONLY  3
#define LIST_STYLE_ARROW     4

#endif
