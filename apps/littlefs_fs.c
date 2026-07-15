/**
 * @file    littlefs_fs.c
 * @note    littlefs 文件系统集成 (W25Q64 SPI NOR Flash)
 * @flash   ~2918B (Debug, -Og)
 *
 * ============================================================
 * 基本信息
 * ============================================================
 * 注册名:  littlefs
 * dep:     NULL
 * app_dep: "1\0w25qxx_base"
 * 平台:    STM32 (__USE_STM32__)
 *
 * ============================================================
 * 资源占用
 * ============================================================
 * 差值法: 从 target_sources 中临时移除 littlefs_fs.c + lfs.c + lfs_util.c 测得:
 *   ROM(Debug -O0, RW):       21,308 B (full 52,504 − baseline 31,196)
 *   ROM(Debug -O0, RONLY):     9,500 B (full 40,696 − baseline 31,196)
 *   ROM(Release -Os, RW):     19,692 B (full 38,016 − baseline 18,324)
 *   ROM(Release -Os, RONLY):   8,572 B (full 26,896 − baseline 18,324)
 *   RAM(静态): 16 B (.bss 增量, 两配置一致)
 *   RAM(堆):   ~1,440 B (lfs_ctx_t ~160 + read_buf[512] + prog_buf[512] + lookahead_buf[256])
 *
 * ============================================================
 * 外部接口
 * ============================================================
 *
 * appget("littlefs") → app_t*
 *   前置条件: w25qxx_base 已注册.
 *
 * appopen(fs)
 *   打开 w25qxx_base, 分配上下文及缓冲区.
 *
 * appclose(fs)
 *   卸载 FS, 关闭 w25qxx_base, 释放全部内存.
 *
 * appwrite(fs, data, count, mode)
 *   0  — no-op (通用约定)
 *   1  — 格式化. data=NULL, 调用 lfs_format. (RONLY 不可用)
 *   2  — 挂载.   data=NULL, 调用 lfs_mount.
 *   3  — 卸载.   data=NULL, 调用 lfs_unmount.
 *   4  — 文件打开. data=lfs_file_op_t*, 返回 lfs_file_open 结果. (RONLY 也可用)
 *   5  — 文件关闭. data=lfs_file_t*, 返回 lfs_file_close 结果. (RONLY 也可用)
 *   6  — 文件写入. data=lfs_rw_t*, 返回 lfs_file_write 结果. (RONLY 不可用)
 *   7  — 文件定位. data=lfs_seek_t*, 返回 lfs_file_seek 结果. (RONLY 不可用)
 *   8  — 删除文件. data=path(char*), 返回 lfs_remove 结果. (RONLY 不可用)
 *   9  — 重命名.   data=lfs_rename_t*, 返回 lfs_rename 结果. (RONLY 不可用)
 *   10 — 创建目录. data=path(char*), 返回 lfs_mkdir 结果. (RONLY 不可用)
 *   11 — 目录打开. data=lfs_dir_op_t*, 返回 lfs_dir_open 结果. (RONLY 不可用)
 *   12 — 目录关闭. data=lfs_dir_t*, 返回 lfs_dir_close 结果. (RONLY 不可用)
 *
 * appread(fs, data, count, mode)
 *   0  — no-op (通用约定)
 *   1  — 文件读取. data=lfs_rw_t*, 返回 lfs_file_read 结果.
 *   2  — 文件位置. data=lfs_file_t*, 返回 lfs_file_tell 结果.
 *   3  — 文件大小. data=lfs_file_t*, 返回 lfs_file_size 结果.
 *   4  — 文件状态. data=lfs_stat_t*, 返回 lfs_stat 结果.
 *   5  — 目录读取. data=lfs_dir_read_t*, 返回 lfs_dir_read 结果.
 *   6  — FS 信息.  data=lfs_fsinfo_t*, 返回 lfs_fs_stat 结果.
 *
 * 典型使用:
 *
 *   app_t* fs = appget("littlefs");
 *   appopen(fs);
 *   appwrite(fs, NULL, 0, 1);        // format (RONLY 下返回 -1, 忽略)
 *   appwrite(fs, NULL, 0, 2);        // mount
 *
 *   lfs_file_t f = {0};
 *   lfs_file_op_t op = {&f, "test.txt", LFS_O_WRONLY | LFS_O_CREAT};
 *   appwrite(fs, &op, 0, 4);         // open (RONLY 下返回 -1)
 *   lfs_rw_t rw = {&f, "Hello", 5};
 *   appwrite(fs, &rw, 0, 6);         // write (RONLY 下返回 -1)
 *   appwrite(fs, &f, 0, 5);          // close
 *
 *   op.flags = LFS_O_RDONLY;
 *   appwrite(fs, &op, 0, 4);         // open (RONLY 也可用)
 *   char buf[64];
 *   rw.buffer = buf; rw.size = 64;
 *   int n = appread(fs, &rw, 0, 1);  // read
 *   appwrite(fs, &f, 0, 5);          // close (RONLY 也可用)
 *
 *   appwrite(fs, NULL, 0, 3);        // unmount
 *   appclose(fs);
 *   appfree(fs);
 *
 * ============================================================
 * 注意事项
 * ============================================================
 * 1. 所有缓冲区由 osmalloc 动态分配, 无全局/静态占用.
 * 2. 必须先 format 再 mount, 或 mount 已 format 的 FS.
 * 3. lfs_file_t 等 littlefs 对象生命周期由用户保证. RONLY 下将 file 初始化为 {0} 避免断言.
 * 4. W25Q64 block_size=4096, block_count=2048.
 * 5. RONLY 构建时 lfs.c 的 LFS_ASSERT 会通过 assert() → abort() 导致裸机死循环.
 *    原因是 lfs.c 的包含链为 lfs.c→lfs.h→lfs_util.h, 从未包含 lfs_config.h,
 *    默认 LFS_ASSERT=assert() 始终生效. 解决方案:
 *    CMakeLists.txt 中加 target_compile_definitions(... PRIVATE LFS_NO_ASSERT).
 *    不要用 LFS_CONFIG=lfs_config.h — 该文件是完整 lfs_util.h 替代品, 缺少
 *    stdbool.h/lfs_min/lfs_crc 等, 会导致编译失败.
 * 6. mode 4/5 (file_open/close) 始终可用 (包括 RONLY). mode 1/3/6-12 仅在
 *    #ifndef LFS_READONLY 中编译, RONLY 下返回 -1.
 */

#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include "lfs.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>
#if __USE_STM32__

typedef struct {
    app_t* flash;
    lfs_t  lfs;
    struct lfs_config cfg;
    uint8_t* read_buf;
    uint8_t* prog_buf;
    uint8_t* lookahead_buf;
    int mounted;
} lfs_ctx_t;

static int lfs_bd_read(const struct lfs_config *c, lfs_block_t block,
                       lfs_off_t off, void *buffer, lfs_size_t size)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)c->context;
    uint32_t addr = block * 4096 + off;
    appwrite(ctx->flash, &addr, 4, 1);
    int ret = appread(ctx->flash, buffer, size, 1);
    return ret < 0 ? LFS_ERR_IO : 0;
}

#ifndef LFS_READONLY
static int lfs_bd_prog(const struct lfs_config *c, lfs_block_t block,
                       lfs_off_t off, const void *buffer, lfs_size_t size)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)c->context;
    uint32_t addr = block * 4096 + off;
    appwrite(ctx->flash, &addr, 4, 1);
    int ret = appwrite(ctx->flash, (void*)buffer, size, 3);
    return ret < 0 ? LFS_ERR_IO : 0;
}

static int lfs_bd_erase(const struct lfs_config *c, lfs_block_t block)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)c->context;
    uint32_t addr = block * 4096;
    appwrite(ctx->flash, &addr, 4, 1);
    int ret = appwrite(ctx->flash, NULL, 0, 5);
    return ret < 0 ? LFS_ERR_IO : 0;
}

static int lfs_bd_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;
}
#endif

static int lfs_app_open(app_t* app)
{
    lfs_ctx_t* ctx = osmalloc(sizeof(lfs_ctx_t));
    if (!ctx) return -1;
    ctx->flash = app->app0;
    ctx->mounted = 0;
    ctx->read_buf = NULL;
    ctx->prog_buf = NULL;
    ctx->lookahead_buf = NULL;

    int ret = appopen(ctx->flash);
    if (ret) { osfree(ctx); return ret; }

    ctx->read_buf = osmalloc(512);
    ctx->prog_buf = osmalloc(512);
    ctx->lookahead_buf = osmalloc(256);
    if (!ctx->read_buf || !ctx->prog_buf || !ctx->lookahead_buf) {
        if (ctx->read_buf) osfree(ctx->read_buf);
        if (ctx->prog_buf) osfree(ctx->prog_buf);
        if (ctx->lookahead_buf) osfree(ctx->lookahead_buf);
        osfree(ctx);
        return -1;
    }

    ctx->cfg = (struct lfs_config){
        .context        = ctx,
        .read           = lfs_bd_read,
#ifndef LFS_READONLY
        .prog           = lfs_bd_prog,
        .erase          = lfs_bd_erase,
        .sync           = lfs_bd_sync,
#endif
        .read_size      = 256,
        .prog_size      = 256,
        .block_size     = 4096,
        .block_count    = 2048,
        .block_cycles   = 500,
        .cache_size     = 512,
        .lookahead_size = 256,
        .read_buffer    = ctx->read_buf,
        .prog_buffer    = ctx->prog_buf,
        .lookahead_buffer = ctx->lookahead_buf,
    };

    app->app_data = ctx;
    return 0;
}

static int lfs_app_close(app_t* app)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx) return 0;

    if (ctx->mounted) {
        lfs_unmount(&ctx->lfs);
        ctx->mounted = 0;
    }

    if (ctx->read_buf) osfree(ctx->read_buf);
    if (ctx->prog_buf) osfree(ctx->prog_buf);
    if (ctx->lookahead_buf) osfree(ctx->lookahead_buf);
    if (ctx->flash) appclose(ctx->flash);

    osfree(ctx);
    app->app_data = NULL;
    return 0;
}

/* file/io operation descriptors (formerly in littlefs_fs.h) */
typedef struct {
    lfs_file_t* file;
    const char* path;
    int         flags;
} lfs_file_op_t;

typedef struct {
    lfs_file_t* file;
    void*       buffer;
    lfs_size_t  size;
} lfs_rw_t;

typedef struct {
    lfs_file_t* file;
    lfs_soff_t  offset;
    int         whence;
} lfs_seek_t;

typedef struct {
    const char*      oldpath;
    const char*      newpath;
} lfs_rename_t;

typedef struct {
    const char*       path;
    struct lfs_info* info;
} lfs_stat_t;

typedef struct {
    lfs_dir_t*  dir;
    const char* path;
} lfs_dir_op_t;

typedef struct {
    lfs_dir_t*      dir;
    struct lfs_info* info;
} lfs_dir_read_t;

static int lfs_app_write(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx) return -1;
    int ret;

    switch (mode) {
    case 0:
        return 0;
#ifndef LFS_READONLY
    case 1:
        if (ctx->mounted) return -1;
        ret = lfs_format(&ctx->lfs, &ctx->cfg);
        return ret;
#endif
    case 2:
        if (ctx->mounted) return -1;
        ret = lfs_mount(&ctx->lfs, &ctx->cfg);
        if (ret == 0) ctx->mounted = 1;
        return ret;
    case 3:
        if (!ctx->mounted) return -1;
        ret = lfs_unmount(&ctx->lfs);
        if (ret == 0) ctx->mounted = 0;
        return ret;
    case 4:
        if (!ctx->mounted || !data) return -1;
    {
        lfs_file_op_t* op = (lfs_file_op_t*)data;
        ret = lfs_file_open(&ctx->lfs, op->file, op->path, op->flags);
    }
        return ret;
    case 5:
        if (!ctx->mounted || !data) return -1;
        ret = lfs_file_close(&ctx->lfs, (lfs_file_t*)data);
        return ret;
#ifndef LFS_READONLY
    case 6:
        if (!ctx->mounted || !data) return -1;
    {
        lfs_rw_t* rw = (lfs_rw_t*)data;
        ret = (int)lfs_file_write(&ctx->lfs, rw->file, rw->buffer, rw->size);
    }
        return ret;
    case 7:
        if (!ctx->mounted || !data) return -1;
    {
        lfs_seek_t* sk = (lfs_seek_t*)data;
        ret = (int)lfs_file_seek(&ctx->lfs, sk->file, sk->offset, sk->whence);
    }
        return ret;
    case 8:
        if (!ctx->mounted || !data) return -1;
        ret = lfs_remove(&ctx->lfs, (const char*)data);
        return ret;
    case 9:
        if (!ctx->mounted || !data) return -1;
    {
        lfs_rename_t* rn = (lfs_rename_t*)data;
        ret = lfs_rename(&ctx->lfs, rn->oldpath, rn->newpath);
    }
        return ret;
    case 10:
        if (!ctx->mounted || !data) return -1;
        ret = lfs_mkdir(&ctx->lfs, (const char*)data);
        return ret;
    case 11:
        if (!ctx->mounted || !data) return -1;
    {
        lfs_dir_op_t* dop = (lfs_dir_op_t*)data;
        ret = lfs_dir_open(&ctx->lfs, dop->dir, dop->path);
    }
        return ret;
    case 12:
        if (!ctx->mounted || !data) return -1;
        ret = lfs_dir_close(&ctx->lfs, (lfs_dir_t*)data);
        return ret;
#endif
    default:
        return -1;
    }
}

static int lfs_app_read(app_t* app, void* data, uint32_t count, uint32_t mode)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;

    switch (mode) {
    case 0:
        return 0;
    case 1:
        if (!data) return -1;
    {
        lfs_rw_t* rw = (lfs_rw_t*)data;
        return (int)lfs_file_read(&ctx->lfs, rw->file, rw->buffer, rw->size);
    }
    case 2:
        if (!data) return -1;
        return (int)lfs_file_tell(&ctx->lfs, (lfs_file_t*)data);
    case 3:
        if (!data) return -1;
        return (int)lfs_file_size(&ctx->lfs, (lfs_file_t*)data);
    case 4:
        if (!data) return -1;
    {
        lfs_stat_t* st = (lfs_stat_t*)data;
        return lfs_stat(&ctx->lfs, st->path, st->info);
    }
    case 5:
        if (!data) return -1;
    {
        lfs_dir_read_t* dr = (lfs_dir_read_t*)data;
        return lfs_dir_read(&ctx->lfs, dr->dir, dr->info);
    }
    case 6:
        if (!data) return -1;
        return lfs_fs_stat(&ctx->lfs, (struct lfs_fsinfo*)data);
    default:
        return -1;
    }
}

/* ================================================================
 * appcmd 接口
 * ================================================================
 * 所有输出写到 app->user_data (至少 256 B 缓冲)。
 * 字符串数据通过 -d <text>，二进制通过 user_data + -n <len>。
 * 持久文件 handle: open 返回 handle(int), close/fread/fwrite/fseek 用 -h。
 */

typedef struct { const char* name; int (*handler)(app_t*, const char**); } lfs_cmd_entry_t;

/* --- FS 管理 --- */

static int cmd_format(app_t* app, const char** argv)
{
    (void)argv;
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || ctx->mounted) return -1;
    return lfs_format(&ctx->lfs, &ctx->cfg);
}

static int cmd_mount(app_t* app, const char** argv)
{
    (void)argv;
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || ctx->mounted) return -1;
    int ret = lfs_mount(&ctx->lfs, &ctx->cfg);
    if (ret == 0) ctx->mounted = 1;
    return ret;
}

static int cmd_unmount(app_t* app, const char** argv)
{
    (void)argv;
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    int ret = lfs_unmount(&ctx->lfs);
    if (ret == 0) ctx->mounted = 0;
    return ret;
}

static int cmd_info(app_t* app, const char** argv)
{
    (void)argv;
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    struct lfs_fsinfo fi;
    int ret = lfs_fs_stat(&ctx->lfs, &fi);
    if (ret < 0 || !app->user_data) return ret;
    snprintf((char*)app->user_data, 256, "blk_size=%u blk_count=%u\r\n",
             (unsigned)fi.block_size, (unsigned)fi.block_count);
    return ret;
}

/* --- 目录 / 文件一次性操作 --- */

static int cmd_ls(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted || !app->user_data) return -1;
    const char* path = APPCMD_HAS(argv, 'p') ? argv[APPCMD_ARG('p')] : "/";
    uint16_t max = APPCMD_HAS(argv, 'n') ? (uint16_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0) : 256;

    lfs_dir_t* dir = (lfs_dir_t*)osmalloc(sizeof(lfs_dir_t));
    if (!dir) return -1;
    int ret = lfs_dir_open(&ctx->lfs, dir, path);
    if (ret < 0) { osfree(dir); return ret; }

    struct lfs_info* info = (struct lfs_info*)osmalloc(sizeof(struct lfs_info));
    if (!info) { lfs_dir_close(&ctx->lfs, dir); osfree(dir); return -1; }

    char* out = (char*)app->user_data;
    int pos = 0;
    while (lfs_dir_read(&ctx->lfs, dir, info) > 0) {
        int n = snprintf(out + pos, max > (uint16_t)pos ? max - (uint16_t)pos : 0,
                         "%c %s %u\r\n",
                         info->type == LFS_TYPE_DIR ? 'D' : 'F',
                         info->name, (unsigned)info->size);
        if (pos + n >= (int)max) break;
        pos += n;
    }
    lfs_dir_close(&ctx->lfs, dir);
    osfree(info);
    osfree(dir);
    return pos;
}

static int cmd_cat(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p') || !app->user_data) return -1;
    uint16_t max = APPCMD_HAS(argv, 'n') ? (uint16_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0) : 256;
    if (max == 0) return -1;

    lfs_file_t* f = (lfs_file_t*)osmalloc(sizeof(lfs_file_t));
    if (!f) return -1;
    int ret = lfs_file_open(&ctx->lfs, f, argv[APPCMD_ARG('p')], LFS_O_RDONLY);
    if (ret < 0) { osfree(f); return ret; }

    lfs_size_t rd = lfs_file_read(&ctx->lfs, f, app->user_data, max);
    lfs_file_close(&ctx->lfs, f);
    osfree(f);
    return (int)rd;
}

static int do_write_append(app_t* app, const char** argv, int append_mode)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p')) return -1;

    const uint8_t* data;
    lfs_size_t len;
    if (APPCMD_HAS(argv, 'd')) {
        data = (const uint8_t*)argv[APPCMD_ARG('d')];
        len = (lfs_size_t)strlen((const char*)data);
    } else if (app->user_data && APPCMD_HAS(argv, 'n')) {
        data = (const uint8_t*)app->user_data;
        len = (lfs_size_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0);
    } else return -1;
    if (len == 0) return -1;

    int flags = LFS_O_WRONLY | LFS_O_CREAT;
    if (!append_mode) flags |= LFS_O_TRUNC;

    lfs_file_t* f = (lfs_file_t*)osmalloc(sizeof(lfs_file_t));
    if (!f) return -1;
    int ret = lfs_file_open(&ctx->lfs, f, argv[APPCMD_ARG('p')], flags);
    if (ret < 0) { osfree(f); return ret; }

    if (append_mode) lfs_file_seek(&ctx->lfs, f, 0, LFS_SEEK_END);
    lfs_size_t written = lfs_file_write(&ctx->lfs, f, data, len);
    lfs_file_close(&ctx->lfs, f);
    osfree(f);
    return (int)written;
}

static int cmd_writenew(app_t* app, const char** argv)
{
    return do_write_append(app, argv, 0);
}

static int cmd_append(app_t* app, const char** argv)
{
    return do_write_append(app, argv, 1);
}

static int cmd_rm(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p')) return -1;
    return lfs_remove(&ctx->lfs, argv[APPCMD_ARG('p')]);
}

static int cmd_mkdir(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p')) return -1;
    return lfs_mkdir(&ctx->lfs, argv[APPCMD_ARG('p')]);
}

static int cmd_mv(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 's') || !APPCMD_HAS(argv, 'd')) return -1;
    return lfs_rename(&ctx->lfs, argv[APPCMD_ARG('s')], argv[APPCMD_ARG('d')]);
}

static int cmd_stat(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p')) return -1;

    struct lfs_info* info = (struct lfs_info*)osmalloc(sizeof(struct lfs_info));
    if (!info) return -1;
    int ret = lfs_stat(&ctx->lfs, argv[APPCMD_ARG('p')], info);
    if (ret < 0) { osfree(info); return ret; }

    if (app->user_data)
        snprintf((char*)app->user_data, 256, "type=%c size=%u name=%s\r\n",
                 info->type == LFS_TYPE_DIR ? 'D' : 'F',
                 (unsigned)info->size, info->name);
    osfree(info);
    return ret;
}

/* --- 持久文件 handle (callback_data/mode_data) --- */

static lfs_file_t* resolve_handle(app_t* app, const char** argv)
{
    if (APPCMD_HAS(argv, 'h'))
        return (lfs_file_t*)(uintptr_t)strtoul(argv[APPCMD_ARG('h')], NULL, 0);
    return (lfs_file_t*)app->mode_data;
}

static int cmd_open(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p')) return -1;
    int flags = APPCMD_HAS(argv, 'f') ? (int)strtoul(argv[APPCMD_ARG('f')], NULL, 0) : LFS_O_RDONLY;

    lfs_file_t* f = (lfs_file_t*)osmalloc(sizeof(lfs_file_t));
    if (!f) return -1;
    int ret = lfs_file_open(&ctx->lfs, f, argv[APPCMD_ARG('p')], flags);
    if (ret < 0) { osfree(f); return ret; }

    app->callback_data = f;
    return 0;
}

static int cmd_close(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    lfs_file_t* f = resolve_handle(app, argv);
    if (!f) return -1;
    int ret = lfs_file_close(&ctx->lfs, f);
    osfree(f);
    return ret;
}

static int cmd_fread(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted || !app->user_data) return -1;
    if (!APPCMD_HAS(argv, 'n')) return -1;
    lfs_file_t* f = resolve_handle(app, argv);
    if (!f) return -1;
    lfs_size_t n = (lfs_size_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0);
    if (n == 0) return -1;
    return (int)lfs_file_read(&ctx->lfs, f, app->user_data, n);
}

static int cmd_fwrite(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    lfs_file_t* f = resolve_handle(app, argv);
    if (!f) return -1;

    const uint8_t* data;
    lfs_size_t len;
    if (APPCMD_HAS(argv, 'd')) {
        data = (const uint8_t*)argv[APPCMD_ARG('d')];
        len = (lfs_size_t)strlen((const char*)data);
    } else if (app->user_data && APPCMD_HAS(argv, 'n')) {
        data = (const uint8_t*)app->user_data;
        len = (lfs_size_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0);
    } else return -1;
    if (len == 0) return -1;
    return (int)lfs_file_write(&ctx->lfs, f, data, len);
}

static int cmd_fseek(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'o')) return -1;
    lfs_file_t* f = resolve_handle(app, argv);
    if (!f) return -1;
    lfs_soff_t offset = (lfs_soff_t)strtoul(argv[APPCMD_ARG('o')], NULL, 0);
    int whence = APPCMD_HAS(argv, 'w') ? (int)strtoul(argv[APPCMD_ARG('w')], NULL, 0) : LFS_SEEK_SET;
    return (int)lfs_file_seek(&ctx->lfs, f, offset, whence);
}

/* --- dispatch --- */

static int lfs_cmd(app_t* app, const char* cmdname, const char** argv)
{
    static const lfs_cmd_entry_t table[] = {
        {"format",   cmd_format},
        {"mount",    cmd_mount},
        {"unmount",  cmd_unmount},
        {"info",     cmd_info},
        {"ls",       cmd_ls},
        {"cat",      cmd_cat},
        {"writenew", cmd_writenew},
        {"append",   cmd_append},
        {"rm",       cmd_rm},
        {"mkdir",    cmd_mkdir},
        {"mv",       cmd_mv},
        {"stat",     cmd_stat},
        {"open",     cmd_open},
        {"close",    cmd_close},
        {"fread",    cmd_fread},
        {"fwrite",   cmd_fwrite},
        {"fseek",    cmd_fseek},
        {NULL, NULL}
    };
    for (const lfs_cmd_entry_t* e = table; e->name; e++) {
        if (strcmp(cmdname, e->name) == 0)
            return e->handler(app, argv);
    }
    return -1;
}

static const papp_ops_t lfs_app_ops = {
    .open  = lfs_app_open,
    .close = lfs_app_close,
    .write = lfs_app_write,
    .read  = lfs_app_read,
    .cmd   = lfs_cmd,
};

REGISTER_APP_EX("littlefs", NULL, "1\0w25qxx_base", &lfs_app_ops, "littlefs on W25Q64");

#endif
