#ifndef __APP_H__
#define __APP_H__

#include "dd.h"

struct app_t;

typedef struct papp_base_t {
    const char* app_name;
} papp_base_t;

typedef int (*papp_open_func)(struct app_t* app);
typedef int (*papp_close_func)(struct app_t* app);
typedef int (*papp_read_func)(struct app_t* app, void* data, uint32_t count, uint32_t mode);
typedef int (*papp_write_func)(struct app_t* app, void* data, uint32_t count, uint32_t mode);
typedef int (*papp_ioctl_func)(struct app_t* app, const char* fmt, va_list ap);

typedef struct papp_ops_t {
    papp_open_func  open;
    papp_close_func close;
    papp_read_func  read;
    papp_write_func write;
    papp_ioctl_func ioctl;
} papp_ops_t;

typedef struct __attribute__((aligned(16))) papp_t {
    const papp_base_t* base;
    const char*        dep_str;
    void*              reserved;
    const papp_ops_t*  ops;
} papp_t;

typedef struct app_t {
    const papp_t*    papp;
    dd_t*            dd0;
    dd_t*            dd1;
    dd_t*            dd2;
    dd_t*            dd3;
    const papp_ops_t* app_ops;
    void_func_t      callback;
    void*            app_data;
    void*            user_data;
} app_t;

#define _APP_CONCAT2(a, b) a##b
#define _APP_CONCAT(a, b) _APP_CONCAT2(a, b)
#define REGISTER_APP(name, dep, ops, desc) \
    static const papp_base_t _APP_CONCAT(_APP_BASE_, __LINE__) = {name}; \
    static const papp_t _APP_CONCAT(_APP_DEF_, __LINE__) \
    __attribute__((section("app_table"), used)) = { \
        &_APP_CONCAT(_APP_BASE_, __LINE__), dep, NULL, ops \
    }

extern const papp_t __start_papp_table[];
extern const papp_t __stop_papp_table[];

app_t* appget(const char* name);
void   appfree(app_t* app);
int    appopen(app_t* app);
int    appclose(app_t* app);
int    appread(app_t* app, void* data, uint32_t count, uint32_t mode);
int    appwrite(app_t* app, void* data, uint32_t count, uint32_t mode);
int    appioctl(app_t* app, const char* fmt, ...);

#endif
