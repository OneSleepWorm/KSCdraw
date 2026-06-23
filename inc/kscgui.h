#ifndef __KSCGUI_H__
#define __KSCGUI_H__

#include "app.h"

#define GUI_INIT(g)          appioctl(g, "init")
#define GUI_CLEAR(g,c)       appioctl(g, "clear", (int)(c))
#define GUI_ORIENT(g,m)      appioctl(g, "orient", (int)(m))
#define GUI_PIXEL(g,x,y,c)   appioctl(g, "pixel", (int)(x),(int)(y),(int)(c))
#define GUI_FILL(g,x,y,w,h,c)   appioctl(g, "fill", (int)(x),(int)(y),(int)(w),(int)(h),(int)(c))
#define GUI_RECT(g,x,y,w,h,c)   appioctl(g, "rect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(c))
#define GUI_LINE(g,x0,y0,x1,y1,c) appioctl(g, "line", (int)(x0),(int)(y0),(int)(x1),(int)(y1),(int)(c))
#define GUI_CIRCLE(g,x,y,r,c)  appioctl(g, "circle", (int)(x),(int)(y),(int)(r),(int)(c))
#define GUI_FCIRCLE(g,x,y,r,c) appioctl(g, "fcircle", (int)(x),(int)(y),(int)(r),(int)(c))
#define GUI_ARC(g,x,y,r,d,c)   appioctl(g, "arc", (int)(x),(int)(y),(int)(r),(int)(d),(int)(c))
#define GUI_RRECT(g,x,y,w,h,r,c) appioctl(g, "rrect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(r),(int)(c))
#define GUI_FRRECT(g,x,y,w,h,r,c) appioctl(g, "frrect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(r),(int)(c))
#define GUI_CHAR(g,x,y,ch,fg,bg) appioctl(g, "char", (int)(x),(int)(y),(int)(ch),(int)(fg),(int)(bg))
#define GUI_STRING(g,x,y,s,fg,bg) appioctl(g, "string", (int)(x),(int)(y),(s),(int)(fg),(int)(bg))
#define GUI_IMAGE(g,x,y,w,h,d) appioctl(g, "image", (int)(x),(int)(y),(int)(w),(int)(h),(const uint16_t*)(d))
#define GUI_IBIG(g,x,y,w,h,s,d) appioctl(g, "ibig", (int)(x),(int)(y),(int)(w),(int)(h),(int)(s),(const uint16_t*)(d))
#define GUI_IBIN(g,x,y,w,h,d,fg,bg) appioctl(g, "ibin", (int)(x),(int)(y),(int)(w),(int)(h),(const uint8_t*)(d),(int)(fg),(int)(bg))

#endif
