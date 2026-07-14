#ifndef __APP_H__
#define __APP_H__

#include <stdint.h>
#include <stdarg.h>

typedef void* (*void_func_t)(void*);

struct app_t;

typedef struct papp_base_t {
    const char* app_name;
} papp_base_t;

typedef int (*papp_open_func)(struct app_t* app);
typedef int (*papp_close_func)(struct app_t* app);
typedef int (*papp_read_func)(struct app_t* app, void* data, uint32_t count, uint32_t mode);
typedef int (*papp_write_func)(struct app_t* app, void* data, uint32_t count, uint32_t mode);
typedef int (*papp_ioctl_func)(struct app_t* app, const char* fmt, va_list ap);
typedef int (*papp_cmd_func)(struct app_t* app, const char* cmdname, const char** argv);

#define APPCMD_ARG(c)   ((c) - 'a')
#define APPCMD_HAS(argv, c)  ((argv)[APPCMD_ARG(c)] != NULL)

typedef struct papp_ops_t {
    papp_open_func  open;
    papp_close_func close;
    papp_read_func  read;
    papp_write_func write;
    papp_ioctl_func ioctl;
    papp_cmd_func   cmd;
} papp_ops_t;

typedef struct __attribute__((aligned(16))) papp_t {
    const papp_base_t* base;
    const char*        dep_str;        /* 驱动依赖 "N\0name1\0name2\0..." */
    const char*        app_dep_str;    /* 应用依赖 "N\0name1\0name2\0..." (NULL=无) */
    const papp_ops_t*  ops;
} papp_t;

typedef struct app_t {
    const papp_t*       papp;
    struct app_t*       app0;             /* 应用依赖 (appget 递归填充) */
    struct app_t*       app1;
    struct app_t*       app2;
    struct app_t*       app3;
    const papp_ops_t*   app_ops;
    void_func_t         callback;
    void*               app_data;
    void*               user_data;
    void*               callback_data;
    void*               mode_data;
} app_t;

#define _APP_CONCAT2(a, b) a##b
#define _APP_CONCAT(a, b) _APP_CONCAT2(a, b)
#define REGISTER_APP(name, dep, ops, desc) \
    REGISTER_APP_EX(name, dep, NULL, ops, desc)
#define REGISTER_APP_EX(name, dep, app_dep, ops, desc) \
    static const papp_base_t _APP_CONCAT(_APP_BASE_, __LINE__) = {name}; \
    static const papp_t _APP_CONCAT(_APP_DEF_, __LINE__) \
    __attribute__((section("app_table"), used)) = { \
        &_APP_CONCAT(_APP_BASE_, __LINE__), dep, app_dep, ops \
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
int    appcmd(app_t* app, const char* cmdline);

#endif
