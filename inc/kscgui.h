#ifndef __KSCGUI_H__
#define __KSCGUI_H__

#include "app.h"
#include "KSCdraw.h"   /* for ksc_obj_t in SETOBJS/DRAWOBJS/DRAWOBJ */

/* ================================================================
 * kscgui ioctl command macros
 *
 * Usage:
 *   app_t* gui = appget("KSCGUI");
 *   appopen(gui);
 *   GUI_SETSPI(gui, 2);          -- optional, default is SPI2
 *   GUI_INIT(gui);
 *   GUI_CLEAR(gui, 0x0000);
 *   ...
 * ================================================================ */

/* --- lifecycle --- */
#define GUI_INIT(g)              appioctl(g, "init")
#define GUI_SETSPI(g,n)          appioctl(g, "setspi", (int)(n))
#define GUI_ORIENT(g,m)          appioctl(g, "orient", (int)(m))

/* --- window management --- */
#define GUI_WCREATE(g,x,y,w,h,b) appioctl(g, "wcreate", (int)(x),(int)(y),(int)(w),(int)(h),(int)(b))
#define GUI_WDELETE(g,i)         appioctl(g, "wdelete", (int)(i))
#define GUI_WSELECT(g,i)         appioctl(g, "wselect", (int)(i))
#define GUI_WCLEAR(g)            appioctl(g, "wclear")

/* --- drawing primitives (active window) --- */
#define GUI_CLEAR(g,c)           appioctl(g, "clear", (int)(c))
#define GUI_PIXEL(g,x,y,c)       appioctl(g, "pixel", (int)(x),(int)(y),(int)(c))
#define GUI_FILL(g,x,y,w,h,c)    appioctl(g, "fill", (int)(x),(int)(y),(int)(w),(int)(h),(int)(c))
#define GUI_RECT(g,x,y,w,h,c)    appioctl(g, "rect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(c))
#define GUI_FRECT(g,x,y,w,h,c)   appioctl(g, "frect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(c))
#define GUI_LINE(g,x0,y0,x1,y1,c)  appioctl(g, "line", (int)(x0),(int)(y0),(int)(x1),(int)(y1),(int)(c))
#define GUI_CIRCLE(g,x,y,r,c)    appioctl(g, "circle", (int)(x),(int)(y),(int)(r),(int)(c))
#define GUI_FCIRCLE(g,x,y,r,c)   appioctl(g, "fcircle", (int)(x),(int)(y),(int)(r),(int)(c))
#define GUI_ARC(g,x,y,r,d,c)     appioctl(g, "arc", (int)(x),(int)(y),(int)(r),(int)(d),(int)(c))
#define GUI_RRECT(g,x,y,w,h,r,c) appioctl(g, "rrect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(r),(int)(c))
#define GUI_FRRECT(g,x,y,w,h,r,c) appioctl(g, "frrect", (int)(x),(int)(y),(int)(w),(int)(h),(int)(r),(int)(c))
#define GUI_CHAR(g,x,y,ch,fg,bg) appioctl(g, "char", (int)(x),(int)(y),(int)(ch),(int)(fg),(int)(bg))
#define GUI_STRING(g,x,y,s,fg,bg) appioctl(g, "string", (int)(x),(int)(y),(s),(int)(fg),(int)(bg))
#if __USE_CHINESE__
#define GUI_STRCN(g,x,y,s,fg,bg) appioctl(g, "strcn", (int)(x),(int)(y),(s),(int)(fg),(int)(bg))
#endif

/* --- compat aliases --- */
#define GUI_FILLBOX(g,x,y,w,h,c) GUI_FILL(g,x,y,w,h,c)

/* --- image (direct SPI fast-path) --- */
#define GUI_IMAGE(g,x,y,w,h,d)   appioctl(g, "image", (int)(x),(int)(y),(int)(w),(int)(h),(const uint8_t*)(d))
#define GUI_IBIG(g,x,y,w,h,s,d)  appioctl(g, "ibig", (int)(x),(int)(y),(int)(w),(int)(h),(int)(s),(const uint8_t*)(d))
#define GUI_IBIN(g,x,y,w,h,d,fg,bg) appioctl(g, "ibin", (int)(x),(int)(y),(int)(w),(int)(h),(const uint8_t*)(d),(int)(fg),(int)(bg))

/* --- object system (user-owned ksc_obj_t array) --- */
#define GUI_SETOBJS(g,n,o)       appioctl(g, "setobjs", (int)(n),(const ksc_obj_t*)(o))
#define GUI_DRAWOBJS(g,n)        appioctl(g, "drawobjs", (int)(n))
#define GUI_DRAWOBJ(g,i)         appioctl(g, "drawobj", (int)(i))

#endif
