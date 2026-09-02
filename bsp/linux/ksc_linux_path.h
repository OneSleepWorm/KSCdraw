/**
 * @file    ksc_linux_path.h
 * @note    Linux BSP — KSCOS/.data/ 运行时数据目录定位 (POSIX)
 *
 * ============================================================
 * 定位策略 (依次降级)
 * ============================================================
 *   1. KSCOS_DATA_DIR 环境变量 — 显式覆盖, 优先级最高
 *   2. readlink("/proc/self/exe") 逐级上溯, 找工程根
 *      判定依据: 该目录下存在 inc/KSCconfig.h
 *   3. 相对路径 ".data/" — 兜底 (CWD 恰好在 KSCOS/ 时有效)
 *
 * ⚠️ 不能像 PC (MinGW) 版那样"固定上溯两级": MinGW 产物在
 *    KSCOS/build_debug/KSCOS.exe (深 1 层), 而 Linux 预设产物在
 *    KSCOS/build/linux-debug/KSCOS (深 2 层)。固定层数会随 binaryDir
 *    变动而失效, 故改为按工程根标志物逐级上溯。
 *
 * ⚠️ 本目录含 flash.bin (littlefs 持久化镜像), 严禁删除或清空。
 *    本头文件只做路径拼接与目录创建, 绝不删除已有文件。
 */

#ifndef KSC_LINUX_PATH_H
#define KSC_LINUX_PATH_H

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

/* .data 目录权限: u=rwx,g=rwx,o=rx (与常见源码树权限一致) */
#define KSC_DATA_DIR_MODE   0775
#define KSC_PATH_MAX        512

/**
 * @brief 拼接 KSCOS/.data/<basename> 的绝对路径
 * @param out      输出缓冲
 * @param sz       缓冲大小 (建议 KSC_PATH_MAX)
 * @param basename 文件名, 如 "flash.bin" / "stdin1.txt"
 * @return 0=成功, -1=缓冲不足
 */
static inline int ksc_data_path(char* out, size_t sz, const char* basename)
{
    const char* env = getenv("KSCOS_DATA_DIR");

    if (env && *env) {
        int n = snprintf(out, sz, "%s/%s", env, basename);
        return (n > 0 && (size_t)n < sz) ? 0 : -1;
    }

    /* /proc/self/exe → 逐级上溯找工程根 (含 inc/KSCconfig.h 的目录) */
    char dir[KSC_PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", dir, sizeof(dir) - 1);
    if (len > 0) {
        dir[len] = '\0';
        for (int i = 0; i < 8; i++) {
            char* sep = strrchr(dir, '/');
            if (!sep || sep == dir) break;   /* 已到根目录, 无更多层级 */
            *sep = '\0';

            char probe[KSC_PATH_MAX + 32];
            snprintf(probe, sizeof(probe), "%s/inc/KSCconfig.h", dir);
            struct stat st;
            if (stat(probe, &st) == 0) {
                int n = snprintf(out, sz, "%s/.data/%s", dir, basename);
                return (n > 0 && (size_t)n < sz) ? 0 : -1;
            }
        }
    }

    int n = snprintf(out, sz, ".data/%s", basename);
    return (n > 0 && (size_t)n < sz) ? 0 : -1;
}

/**
 * @brief 确保 KSCOS/.data/ 目录存在 (已存在则不动)
 * @param file_path ksc_data_path() 产出的文件路径
 *
 * 只创建目录, 绝不删除/清空其中已有文件 (尤其 flash.bin)。
 */
static inline void ksc_data_mkdir(const char* file_path)
{
    char dir[KSC_PATH_MAX];
    size_t n = strlen(file_path);
    if (n == 0 || n >= sizeof(dir)) return;

    memcpy(dir, file_path, n + 1);
    char* sep = strrchr(dir, '/');
    if (!sep || sep == dir) return;

    *sep = '\0';
    struct stat st;
    if (stat(dir, &st) != 0)
        mkdir(dir, KSC_DATA_DIR_MODE);
}

#endif /* KSC_LINUX_PATH_H */
