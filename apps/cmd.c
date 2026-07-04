#include "../inc/app.h"
#include "../inc/KSCOSsystem.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../inc/littlefs_fs.h"

#if __USE_STM32__

#define CMD_LINE_BUF_SIZE   128
#define CMD_MAX_ARGS        10
#define CWD_SIZE            32

typedef struct cmd_ctx {
    char       line[CMD_LINE_BUF_SIZE];
    uint8_t    pos;
    app_t*     uart;
    app_t*     gui;
    uint8_t    prompt_ready;
    app_t*     fs;
    lfs_file_t file;
    uint8_t*   rw_buf;
    char       cwd[CWD_SIZE];
} cmd_ctx_t;

typedef int (*cmd_handler_t)(struct cmd_ctx*, int argc, char** argv);

typedef struct {
    const char*    name;
    cmd_handler_t  handler;
} cmd_entry_t;

static void cmd_puts(cmd_ctx_t* ctx, const char* s)
{
    appwrite(ctx->uart, (void*)s, strlen(s), 0x11);
}

static void cmd_putc(cmd_ctx_t* ctx, char c)
{
    appwrite(ctx->uart, &c, 1, 0x11);
}

static int a2i(const char* s) { return (int)strtoul(s, NULL, 0); }
static int cmd_help(cmd_ctx_t* ctx, int argc, char** argv);
static int bridge_fs(cmd_ctx_t* ctx, int argc, char** argv);
static int cmd_ls(cmd_ctx_t* ctx, int argc, char** argv);
static int cmd_cat(cmd_ctx_t* ctx, int argc, char** argv);
static int cmd_write(cmd_ctx_t* ctx, int argc, char** argv);
static int cmd_append(cmd_ctx_t* ctx, int argc, char** argv);
static int cmd_rm(cmd_ctx_t* ctx, int argc, char** argv);
static int cmd_mkdir(cmd_ctx_t* ctx, int argc, char** argv);
static int cmd_mv(cmd_ctx_t* ctx, int argc, char** argv);
static int cmd_stat(cmd_ctx_t* ctx, int argc, char** argv);
static int cmd_cd(cmd_ctx_t* ctx, int argc, char** argv);
static int cmd_pwd(cmd_ctx_t* ctx, int argc, char** argv);

static void resolve_path(cmd_ctx_t* ctx, const char* path, char* dst)
{
    char tmp[CWD_SIZE];
    if (path[0] == '/') {
        strncpy(tmp, path, CWD_SIZE - 1);
        tmp[CWD_SIZE - 1] = '\0';
    } else {
        int cl = (int)strlen(ctx->cwd);
        int pl = (int)strlen(path);
        if (cl + 1 + pl >= CWD_SIZE) { dst[0] = '\0'; return; }
        memcpy(tmp, ctx->cwd, cl);
        if (ctx->cwd[cl - 1] != '/') tmp[cl++] = '/';
        memcpy(tmp + cl, path, pl + 1);
    }
    char* segs[16];
    int n = 0;
    char* p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        segs[n++] = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = '\0';
    }
    int pos = 0;
    dst[pos++] = '/';
    for (int i = 0; i < n; i++) {
        if (strcmp(segs[i], ".") == 0) continue;
        if (strcmp(segs[i], "..") == 0) {
            if (pos > 1) {
                pos--;
                while (pos > 0 && dst[pos - 1] != '/') pos--;
            }
            continue;
        }
        int len = (int)strlen(segs[i]);
        if (pos + len + 1 >= CWD_SIZE) break;
        if (pos > 1) dst[pos++] = '/';
        memcpy(dst + pos, segs[i], len);
        pos += len;
    }
    dst[pos] = '\0';
}

static int cmd_echo(cmd_ctx_t* ctx, int argc, char** argv)
{
    for (int i = 0; i < argc; i++) {
        if (i > 0) cmd_putc(ctx, ' ');
        cmd_puts(ctx, argv[i]);
    }
    cmd_puts(ctx, "\r\n");
    return 1;
}

static int bridge_gui(cmd_ctx_t* ctx, int argc, char** argv)
{
    app_t* gui = ctx->gui;
    if (!gui) { cmd_puts(ctx, "ERR: KSCGUI not found\r\n"); return -1; }
    if (argc < 1) { cmd_puts(ctx, "ERR: gui needs subcmd\r\n"); return -1; }

    const char* cmd = argv[0];
    int ret = 0;

    if (strcmp(cmd, "setspi") == 0 && argc >= 2)
        ret = appioctl(gui, "setspi", a2i(argv[1]));
    else if (strcmp(cmd, "init") == 0)
        ret = appioctl(gui, "init");
    else if (strcmp(cmd, "wcreate") == 0 && argc >= 6)
        ret = appioctl(gui, "wcreate", a2i(argv[1]), a2i(argv[2]), a2i(argv[3]), a2i(argv[4]), a2i(argv[5]));
    else if (strcmp(cmd, "wselect") == 0 && argc >= 2)
        ret = appioctl(gui, "wselect", a2i(argv[1]));
    else if (strcmp(cmd, "trenderall") == 0)
        ret = appioctl(gui, "trenderall");
    else if (strcmp(cmd, "wclear") == 0)
        ret = appioctl(gui, "wclear");
    else if (strcmp(cmd, "clear") == 0 && argc >= 2)
        ret = appioctl(gui, "clear", a2i(argv[1]));
    else if (strcmp(cmd, "pixel") == 0 && argc >= 4)
        ret = appioctl(gui, "pixel", a2i(argv[1]), a2i(argv[2]), a2i(argv[3]));
    else if (strcmp(cmd, "fill") == 0 && argc >= 6)
        ret = appioctl(gui, "fill", a2i(argv[1]), a2i(argv[2]), a2i(argv[3]), a2i(argv[4]), a2i(argv[5]));
    else if (strcmp(cmd, "rect") == 0 && argc >= 6)
        ret = appioctl(gui, "rect", a2i(argv[1]), a2i(argv[2]), a2i(argv[3]), a2i(argv[4]), a2i(argv[5]));
    else if (strcmp(cmd, "line") == 0 && argc >= 6)
        ret = appioctl(gui, "line", a2i(argv[1]), a2i(argv[2]), a2i(argv[3]), a2i(argv[4]), a2i(argv[5]));
    else if (strcmp(cmd, "char") == 0 && argc >= 6)
        ret = appioctl(gui, "char", a2i(argv[1]), a2i(argv[2]), (int)argv[3][0], a2i(argv[4]), a2i(argv[5]));
    else if (strcmp(cmd, "string") == 0 && argc >= 6)
        ret = appioctl(gui, "string", a2i(argv[1]), a2i(argv[2]), argv[3], a2i(argv[4]), a2i(argv[5]));
    else {
        cmd_puts(ctx, "ERR: gui: unknown subcmd\r\n");
        return 0;
    }

    if (ret < 0)
        cmd_puts(ctx, "ERR\r\n");
    else if (ret == 0)
        cmd_puts(ctx, "FAIL\r\n");
    else if (ret > 1) {
        char buf[8];
        int len = snprintf(buf, sizeof(buf), "%d\r\n", ret);
        appwrite(ctx->uart, buf, len, 0x11);
    }
    return ret;
}

static int bridge_fs(cmd_ctx_t* ctx, int argc, char** argv)
{
    app_t* fs = ctx->fs;
    if (!fs) { cmd_puts(ctx, "ERR: littlefs not found\r\n"); return -1; }
    if (argc < 1) { cmd_puts(ctx, "ERR: fs needs subcmd\r\n"); return -1; }

    const char* cmd = argv[0];
    int ret = 0;

    if (strcmp(cmd, "format") == 0)
        ret = appwrite(fs, NULL, 0, 1);
    else if (strcmp(cmd, "mount") == 0)
        ret = appwrite(fs, NULL, 0, 2);
    else if (strcmp(cmd, "unmount") == 0)
        ret = appwrite(fs, NULL, 0, 3);
    else if (strcmp(cmd, "info") == 0) {
        struct lfs_fsinfo fi;
        ret = appread(fs, &fi, 0, 6);
        if (ret >= 0) {
            char buf[64];
            int n = snprintf(buf, sizeof(buf), "blk_size=%u blk_count=%u\r\n",
                (unsigned)fi.block_size, (unsigned)fi.block_count);
            appwrite(ctx->uart, buf, n, 0x11);
        }
    } else {
        cmd_puts(ctx, "ERR: fs: unknown subcmd\r\n");
        return 0;
    }

    if (ret < 0)
        cmd_puts(ctx, "ERR\r\n");
    else
        cmd_puts(ctx, "OK\r\n");
    return ret;
}

static int cmd_cd(cmd_ctx_t* ctx, int argc, char** argv)
{
    const char* target = (argc > 0) ? argv[0] : "/";
    char resolved[CWD_SIZE];
    resolve_path(ctx, target, resolved);
    if (!ctx->fs) { cmd_puts(ctx, "ERR: littlefs not found\r\n"); return -1; }
    lfs_dir_t* dir = (lfs_dir_t*)osmalloc(sizeof(lfs_dir_t));
    if (!dir) { cmd_puts(ctx, "ERR: no mem\r\n"); return -1; }
    lfs_dir_op_t dop = {dir, resolved};
    int ret = appwrite(ctx->fs, &dop, 0, 11);
    appwrite(ctx->fs, dir, 0, 12);
    osfree(dir);
    if (ret < 0) { cmd_puts(ctx, "ERR: no such dir\r\n"); return ret; }
    strncpy(ctx->cwd, resolved, CWD_SIZE - 1);
    ctx->cwd[CWD_SIZE - 1] = '\0';
    return 1;
}

static int cmd_pwd(cmd_ctx_t* ctx, int argc, char** argv)
{
    (void)argc; (void)argv;
    cmd_puts(ctx, ctx->cwd);
    cmd_puts(ctx, "\r\n");
    return 1;
}

static int cmd_ls(cmd_ctx_t* ctx, int argc, char** argv)
{
    if (!ctx->fs) { cmd_puts(ctx, "ERR: littlefs not found\r\n"); return -1; }
    char resolved[CWD_SIZE];
    const char* path;
    if (argc > 0) { resolve_path(ctx, argv[0], resolved); path = resolved; }
    else { path = ctx->cwd; }

    lfs_dir_t* dir = (lfs_dir_t*)osmalloc(sizeof(lfs_dir_t));
    if (!dir) { cmd_puts(ctx, "ERR: no mem\r\n"); return -1; }

    lfs_dir_op_t dop = {dir, path};
    int ret = appwrite(ctx->fs, &dop, 0, 11);
    if (ret < 0) {
        cmd_puts(ctx, "ERR: cannot open dir\r\n");
        osfree(dir);
        return ret;
    }

    struct lfs_info* info = (struct lfs_info*)osmalloc(sizeof(struct lfs_info));
    if (!info) {
        appwrite(ctx->fs, dir, 0, 12);
        osfree(dir);
        cmd_puts(ctx, "ERR: no mem\r\n");
        return -1;
    }

    lfs_dir_read_t dr = {dir, info};
    while ((ret = appread(ctx->fs, &dr, 0, 5)) > 0) {
        char buf[72];
        int n = snprintf(buf, sizeof(buf), "%c %8u %s\r\n",
            (info->type == LFS_TYPE_DIR) ? 'D' : 'F',
            (unsigned)info->size, info->name);
        appwrite(ctx->uart, buf, n, 0x11);
    }

    appwrite(ctx->fs, dir, 0, 12);
    osfree(info);
    osfree(dir);
    return 1;
}

static int cmd_cat(cmd_ctx_t* ctx, int argc, char** argv)
{
    if (!ctx->fs) { cmd_puts(ctx, "ERR: littlefs not found\r\n"); return -1; }
    if (argc < 1) { cmd_puts(ctx, "usage: cat <path>\r\n"); return -1; }
    char resolved[CWD_SIZE];
    resolve_path(ctx, argv[0], resolved);

    lfs_file_op_t op = {&ctx->file, resolved, LFS_O_RDONLY};
    int ret = appwrite(ctx->fs, &op, 0, 4);
    if (ret < 0) { cmd_puts(ctx, "ERR: cannot open\r\n"); return ret; }

    int n;
    do {
        lfs_rw_t rw = {&ctx->file, ctx->rw_buf, 128};
        n = appread(ctx->fs, &rw, 0, 1);
        if (n > 0) appwrite(ctx->uart, ctx->rw_buf, n, 0x11);
    } while (n > 0);

    appwrite(ctx->fs, &ctx->file, 0, 5);
    return 1;
}

static int cmd_write(cmd_ctx_t* ctx, int argc, char** argv)
{
    if (!ctx->fs) { cmd_puts(ctx, "ERR: littlefs not found\r\n"); return -1; }
    if (argc < 2) { cmd_puts(ctx, "usage: write <path> <text>\r\n"); return -1; }
    char resolved[CWD_SIZE];
    resolve_path(ctx, argv[0], resolved);

    int pos = 0;
    for (int i = 1; i < argc; i++) {
        if (i > 1) ctx->rw_buf[pos++] = ' ';
        int len = (int)strlen(argv[i]);
        if (pos + len >= 512) len = 512 - pos - 1;
        memcpy(ctx->rw_buf + pos, argv[i], len);
        pos += len;
    }
    ctx->rw_buf[pos] = '\0';

    lfs_file_op_t op = {&ctx->file, resolved, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC};
    int ret = appwrite(ctx->fs, &op, 0, 4);
    if (ret < 0) { cmd_puts(ctx, "ERR: cannot open\r\n"); return ret; }

    lfs_rw_t rw = {&ctx->file, ctx->rw_buf, (lfs_size_t)pos};
    ret = appwrite(ctx->fs, &rw, 0, 6);
    appwrite(ctx->fs, &ctx->file, 0, 5);

    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d\r\n", pos);
    appwrite(ctx->uart, buf, n, 0x11);
    return 1;
}

static int cmd_append(cmd_ctx_t* ctx, int argc, char** argv)
{
    if (!ctx->fs) { cmd_puts(ctx, "ERR: littlefs not found\r\n"); return -1; }
    if (argc < 2) { cmd_puts(ctx, "usage: append <path> <text>\r\n"); return -1; }
    char resolved[CWD_SIZE];
    resolve_path(ctx, argv[0], resolved);

    int pos = 0;
    for (int i = 1; i < argc; i++) {
        if (i > 1) ctx->rw_buf[pos++] = ' ';
        int len = (int)strlen(argv[i]);
        if (pos + len >= 512) len = 512 - pos - 1;
        memcpy(ctx->rw_buf + pos, argv[i], len);
        pos += len;
    }
    ctx->rw_buf[pos] = '\0';

    lfs_file_op_t op = {&ctx->file, resolved, LFS_O_WRONLY | LFS_O_CREAT};
    int ret = appwrite(ctx->fs, &op, 0, 4);
    if (ret < 0) { cmd_puts(ctx, "ERR: cannot open\r\n"); return ret; }

    lfs_seek_t sk = {&ctx->file, 0, LFS_SEEK_END};
    appwrite(ctx->fs, &sk, 0, 7);

    lfs_rw_t rw = {&ctx->file, ctx->rw_buf, (lfs_size_t)pos};
    ret = appwrite(ctx->fs, &rw, 0, 6);
    appwrite(ctx->fs, &ctx->file, 0, 5);

    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d\r\n", pos);
    appwrite(ctx->uart, buf, n, 0x11);
    return 1;
}

static int cmd_rm(cmd_ctx_t* ctx, int argc, char** argv)
{
    if (!ctx->fs) { cmd_puts(ctx, "ERR: littlefs not found\r\n"); return -1; }
    if (argc < 1) { cmd_puts(ctx, "usage: rm <path>\r\n"); return -1; }
    char resolved[CWD_SIZE];
    resolve_path(ctx, argv[0], resolved);
    int ret = appwrite(ctx->fs, resolved, 0, 8);
    if (ret < 0) { cmd_puts(ctx, "ERR: rm failed\r\n"); return ret; }
    cmd_puts(ctx, "OK\r\n");
    return 1;
}

static int cmd_mkdir(cmd_ctx_t* ctx, int argc, char** argv)
{
    if (!ctx->fs) { cmd_puts(ctx, "ERR: littlefs not found\r\n"); return -1; }
    if (argc < 1) { cmd_puts(ctx, "usage: mkdir <path>\r\n"); return -1; }
    char resolved[CWD_SIZE];
    resolve_path(ctx, argv[0], resolved);
    int ret = appwrite(ctx->fs, resolved, 0, 10);
    if (ret < 0) { cmd_puts(ctx, "ERR: mkdir failed\r\n"); return ret; }
    cmd_puts(ctx, "OK\r\n");
    return 1;
}

static int cmd_mv(cmd_ctx_t* ctx, int argc, char** argv)
{
    if (!ctx->fs) { cmd_puts(ctx, "ERR: littlefs not found\r\n"); return -1; }
    if (argc < 2) { cmd_puts(ctx, "usage: mv <src> <dst>\r\n"); return -1; }
    char src_res[CWD_SIZE], dst_res[CWD_SIZE];
    resolve_path(ctx, argv[0], src_res);
    resolve_path(ctx, argv[1], dst_res);
    lfs_rename_t rn = {src_res, dst_res};
    int ret = appwrite(ctx->fs, &rn, 0, 9);
    if (ret < 0) { cmd_puts(ctx, "ERR: mv failed\r\n"); return ret; }
    cmd_puts(ctx, "OK\r\n");
    return 1;
}

static int cmd_stat(cmd_ctx_t* ctx, int argc, char** argv)
{
    if (!ctx->fs) { cmd_puts(ctx, "ERR: littlefs not found\r\n"); return -1; }
    if (argc < 1) { cmd_puts(ctx, "usage: stat <path>\r\n"); return -1; }
    char resolved[CWD_SIZE];
    resolve_path(ctx, argv[0], resolved);

    struct lfs_info* info = (struct lfs_info*)osmalloc(sizeof(struct lfs_info));
    if (!info) { cmd_puts(ctx, "ERR: no mem\r\n"); return -1; }

    lfs_stat_t st = {resolved, info};
    int ret = appread(ctx->fs, &st, 0, 4);
    if (ret < 0) {
        cmd_puts(ctx, "ERR: stat failed\r\n");
        osfree(info);
        return ret;
    }

    char buf[72];
    int n = snprintf(buf, sizeof(buf), "type=%c size=%u name=%s\r\n",
        (info->type == LFS_TYPE_DIR) ? 'D' : 'F',
        (unsigned)info->size, info->name);
    appwrite(ctx->uart, buf, n, 0x11);
    osfree(info);
    return 1;
}

static int cmd_help(cmd_ctx_t* ctx, int argc, char** argv)
{
    (void)argc; (void)argv;
    cmd_puts(ctx, "help                           : show this help\r\n");
    cmd_puts(ctx, "echo <text...>                 : echo arguments back\r\n");
    cmd_puts(ctx, "gui setspi <inst>              : select SPI instance (1/2)\r\n");
    cmd_puts(ctx, "gui init                       : init ST7789\r\n");
    cmd_puts(ctx, "gui wcreate <x> <y> <w> <h> <bk> : create tile\r\n");
    cmd_puts(ctx, "gui wselect <handle>           : set active tile\r\n");
    cmd_puts(ctx, "gui trenderall                 : render all tiles\r\n");
    cmd_puts(ctx, "gui wclear                     : clear active tile (bk)\r\n");
    cmd_puts(ctx, "gui clear <color>              : clear active tile (color)\r\n");
    cmd_puts(ctx, "gui pixel <x> <y> <color>      : draw pixel\r\n");
    cmd_puts(ctx, "gui fill <x> <y> <w> <h> <color> : fill rect\r\n");
    cmd_puts(ctx, "gui rect <x> <y> <w> <h> <color> : rect outline\r\n");
    cmd_puts(ctx, "gui line <x1> <y1> <x2> <y2> <color> : draw line\r\n");
    cmd_puts(ctx, "gui char <x> <y> <c> <fg> <bg> : draw char\r\n");
    cmd_puts(ctx, "gui string <x> <y> <s> <fg> <bg> : draw string\r\n");
    cmd_puts(ctx, "fs format                      : format W25Q64 with littlefs\r\n");
    cmd_puts(ctx, "fs mount                       : mount littlefs\r\n");
    cmd_puts(ctx, "fs unmount                     : unmount littlefs\r\n");
    cmd_puts(ctx, "fs info                        : show FS block info\r\n");
    cmd_puts(ctx, "ls [path]                      : list directory\r\n");
    cmd_puts(ctx, "cat <path>                     : print file contents\r\n");
    cmd_puts(ctx, "write <path> <text>            : write text to file\r\n");
    cmd_puts(ctx, "append <path> <text>           : append text to file\r\n");
    cmd_puts(ctx, "rm <path>                      : remove file\r\n");
    cmd_puts(ctx, "mkdir <path>                   : create directory\r\n");
    cmd_puts(ctx, "mv <src> <dst>                 : rename/move file\r\n");
    cmd_puts(ctx, "stat <path>                    : show file info\r\n");
    cmd_puts(ctx, "cd [path]                      : change directory\r\n");
    cmd_puts(ctx, "pwd                            : print working directory\r\n");
    return 1;
}

static const cmd_entry_t cmd_table[] = {
    {"help",   cmd_help},
    {"echo",   cmd_echo},
    {"gui",    bridge_gui},
    {"fs",     bridge_fs},
    {"ls",     cmd_ls},
    {"cat",    cmd_cat},
    {"write",  cmd_write},
    {"append", cmd_append},
    {"rm",     cmd_rm},
    {"mkdir",  cmd_mkdir},
    {"mv",     cmd_mv},
    {"stat",   cmd_stat},
    {"cd",     cmd_cd},
    {"pwd",    cmd_pwd},
};
static const int cmd_table_size = sizeof(cmd_table) / sizeof(cmd_table[0]);

static void process_line(cmd_ctx_t* ctx, char* line)
{
    char* argv[CMD_MAX_ARGS];
    int argc = 0;

    char* p = line;
    while (*p) {
        while (*p == ' ') *p++ = '\0';
        if (!*p) break;
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            if (argc >= CMD_MAX_ARGS) break;
            while (*p && *p != '"') p++;
            if (*p) *p++ = '\0';
        } else {
            argv[argc++] = p;
            if (argc >= CMD_MAX_ARGS) break;
            while (*p && *p != ' ') p++;
        }
    }

    if (argc == 0) return;

    for (int i = 0; i < cmd_table_size; i++) {
        if (strcmp(argv[0], cmd_table[i].name) == 0) {
            cmd_table[i].handler(ctx, argc - 1, argv + 1);
            return;
        }
    }

    cmd_puts(ctx, "ERR: unknown cmd '");
    cmd_puts(ctx, argv[0]);
    cmd_puts(ctx, "'\r\n");
}

static int cmd_poll(cmd_ctx_t* ctx)
{
    if (!ctx->prompt_ready) {
        cmd_puts(ctx, "# ");
        ctx->prompt_ready = 1;
    }

    uint8_t buf[16];
    int n = appread(ctx->uart, buf, sizeof(buf), 0x01);
    for (int i = 0; i < n; i++) {
        uint8_t ch = buf[i];
        if (ch == '\r' || ch == '\n') {
            if (ctx->pos == 0) continue;
            ctx->line[ctx->pos] = '\0';
            cmd_puts(ctx, "\r\n");
            process_line(ctx, ctx->line);
            cmd_puts(ctx, "# ");
            ctx->pos = 0;
        } else if (ch == '\b' || ch == 127) {
            if (ctx->pos > 0) {
                ctx->pos--;
                cmd_puts(ctx, "\b \b");
            }
        } else if (ctx->pos < CMD_LINE_BUF_SIZE - 1) {
            ctx->line[ctx->pos++] = (char)ch;
        }
    }
    return 1;
}

static int cmd_open(app_t* app)
{
    cmd_ctx_t* ctx = (cmd_ctx_t*)osmalloc(sizeof(cmd_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(cmd_ctx_t));
    ctx->uart = app->app0;

    ctx->rw_buf = (uint8_t*)osmalloc(512);
    if (!ctx->rw_buf) { osfree(ctx); return -1; }

    ctx->gui = appget("KSCGUI");
    if (ctx->gui) appopen(ctx->gui);

    ctx->fs = appget("littlefs");
    if (ctx->fs) appopen(ctx->fs);

    ctx->cwd[0] = '/';
    ctx->cwd[1] = '\0';

    app->app_data = ctx;
    return 0;
}

static int cmd_close(app_t* app)
{
    cmd_ctx_t* ctx = (cmd_ctx_t*)app->app_data;
    if (ctx) {
        if (ctx->gui) appclose(ctx->gui);
        if (ctx->fs) appclose(ctx->fs);
        if (ctx->rw_buf) osfree(ctx->rw_buf);
        osfree(ctx);
        app->app_data = NULL;
    }
    return 0;
}

static int cmd_ioctl(app_t* app, const char* fmt, va_list ap)
{
    cmd_ctx_t* ctx = (cmd_ctx_t*)app->app_data;
    if (!ctx) return -1;
    if (strcmp(fmt, "poll") == 0) return cmd_poll(ctx);
    return 0;
}

static const papp_ops_t cmd_ops = {
    .open  = cmd_open,
    .close = cmd_close,
    .ioctl = cmd_ioctl,
};

REGISTER_APP_EX("cmd", "0", "1\0uart_serial", &cmd_ops,
    "Serial command terminal: bridges text commands to app ioctl");

#endif
