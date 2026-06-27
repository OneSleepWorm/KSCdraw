/**
 * @file    littlefs_fs.c
 * @note    littlefs 文件系统集成 (W25Q64 SPI NOR Flash)
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
#include "../inc/littlefs_fs.h"
#include "lfs.h"
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

static const papp_ops_t lfs_app_ops = {
    .open  = lfs_app_open,
    .close = lfs_app_close,
    .write = lfs_app_write,
    .read  = lfs_app_read,
};

REGISTER_APP_EX("littlefs", NULL, "1\0w25qxx_base", &lfs_app_ops, "littlefs on W25Q64");

#endif
