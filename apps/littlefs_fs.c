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
#if __USE_STM32__ || __USE_PC__

/* ── littlefs 配置 ──
 * LFS_READONLY 由 KSCconfig.h 的 __USE_LITTLEFS_READONLY__ 控制.
 * CMakeLists.txt 通过 -include 强制注入 KSCconfig.h 到所有编译单元,
 * 使 third_party/littlefs/lfs.c 也能读到该宏.
 */

/* ---- 底层块设备接口 (委托到 w25qxx_base) ---- */

typedef struct {
    app_t* flash;
    lfs_t  lfs;
    struct lfs_config cfg;
    uint8_t* read_buf;
    uint8_t* prog_buf;
    uint8_t* lookahead_buf;
    int mounted;
    char cwd[64];
    lfs_file_t* current;
    uint8_t user_level;
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
    lfs_ctx_t* ctx = (lfs_ctx_t*)osmalloc(sizeof(lfs_ctx_t));
    if (!ctx) return -1;
    ctx->flash = app->app0;
    ctx->mounted = 0;
    ctx->read_buf = NULL;
    ctx->prog_buf = NULL;
    ctx->lookahead_buf = NULL;

    int ret = appopen(ctx->flash);
    if (ret) { osfree(ctx); return ret; }

    ctx->read_buf = (uint8_t*)osmalloc(512);
    ctx->prog_buf = (uint8_t*)osmalloc(512);
    ctx->lookahead_buf = (uint8_t*)osmalloc(256);
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

    ctx->cwd[0] = '/'; ctx->cwd[1] = 'h'; ctx->cwd[2] = 'o'; ctx->cwd[3] = 'm';
    ctx->cwd[4] = 'e'; ctx->cwd[5] = 0;
    ctx->current = 0;
    ctx->user_level = 0;

    app->app_data = ctx;
    return 0;
}

static int lfs_app_close(app_t* app)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx) return 0;

    if (ctx->mounted) {
        if (ctx->current) {
            lfs_file_close(&ctx->lfs, ctx->current);
            osfree(ctx->current);
            ctx->current = NULL;
        }
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

/* ── path helpers (cwd resolution + quota) ── */

static void normalize_path(const char* in, char* out, size_t outsz)
{
    char tmp[128];
    char* dst = tmp;
    const char* src = in;
    const char* end = tmp + sizeof(tmp) - 1;

    *dst++ = '/';
    while (*src) {
        while (*src == '/') src++;
        if (!*src) break;
        const char* seg = src;
        while (*src && *src != '/') src++;
        int len = (int)(src - seg);
        if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            if (dst > tmp + 1) {
                dst--;
                while (dst > tmp && *dst != '/') dst--;
                dst++;
            }
        } else if (len == 1 && seg[0] == '.') {
        } else {
            if (dst > tmp + 1) *dst++ = '/';
            if (dst + len > end) len = (int)(end - dst);
            if (len <= 0) break;
            memcpy(dst, seg, len);
            dst += len;
        }
    }
    *dst = 0;
    size_t slen = (size_t)(dst - tmp);
    if (slen >= outsz) slen = outsz - 1;
    memcpy(out, tmp, slen);
    out[slen] = 0;
}

static void resolve_path(lfs_ctx_t* ctx, const char* rel, char* abs, size_t absz)
{
    if (rel[0] == '/') {
        normalize_path(rel, abs, absz);
    } else {
        char buf[128];
        int n = snprintf(buf, sizeof(buf), "%s/%s", ctx->cwd, rel);
        if (n > 0 && n < (int)sizeof(buf))
            normalize_path(buf, abs, absz);
        else
            normalize_path(rel, abs, absz);
    }
}

static int check_quota(lfs_ctx_t* ctx, const char* abs_path)
{
    struct lfs_info info;
    int ret = lfs_stat(&ctx->lfs, abs_path, &info);
    if (ret == 0) return 0;

    const char* last = strrchr(abs_path, '/');
    if (!last) return 0;
    int is_hidden = (*(last + 1) == '.');
    int dlen = (int)(last - abs_path);

    char dir[64];
    if (dlen == 0) { dir[0] = '/'; dir[1] = 0; }
    else { memcpy(dir, abs_path, dlen); dir[dlen] = 0; }

    lfs_dir_t ldir;
    ret = lfs_dir_open(&ctx->lfs, &ldir, dir);
    if (ret < 0) return 0;

    int hidden_cnt = 0, normal_cnt = 0;
    while (lfs_dir_read(&ctx->lfs, &ldir, &info) > 0) {
        if (info.name[0] == '.' && (info.name[1] == 0 || (info.name[1] == '.' && info.name[2] == 0))) continue;
        if (info.name[0] == '.') hidden_cnt++;
        else normal_cnt++;
    }
    lfs_dir_close(&ctx->lfs, &ldir);

    if (is_hidden) return (hidden_cnt >= 4) ? -12 : 0;
    return (normal_cnt >= 12) ? -12 : 0;
}

/* ── permission helpers ── */

#define ROOT_PW "87654321"

static int check_access(lfs_ctx_t* ctx, const char* abs_path, int need_write)
{
    int is_system = 0;
    if (strncmp(abs_path, "/sys", 4) == 0 && (abs_path[4] == '/' || abs_path[4] == 0)) is_system = 1;
    if (strncmp(abs_path, "/bin", 4) == 0 && (abs_path[4] == '/' || abs_path[4] == 0)) is_system = 1;
    if (strncmp(abs_path, "/apps", 5) == 0 && (abs_path[5] == '/' || abs_path[5] == 0)) is_system = 1;

    if (need_write) {
        if (is_system) return (ctx->user_level <= 1) ? 0 : -1;
        return (ctx->user_level <= 2) ? 0 : -1;
    }
    if (is_system) return (ctx->user_level <= 1) ? 0 : -1;
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
    (void)count;
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
        lfs_dir_op_t* op = (lfs_dir_op_t*)data;
        ret = lfs_dir_open(&ctx->lfs, op->dir, op->path);
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
    (void)count;
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!data) return -1;

    switch (mode) {
    case 0:
        return 0;
    case 1: {
        lfs_rw_t* rw = (lfs_rw_t*)data;
        return (int)lfs_file_read(&ctx->lfs, rw->file, rw->buffer, rw->size);
    }
    case 2:
        return (int)lfs_file_tell(&ctx->lfs, (lfs_file_t*)data);
    case 3:
        return (int)lfs_file_size(&ctx->lfs, (lfs_file_t*)data);
    case 4: {
        lfs_stat_t* st = (lfs_stat_t*)data;
        return lfs_stat(&ctx->lfs, st->path, st->info);
    }
    case 5: {
        lfs_dir_read_t* rd = (lfs_dir_read_t*)data;
        return lfs_dir_read(&ctx->lfs, rd->dir, rd->info);
    }
    case 6:
        return lfs_fs_stat(&ctx->lfs, (struct lfs_fsinfo*)data);
    default:
        return -1;
    }
}

/*
 * appcmd 接口
 * ================================================================
 * 所有输出写到 app->input_data (至少 256 B 缓冲).
 * 字符串数据通过 -d <text>, 二进制通过 input_data + -n <len>.
 * 持久文件 handle 由 ctx->current 管理 (单 slot), 不再使用 output_data/mode_data.
 */

typedef struct { const char* name; int (*handler)(app_t*, const char**); } lfs_cmd_entry_t;

/* --- FS 管理 --- */

static int cmd_format(app_t* app, const char** argv)
{
    (void)argv;
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx) { kscprintf("format: no ctx\r\n"); return -1; }
    if (ctx->user_level != 0) { kscprintf("format: need root\r\n"); return -1; }
    if (ctx->mounted) { kscprintf("format: already mounted\r\n"); return -1; }
    int ret = lfs_format(&ctx->lfs, &ctx->cfg);
    if (ret) kscprintf("format failed: %d\r\n", ret);
    return ret;
}

static int cmd_mount(app_t* app, const char** argv)
{
    (void)argv;
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || ctx->mounted) return -1;
    int ret = lfs_mount(&ctx->lfs, &ctx->cfg);
    if (ret == 0) {
        ctx->mounted = 1;
        strcpy(ctx->cwd, "/home");
        (void)lfs_mkdir(&ctx->lfs, "/sys");
        (void)lfs_mkdir(&ctx->lfs, "/bin");
        (void)lfs_mkdir(&ctx->lfs, "/apps");
        (void)lfs_mkdir(&ctx->lfs, "/home");
    }
    return ret;
}

static int cmd_unmount(app_t* app, const char** argv)
{
    (void)argv;
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (ctx->user_level != 0) return -1;
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
    if (ret < 0) return ret;
    if (!app->input_data && !app->output_fn) return -1;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "blk_size=%u blk_count=%u\r\n",
                     (unsigned)fi.block_size, (unsigned)fi.block_count);
    if (n > 0) {
        if (app->input_data) memcpy(app->input_data, buf, (size_t)n + 1);
        else app->output_fn(buf, n, app->output_ctx);
    }
    return ret;
}

/* --- 目录 / 文件一次性操作 --- */

static int cmd_ls(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!app->input_data && !app->output_fn) return -1;

    const char* raw = APPCMD_HAS(argv, 'p') ? argv[APPCMD_ARG('p')] : "";
    char resolved[64];
    if (raw[0]) resolve_path(ctx, raw, resolved, sizeof(resolved));
    else strcpy(resolved, ctx->cwd);

    if (check_access(ctx, resolved, 0) < 0) return -1;

    uint16_t max = APPCMD_HAS(argv, 'n') ? (uint16_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0) : 256;

    lfs_dir_t* dir = (lfs_dir_t*)osmalloc(sizeof(lfs_dir_t));
    if (!dir) return -1;
    int ret = lfs_dir_open(&ctx->lfs, dir, resolved);
    if (ret < 0) { osfree(dir); return ret; }

    struct lfs_info* info = (struct lfs_info*)osmalloc(sizeof(struct lfs_info));
    if (!info) { lfs_dir_close(&ctx->lfs, dir); osfree(dir); return -1; }

    int total = 0; char line[80];
    while (lfs_dir_read(&ctx->lfs, dir, info) > 0) {
        int n = snprintf(line, sizeof(line), "%c %s %u\r\n",
                         info->type == LFS_TYPE_DIR ? 'D' : 'F',
                         info->name, (unsigned)info->size);
        if (n <= 0) continue;
        if (app->input_data) {
            uint16_t rem = max > (uint16_t)total ? max - (uint16_t)total : 0;
            if (rem == 0) break;
            int cp = n > (int)rem ? (int)rem : n;
            memcpy((char*)app->input_data + total, line, (size_t)cp);
            total += cp;
        } else {
            app->output_fn(line, n, app->output_ctx);
            total += n;
        }
    }
    lfs_dir_close(&ctx->lfs, dir);
    osfree(info);
    osfree(dir);
    return total;
}

static int cmd_cat(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p')) return -1;
    if (!app->input_data && !app->output_fn) return -1;

    const char* raw = argv[APPCMD_ARG('p')];
    char resolved[64];
    resolve_path(ctx, raw, resolved, sizeof(resolved));

    if (check_access(ctx, resolved, 0) < 0) return -1;

    uint16_t max = APPCMD_HAS(argv, 'n') ? (uint16_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0) : 256;
    if (max == 0) return -1;

    lfs_file_t* f = (lfs_file_t*)osmalloc(sizeof(lfs_file_t));
    if (!f) return -1;
    int ret = lfs_file_open(&ctx->lfs, f, resolved, LFS_O_RDONLY);
    if (ret < 0) { osfree(f); return ret; }

    lfs_size_t total = 0;
    if (app->input_data) {
        lfs_size_t rd = lfs_file_read(&ctx->lfs, f, app->input_data, max);
        total = rd;
    } else {
        uint8_t chunk[64];
        lfs_size_t rd;
        while ((rd = lfs_file_read(&ctx->lfs, f, chunk, sizeof(chunk))) > 0) {
            app->output_fn(chunk, rd, app->output_ctx);
            total += rd;
        }
    }
    lfs_file_close(&ctx->lfs, f);
    osfree(f);
    return (int)total;
}

static int do_write_append(app_t* app, const char** argv, int append_mode)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p')) return -1;

    const char* raw = argv[APPCMD_ARG('p')];
    char resolved[64];
    resolve_path(ctx, raw, resolved, sizeof(resolved));

    if (check_access(ctx, resolved, 1) < 0) return -1;

    const uint8_t* data;
    lfs_size_t len;
    if (APPCMD_HAS(argv, 'd')) {
        data = (const uint8_t*)argv[APPCMD_ARG('d')];
        len = (lfs_size_t)strlen((const char*)data);
    } else if (app->input_data && APPCMD_HAS(argv, 'n')) {
        data = (const uint8_t*)app->input_data;
        len = (lfs_size_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0);
    } else return -1;
    if (len == 0) return -1;

    int q = check_quota(ctx, resolved);
    if (q < 0) return q;

    int flags = LFS_O_WRONLY | LFS_O_CREAT;
    if (!append_mode) flags |= LFS_O_TRUNC;

    lfs_file_t* f = (lfs_file_t*)osmalloc(sizeof(lfs_file_t));
    if (!f) return -1;
    int ret = lfs_file_open(&ctx->lfs, f, resolved, flags);
    if (ret < 0) { osfree(f); return ret; }

    if (append_mode) lfs_file_seek(&ctx->lfs, f, 0, LFS_SEEK_END);
    lfs_size_t written = lfs_file_write(&ctx->lfs, f, data, len);
    lfs_file_close(&ctx->lfs, f);
    osfree(f);
    return (int)written;
}

static int cmd_writenew(app_t* app, const char** argv)
{ return do_write_append(app, argv, 0); }

static int cmd_append(app_t* app, const char** argv)
{ return do_write_append(app, argv, 1); }

static int cmd_rm(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p')) return -1;

    const char* raw = argv[APPCMD_ARG('p')];
    char resolved[64];
    resolve_path(ctx, raw, resolved, sizeof(resolved));

    if (check_access(ctx, resolved, 1) < 0) return -1;

    return lfs_remove(&ctx->lfs, resolved);
}

static int cmd_mkdir(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p')) return -1;

    const char* raw = argv[APPCMD_ARG('p')];
    char resolved[64];
    resolve_path(ctx, raw, resolved, sizeof(resolved));

    if (check_access(ctx, resolved, 1) < 0) return -1;

    int q = check_quota(ctx, resolved);
    if (q < 0) return q;

    return lfs_mkdir(&ctx->lfs, resolved);
}

static int cmd_mv(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 's') || !APPCMD_HAS(argv, 'd')) return -1;

    const char* sraw = argv[APPCMD_ARG('s')];
    const char* draw = argv[APPCMD_ARG('d')];
    char src[64], dst[64];
    resolve_path(ctx, sraw, src, sizeof(src));
    resolve_path(ctx, draw, dst, sizeof(dst));

    if (check_access(ctx, src, 1) < 0 || check_access(ctx, dst, 1) < 0) return -1;

    {
        struct lfs_info ti;
        if (lfs_stat(&ctx->lfs, dst, &ti) < 0) {
            int q = check_quota(ctx, dst);
            if (q < 0) return q;
        }
    }

    return lfs_rename(&ctx->lfs, src, dst);
}

static int cmd_stat(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p')) return -1;
    if (!app->input_data && !app->output_fn) return -1;

    const char* raw = argv[APPCMD_ARG('p')];
    char resolved[64];
    resolve_path(ctx, raw, resolved, sizeof(resolved));

    if (check_access(ctx, resolved, 0) < 0) return -1;

    struct lfs_info* info = (struct lfs_info*)osmalloc(sizeof(struct lfs_info));
    if (!info) return -1;
    int ret = lfs_stat(&ctx->lfs, resolved, info);
    if (ret < 0) { osfree(info); return ret; }

    char buf[80];
    int n = snprintf(buf, sizeof(buf), "type=%c size=%u name=%s\r\n",
                     info->type == LFS_TYPE_DIR ? 'D' : 'F',
                     (unsigned)info->size, info->name);
    if (n > 0) {
        if (app->input_data) memcpy(app->input_data, buf, (size_t)n + 1);
        else app->output_fn(buf, n, app->output_ctx);
    }
    osfree(info);
    return ret;
}

/* ── cd / pwd (cwd 管理) ── */

static int cmd_cd(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p')) return -1;

    const char* raw = argv[APPCMD_ARG('p')];
    char resolved[64];
    resolve_path(ctx, raw, resolved, sizeof(resolved));

    struct lfs_info info;
    int ret = lfs_stat(&ctx->lfs, resolved, &info);
    if (ret < 0 || info.type != LFS_TYPE_DIR) return -1;

    strcpy(ctx->cwd, resolved);
    return 0;
}

static int cmd_pwd(app_t* app, const char** argv)
{
    (void)argv;
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!app->input_data && !app->output_fn) return -1;

    int n = (int)strlen(ctx->cwd);
    if (app->input_data) {
        memcpy(app->input_data, ctx->cwd, (size_t)n + 1);
    } else {
        app->output_fn(ctx->cwd, (uint32_t)n, app->output_ctx);
        app->output_fn("\r\n", 2, app->output_ctx);
    }
    return n;
}

/* ── su / whoami (权限管理) ── */

static int cmd_whoami(app_t* app, const char** argv)
{
    (void)argv;
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx) return -1;
    if (!app->input_data && !app->output_fn) return -1;

    static const char* names[] = {"root", "admin", "user", "guest"};
    const char* name = names[ctx->user_level <= 3 ? ctx->user_level : 3];
    int n = (int)strlen(name);
    if (app->input_data) {
        memcpy(app->input_data, name, (size_t)n + 1);
    } else {
        app->output_fn(name, (uint32_t)n, app->output_ctx);
        app->output_fn("\r\n", 2, app->output_ctx);
    }
    return n;
}

static int cmd_su(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx) return -1;
    if (!APPCMD_HAS(argv, 'u')) return -1;

    const char* uname = argv[APPCMD_ARG('u')];
    static const char* rnames[] = {"root", "admin", "user", "guest"};
    static const uint8_t rlevs[] = {0, 1, 2, 3};
    int idx = -1;
    for (int i = 0; i < 4; i++) {
        if (strcmp(uname, rnames[i]) == 0) { idx = i; break; }
    }
    if (idx < 0) return -1;

    if (rlevs[idx] == 0) {
        if (!APPCMD_HAS(argv, 'p')) return -1;
        if (strcmp(argv[APPCMD_ARG('p')], ROOT_PW) != 0) return -1;
    }

    ctx->user_level = rlevs[idx];
    return 0;
}

/* ── 持久文件 handle (ctx->current, 单 slot) ── */

static int cmd_open(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) { kscprintf("open: not mounted\r\n"); return -1; }
    if (!APPCMD_HAS(argv, 'p')) { kscprintf("open: missing -p\r\n"); return -1; }
    if (ctx->current) { kscprintf("open: close current first\r\n"); return -1; }

    const char* raw = argv[APPCMD_ARG('p')];
    char resolved[64];
    resolve_path(ctx, raw, resolved, sizeof(resolved));

    int flags = APPCMD_HAS(argv, 'f') ? (int)strtoul(argv[APPCMD_ARG('f')], NULL, 0) : LFS_O_RDONLY;

    if (check_access(ctx, resolved, (flags & (LFS_O_WRONLY | LFS_O_RDWR | LFS_O_CREAT)) ? 1 : 0) < 0) {
        kscprintf("open: permission denied\r\n"); return -1;
    }

    if (flags & LFS_O_CREAT) {
        struct lfs_info ti;
        if (lfs_stat(&ctx->lfs, resolved, &ti) < 0) {
            int q = check_quota(ctx, resolved);
            if (q < 0) { kscprintf("open: quota exceeded (%d)\r\n", q); return q; }
        }
    }

    lfs_file_t* f = (lfs_file_t*)osmalloc(sizeof(lfs_file_t));
    if (!f) { kscprintf("open: alloc fail\r\n"); return -1; }
    int ret = lfs_file_open(&ctx->lfs, f, resolved, flags);
    if (ret < 0) { osfree(f); kscprintf("open: lfs error %d\r\n", ret); return ret; }

    ctx->current = f;
    return 0;
}

static int cmd_close(app_t* app, const char** argv)
{
    (void)argv;
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) { kscprintf("close: not mounted\r\n"); return -1; }
    if (!ctx->current) { kscprintf("close: no open file\r\n"); return -1; }

    int ret = lfs_file_close(&ctx->lfs, ctx->current);
    osfree(ctx->current);
    ctx->current = NULL;
    if (ret) kscprintf("close: lfs error %d\r\n", ret);
    return ret;
}

static int cmd_fread(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!ctx->current) return -1;
    if (!app->input_data && !app->output_data && !app->output_fn) return -1;
    if (!APPCMD_HAS(argv, 'n')) return -1;

    lfs_size_t n = (lfs_size_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0);
    if (n == 0) return -1;

    if (app->input_data)
        return (int)lfs_file_read(&ctx->lfs, ctx->current, app->input_data, n);
    if (app->output_data)
        return (int)lfs_file_read(&ctx->lfs, ctx->current, app->output_data, n);

    uint8_t chunk[64];
    lfs_size_t total = 0, rd;
    while (total < n) {
        lfs_size_t want = (n - total) > sizeof(chunk) ? sizeof(chunk) : (n - total);
        rd = lfs_file_read(&ctx->lfs, ctx->current, chunk, want);
        if (rd <= 0) break;
        app->output_fn(chunk, rd, app->output_ctx);
        total += rd;
    }
    return (int)total;
}

static int cmd_fwrite(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) { kscprintf("fwrite: not mounted\r\n"); return -1; }
    if (!ctx->current) { kscprintf("fwrite: no open file\r\n"); return -1; }

    const uint8_t* data;
    lfs_size_t len;
    if (APPCMD_HAS(argv, 'd')) {
        data = (const uint8_t*)argv[APPCMD_ARG('d')];
        len = (lfs_size_t)strlen((const char*)data);
    } else if (app->input_data && APPCMD_HAS(argv, 'n')) {
        data = (const uint8_t*)app->input_data;
        len = (lfs_size_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0);
    } else { kscprintf("fwrite: no data\r\n"); return -1; }
    if (len == 0) { kscprintf("fwrite: zero len\r\n"); return -1; }
    int ret = (int)lfs_file_write(&ctx->lfs, ctx->current, data, len);
    if (ret < 0) kscprintf("fwrite: error %d\r\n", ret);
    return ret;
}

static int cmd_echo(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'd')) return -1;

    const char* msg = argv[APPCMD_ARG('d')];
    size_t len = strlen(msg);

    if (APPCMD_HAS(argv, 'p')) {
        char resolved[64];
        resolve_path(ctx, argv[APPCMD_ARG('p')], resolved, sizeof(resolved));
        lfs_file_t f;
        int ret = lfs_file_open(&ctx->lfs, &f, resolved,
                                LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
        if (ret < 0) { kscprintf("echo: open err %d\r\n", ret); return ret; }
        ret = (int)lfs_file_write(&ctx->lfs, &f, msg, len);
        if (ret < 0) kscprintf("echo: write err %d\r\n", ret);
        lfs_file_close(&ctx->lfs, &f);
        return ret;
    }

    if (app->output_fn) {
        app->output_fn(msg, (uint32_t)len, app->output_ctx);
        app->output_fn("\r\n", 2, app->output_ctx);
    }
    return (int)len;
}

static int cmd_fseek(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!ctx->current) return -1;
    if (!APPCMD_HAS(argv, 'o')) return -1;

    lfs_soff_t offset = (lfs_soff_t)strtoul(argv[APPCMD_ARG('o')], NULL, 0);
    int whence = APPCMD_HAS(argv, 'w') ? (int)strtoul(argv[APPCMD_ARG('w')], NULL, 0) : LFS_SEEK_SET;
    return (int)lfs_file_seek(&ctx->lfs, ctx->current, offset, whence);
}

/* ── feed: open file and push to target (uart/gui) ── */

static int cmd_feed(app_t* app, const char** argv)
{
    lfs_ctx_t* ctx = (lfs_ctx_t*)app->app_data;
    if (!ctx || !ctx->mounted) return -1;
    if (!APPCMD_HAS(argv, 'p')) return -1;

    const char* raw = argv[APPCMD_ARG('p')];
    char resolved[64];
    resolve_path(ctx, raw, resolved, sizeof(resolved));

    if (check_access(ctx, resolved, 0) < 0) return -1;

    int use_gui = APPCMD_HAS(argv, 'g');
    if (!use_gui && APPCMD_HAS(argv, 't'))
        use_gui = (strcmp(argv[APPCMD_ARG('t')], "gui") == 0);

    lfs_file_t* f = (lfs_file_t*)osmalloc(sizeof(lfs_file_t));
    if (!f) return -1;
    int ret = lfs_file_open(&ctx->lfs, f, resolved, LFS_O_RDONLY);
    if (ret < 0) { osfree(f); return ret; }

    lfs_size_t total = 0;

    if (use_gui) {
        app_t* gui = appget("KSCGUI");
        if (!gui) { lfs_file_close(&ctx->lfs, f); osfree(f); return -1; }
        appopen(gui);

        uint16_t tx = APPCMD_HAS(argv, 'x') ? (uint16_t)strtoul(argv[APPCMD_ARG('x')], NULL, 0) : 20;
        uint16_t ty = APPCMD_HAS(argv, 'y') ? (uint16_t)strtoul(argv[APPCMD_ARG('y')], NULL, 0) : 20;
        uint16_t tw = APPCMD_HAS(argv, 'w') ? (uint16_t)strtoul(argv[APPCMD_ARG('w')], NULL, 0) : 200;
        uint16_t th_ = APPCMD_HAS(argv, 'h') ? (uint16_t)strtoul(argv[APPCMD_ARG('h')], NULL, 0) : 200;
        uint32_t tc = APPCMD_HAS(argv, 'c') ? strtoul(argv[APPCMD_ARG('c')], NULL, 16) : 0x001F;

        if (!gui->app_data) { lfs_file_close(&ctx->lfs, f); osfree(f); return -1; }

        {
            char wc[80];
            snprintf(wc, sizeof(wc), "wcreate -x %u -y %u -w %u -h %u -c %06lX", tx, ty, tw, th_, (unsigned long)tc);
            int th = appcmd(gui, wc);
            if (th <= 0) { lfs_file_close(&ctx->lfs, f); osfree(f); return -1; }
            char sel[24];
            snprintf(sel, sizeof(sel), "wselect -t %d", th);
            appcmd(gui, sel);
        }

        const uint16_t font_w = 8;
        const uint16_t line_h = 16;
        const uint16_t max_chars = tw / font_w;
        uint16_t y = 0;

        uint8_t chunk[64];
        char line[60];
        int line_len = 0;

        while ((ret = lfs_file_read(&ctx->lfs, f, chunk, sizeof(chunk))) > 0) {
            total += ret;
            for (int i = 0; i < ret; i++) {
                uint8_t c = chunk[i];
                if (c == '\n' || line_len >= (int)(sizeof(line) - 1)) {
                    if (line_len > 0) {
                        line[line_len] = '\0';
                        char* src = line;
                        while (*src) {
                            if (y >= th_) break;
                            int seg_len = 0;
                            while (src[seg_len] && seg_len < (int)max_chars) seg_len++;

                            char sc[128];
                            char* sp = sc;
                            int rem = sizeof(sc);
                            int n = snprintf(sp, rem, "string -x 4 -y %d -s \"", y);
                            if (n > 0) { int adv = (n < rem ? n : rem - 1); sp += adv; rem -= adv; }
                            for (int j = 0; j < seg_len && rem > 2; j++) {
                                if (src[j] == '"' || src[j] == '\\') { *sp++ = '\\'; rem--; }
                                *sp++ = src[j]; rem--;
                            }
                            if (rem > 1) { *sp++ = '"'; rem--; }
                            snprintf(sp, rem, " -c FFFF -b %06lX", (unsigned long)tc);
                            appcmd(gui, sc);

                            y += line_h;
                            src += seg_len;
                        }
                    }
                    line_len = 0;
                } else if (c != '\r') {
                    line[line_len++] = (char)c;
                }
            }
        }
        if (line_len > 0 && y < th_) {
            line[line_len] = '\0';
            char* src = line;
            while (*src && y < th_) {
                int seg_len = 0;
                while (src[seg_len] && seg_len < (int)max_chars) seg_len++;

                char sc[128];
                char* sp = sc;
                int rem = sizeof(sc);
                int n = snprintf(sp, rem, "string -x 4 -y %d -s \"", y);
                if (n > 0) { int adv = (n < rem ? n : rem - 1); sp += adv; rem -= adv; }
                for (int j = 0; j < seg_len && rem > 2; j++) {
                    if (src[j] == '"' || src[j] == '\\') { *sp++ = '\\'; rem--; }
                    *sp++ = src[j]; rem--;
                }
                if (rem > 1) { *sp++ = '"'; rem--; }
                snprintf(sp, rem, " -c FFFF -b %06lX", (unsigned long)tc);
                appcmd(gui, sc);

                y += line_h;
                src += seg_len;
            }
        }
    } else {
        if (!app->input_data && !app->output_fn) {
            lfs_file_close(&ctx->lfs, f); osfree(f); return -1;
        }
        if (app->input_data) {
            lfs_size_t max = APPCMD_HAS(argv, 'n')
                ? (lfs_size_t)strtoul(argv[APPCMD_ARG('n')], NULL, 0) : 256;
            total = lfs_file_read(&ctx->lfs, f, app->input_data, max);
        } else {
            uint8_t chunk[64];
            lfs_size_t rd;
            while ((rd = lfs_file_read(&ctx->lfs, f, chunk, sizeof(chunk))) > 0) {
                app->output_fn(chunk, rd, app->output_ctx);
                total += rd;
            }
        }
    }

    lfs_file_close(&ctx->lfs, f);
    osfree(f);
    return (int)total;
}

/* --- dispatch --- */

#if __USE_APP_HELP__
static int cmd_help(app_t* app, const char** argv)
{
    (void)argv;
    if (app->output_fn) {
        const char* text =
            "format mount unmount info\r\n"
            "ls -p <path> [-n <max>]\r\n"
            "cat -p <path> [-n <max>]\r\n"
            "echo -d <text> [-p <path>]\r\n"
            "feed -p <path> [-g]\r\n"
            "writenew -p <path> [-d <text>]\r\n"
            "append -p <path> [-d <text>]\r\n"
            "rm -p <path>  mkdir -p <path>\r\n"
            "mv -s <src> -d <dst>  stat -p <path>\r\n"
            "cd -p <path>  pwd  whoami\r\n"
            "su -u <user> [-p <pw>]\r\n"
            "open -p <path> [-f <flags>]  close\r\n"
            "fread [-n <max>]  fwrite [-d <text>]\r\n"
            "fseek -o <offset> [-w <whence>]\r\n";
        app->output_fn(text, strlen(text), app->output_ctx);
    }
    return 0;
}
#endif

static int lfs_cmd(app_t* app, const char* cmdname, const char** argv)
{
    static const lfs_cmd_entry_t table[] = {
        {"format",   cmd_format},
        {"mount",    cmd_mount},
        {"unmount",  cmd_unmount},
        {"info",     cmd_info},
        {"ls",       cmd_ls},
        {"cat",      cmd_cat},
        {"echo",     cmd_echo},
        {"feed",     cmd_feed},
        {"writenew", cmd_writenew},
        {"append",   cmd_append},
        {"rm",       cmd_rm},
        {"mkdir",    cmd_mkdir},
        {"mv",       cmd_mv},
        {"stat",     cmd_stat},
        {"cd",       cmd_cd},
        {"pwd",      cmd_pwd},
        {"whoami",   cmd_whoami},
        {"su",       cmd_su},
        {"open",     cmd_open},
        {"close",    cmd_close},
        {"fread",    cmd_fread},
        {"fwrite",   cmd_fwrite},
        {"fseek",    cmd_fseek},
#if __USE_APP_HELP__
        {"help",     cmd_help},
#endif
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
    .read  = lfs_app_read,
    .write = lfs_app_write,
    .cmd   = lfs_cmd,
};

REGISTER_APP_EX("littlefs", NULL, "1\0w25qxx_base", &lfs_app_ops, "littlefs on W25Q64");

#endif
