#ifndef __dd_h__
#define __dd_h__

#include "KSCconfig.h"
#include <stdarg.h>

/**
 * @brief KSCOS 设备-驱动(Device-Driver)总线框架 v2
 *
 * ============================================================
 * 一、设计思想
 * ============================================================
 * 三层解耦：设备(pdev_t)只描述硬件实例，驱动(pdrv_t)只声明能力，
 * 总线在运行时根据 device_dependency 自动匹配设备指针填入 dd_t.devX 槽。
 *
 * 设备名即完整名（如 "spi2", "tim3", "gpioa"），无单独的实例号。
 * 驱动通过 device_dependency 字符串声明需要哪些设备（支持通配符前缀匹配）。
 * 用户通过 bus_rebind_dev 可在运行时重绑定 dd_t 到具体设备。
 *
 * ============================================================
 * 二、三层架构
 * ============================================================
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │  第1层: pdev_t (设备)                                    │
 * │  作用: 描述硬件外设实例                                   │
 * │  内容: base(设备名) + private(寄存器基址)                  │
 * │  注册: REGISTER_DEVICE(name, reg) → pdev_table 段        │
 * │  命名: "tim3", "spi2", "gpioa" 完整名                     │
 * │                                                         │
 * │  第2层: pdrv_t (驱动)                                    │
 * │  作用: 描述一类驱动程序的能力                             │
 * │  内容: 驱动名 + device_dependency + sysfunc + ops         │
 * │  注册: REGISTER_DRIVER(name, dep, sysfunc, ops, desc)    │
 * │  命名: 独立名如 "tim_clocktask", "spi_master"             │
 * │                                                         │
 * │  第3层: dd_t (设备描述符)                                │
 * │  作用: 用户与驱动交互的运行时句柄                          │
 * │  内容: driver + dev0..dev3 + dd_ops + callback + data    │
 * │  获取: bus_getdriver(driver_name, ops_name) → dd_t*     │
 * └─────────────────────────────────────────────────────────┘
 *
 *
 * ============================================================
 * 三、匹配规则（已移除 ops_name 匹配，驱动名唯一确定）
 * ============================================================
 * bus_getdriver(driver_name) 执行:
 *
 *   第1步: 遍历驱动表, 精确匹配 driver_name == drv->base->driver_name
 *
 *   第2步: 解析 device_dependency 字符串:
 *          格式: "N\0pat1\0pat2\0..."
 *          N 为数量, pat 为设备名或前缀通配符
 *          例: "1\0tim"  → 匹配第一个 device_name 以 "tim" 开头的设备
 *               "3\0apb\0spi\0gpio" → 匹配三个设备, 填 dev0/dev1/dev2
 *
 *   第3步: 匹配到的设备指针填入 dd->dev0..dev3
 *
 *   第4步: 若 driver 定义了 sysfunc->probe, 调用 probe(dd)
 *
 *   返回: 新分配的 dd_t* (用户保证生命周期)
 *
 * ★ bus_rebind_dev(dd, slot, dev_name) 可重绑定 dev 指针到其他设备
 *
 *
 * ============================================================
 * 四、使用流程
 * ============================================================
 *
 *   // 1. 总线初始化
 *   bus_init();
 *
 *   // 2. 获取驱动描述符（自动匹配第一个可用设备）
 *   dd_t* tmr = bus_getdriver("tim_clocktask");
 *
 *   // 2b. 如需指定特定设备实例，用 rebind
 *   bus_rebind_dev(tmr, 0, "tim3");
 *
 *   // 3. 配置参数
 *   tmr->user_data = (void*)(uintptr_t)500;
 *   tmr->callback  = my_tick_callback;
 *
 *   // 4. 打开
 *   ddopen(tmr);
 *
 *   // 5. 使用 ...
 *
 *   // 6. 关闭
 *   ddclose(tmr);
 *
 *
 * ============================================================
 * 五、注意事项
 * ============================================================
 *
 * 1. pdrv_t / pdev_t 对齐
 *    pdrv_t sizeof(=16) aligned(16)，4 指针。
 *    pdev_t sizeof(=8)  aligned(8)， 2 指针。
 *    均为 2 的幂，section 按 sizeof 连续排布，stride = sizeof。
 *
 * 2. 静态库链接丢弃问题
 *    若一个 .c 文件只包含 REGISTER_DEVICE / REGISTER_DRIVER 而
 *    没有任何被其他代码引用的符号, 则该文件所在的静态库目标文件
 *    (.o) 会被链接器丢弃, 导致注册不生效.
 *    解决: 此类文件必须直接加入 add_executable(...) 而不是
 *    add_library(...).
 *
 * 3. dd_t 生命周期
 *    bus_getdriver 每次返回新分配的 dd_t, 目前总线不负责回收.
 *    简单场景下用户可直接使用无需 free; 高频创建/销毁场景需
 *    用户自行管理.
 *
 * 4. callback 与 driver_data / user_data 的约定
 *    - callback: 由驱动在特定事件时调用, 参数为 user_data
 *    - driver_data: 由驱动的 probe 预分配, close 释放, 用户不应直接修改
 *    - user_data: 由用户在 ddopen 前配置
 *
 * 5. probe 回调
 *    bus_getdriver 匹配成功后自动调用 drv->sysfunc->probe(dd),
 *    用于预分配 driver_data。probe 为 NULL 则跳过。
 *
 * 6. bus_rebind_dev 仅交换 dev 指针，不调 remove/probe。
 *    驱动若缓存了寄存器基址, 需自行处理重绑定一致性。
 *
 * ============================================================
 * 六、完整注册示例
 * ============================================================
 *
 *   // === 设备注册 (device.c) ===
 *   REGISTER_DEVICE("tim3", 0x40000400);
 *
 *   // === 驱动注册 (timer.c) ===
 *   static int timer_probe(dd_t* dd) {
 *       timer_ctx_t* ctx = osmalloc(sizeof(timer_ctx_t));
 *       if(!ctx) return -1;
 *       ctx->period_ms = 1000;
 *       dd->driver_data = ctx;
 *       return 0;
 *   }
 *   static const pdrv_sysfunc_t timer_sysfunc = {
 *       .probe = timer_probe,
 *   };
 *   static pdrv_ops_t timer_ops = {
 *       .open = timer_open,
 *       .close = timer_close,
 *       .read = timer_read,
 *   };
 *   REGISTER_DRIVER("tim_clocktask", "1\0tim", &timer_sysfunc, &timer_ops, "TIM clock");
 *
 *   // === 用户使用 ===
 *   dd_t* tmr = bus_getdriver("tim_clocktask");
 *   bus_rebind_dev(tmr, 0, "tim3");
 *   tmr->user_data = (void*)(uintptr_t)500;
 *   tmr->callback = my_callback;
 *   ddopen(tmr);
 */

#define container_of(ptr, type, member) ({                      \
    const typeof(((type *)0)->member) * __mptr = (ptr);     \
    (type *)((char *)__mptr - offsetof(type, member)); })

typedef struct pdev_t pdev_t;
typedef struct pdrv_t pdrv_t;

/**
 * @brief 设备基类: 包含完整设备名
 * @note  命名如 "tim3", "spi2", "gpioa", "sys"
 */
typedef struct pdev_base_t{
    const char* device_name;
} pdev_base_t;

typedef struct pdev_private_t{
    uint32_t device_register;
    uint32_t rcc_reg_addr;
    uint32_t rcc_bit;
} pdev_private_t;

/**
 * @brief 设备完整结构
 * @note  2 个指针，8 字节 (32-bit)，对齐 8
 *        base 和 private 指向 const 静态数据，不可变
 */
typedef struct __attribute__((aligned(8))) pdev_t {
    const pdev_base_t* base;
    const pdev_private_t* private;
}pdev_t;

/**
 * @brief 总线结构
 * @note  保存设备表与驱动表的运行时副本
 *        无设备占用追踪，匹配到就给
 */
typedef struct bus_t bus_t;
typedef struct bus_t{
    pdev_t* dev_table;
    uint32_t dev_count;
    pdrv_t* drv_table;
    uint32_t drv_count;
} bus_t;

/**
 * @brief 驱动基类: 仅包含驱动名称
 * @note  命名独立，不依赖设备名前缀，如 "tim_clocktask", "spi_master"
 */
typedef struct pdrv_base_t{
    const char* driver_name;
} pdrv_base_t;

typedef struct pdrv_ops_t pdrv_ops_t;
typedef struct dd_t dd_t;
typedef int (*driver_probe_func)(dd_t* dd);
typedef int (*driver_remove_func)(dd_t* dd);

/**
 * @brief 驱动系统函数
 * @note  用于设备初始化与注销, 由总线调用，一般实现为预堆分配driver_data空间与释放
 */
typedef struct pdrv_sysfunc_t{
    driver_probe_func probe;
    driver_remove_func remove;
} pdrv_sysfunc_t;

/**
 * @brief 驱动完整结构
 * @param base  驱动名
 * @param device_dependency  设备依赖关系
 * @param sysfunc  驱动系统函数指针
 * @param ops  驱动操作集指针
 * @note  device_dependency 格式:
 *        "N\0pat1\0pat2\0pat3"
 *        N 为数量(ASCII数字)，pat 为设备完整名或前缀通配符
 *        例如 "3\0apb1\0spi2\0gpioa" 精确匹配三个设备
 *              "1\0tim" 通配所有以 "tim" 开头的设备(tim1~tim4)
 *        dep 为 NULL 或 "0" 表示无硬件依赖
 *        dev0~dev3 最多 4 个槽位
 */
typedef struct __attribute__((aligned(16))) pdrv_t {
    const pdrv_base_t* base;
    const char* device_dependency;
    const pdrv_sysfunc_t* sysfunc;
    const pdrv_ops_t* ops;
} pdrv_t;

/**
 * @brief 通用回调函数类型
 * @param data  用户数据指针 (对应 dd_t.user_data)
 */
typedef void* (*void_func_t)(void* data);

/**
 * @brief 设备描述符 ( Device Descriptor )
 * @note  用户与驱动交互的核心句柄, 由 bus_getdriver 创建并返回
 * 
 *        driver      — 匹配到的 pdrv_t
 *        dev0~dev3   — 总线根据 device_dependency 匹配的设备指针
 *        dd_ops      — 指向某个 pdrv_ops_t, 决定 open/close/read/write/ioctl
 *        callback    — 异步回调 (如定时器到期), 参数为 user_data
 *        driver_data — 由 probe 预分配, close 释放
 *        user_data   — 用户自定义参数, 在 ddopen 前设置
 */
typedef struct dd_t{
    const pdrv_t* driver;
    const pdev_t* dev0;
    const pdev_t* dev1;
    const pdev_t* dev2;
    const pdev_t* dev3;
    const pdrv_ops_t* dd_ops;
    void_func_t callback;
    void* driver_data;
    void* user_data;
} dd_t;

typedef int (*driver_open_func)(struct dd_t* dd);
typedef int (*driver_close_func)(struct dd_t* dd);
typedef int (*driver_read_func)(struct dd_t* dd, void* data, uint32_t count, uint32_t mode);
typedef int (*driver_write_func)(struct dd_t* dd, void* data, uint32_t count, uint32_t mode);
typedef int (*driver_ioctl_func)(struct dd_t* dd, const char* fmt, va_list ap);

/**
 * @brief 驱动操作集
 */
typedef struct pdrv_ops_t{
    driver_open_func open;
    driver_close_func close;
    driver_read_func read;
    driver_write_func write;
    driver_ioctl_func ioctl;
} pdrv_ops_t;


#if __USE_PC__

#define _CONCAT_IMPL(a, b) a##b
#define _CONCAT(a, b)      _CONCAT_IMPL(a, b)

/**
 * @brief 静态注册设备
 * @param name  设备完整名 (如 "tim3", "spi2", "gpioa")
 * @param reg   寄存器基地址 (无硬件传 0)
 * @note  展开为 pdev_base_t + pdev_private_t + pdev_t 三个静态对象，
 *        编入 "pdev_table" 链接段
 */
#define _REGISTER_DEVICE_IMPL(name, reg, rcca, rccb, ctr) \
    static const pdev_base_t _CONCAT(_B_, ctr) = {name}; \
    static const pdev_private_t _CONCAT(_P_, ctr) = {(uint32_t)(reg), (uint32_t)(rcca), (uint32_t)(rccb)}; \
    static const pdev_t _CONCAT(_D_, ctr) \
    __attribute__((section("pdev_table"), used)) = { \
        &_CONCAT(_B_, ctr), \
        &_CONCAT(_P_, ctr) \
    };

#define REGISTER_DEVICE(name, reg, rcc_addr, rcc_bit) \
    _REGISTER_DEVICE_IMPL(name, reg, rcc_addr, rcc_bit, __COUNTER__)

/**
 * @brief 静态注册驱动
 * @param drvname       驱动名 (如 "tim_clocktask")
 * @param dep           device_dependency 字符串 (如 "1\0tim")
 * @param sysfunc_ptr   指向 pdrv_sysfunc_t 的指针
 * @param ops_ptr       指向 pdrv_ops_t 的指针
 * @param desc          描述字符串
 * @note  编入 "pdrv_table" 链接段
 *        字段顺序: base, dep, sysfunc, ops
 */
#define _REGISTER_DRIVER_IMPL(drvname, dep, sysfunc_ptr, ops_ptr, desc, ctr) \
    static const pdrv_base_t _CONCAT(_DRV_BASE_, ctr) = {drvname}; \
    static const pdrv_t _CONCAT(_DRV_, ctr) \
    __attribute__((section("pdrv_table"), used)) = { \
        &_CONCAT(_DRV_BASE_, ctr), \
        dep, \
        sysfunc_ptr, \
        ops_ptr \
    };

#define REGISTER_DRIVER(drvname, dep, sysfunc_ptr, ops_ptr, desc) \
    _REGISTER_DRIVER_IMPL(drvname, dep, sysfunc_ptr, ops_ptr, desc, __COUNTER__)

extern const pdev_t __start_pdev_table[];
extern const pdev_t __stop_pdev_table[];

extern const pdrv_t __start_pdrv_table[];
extern const pdrv_t __stop_pdrv_table[];
#else // __USE_STM32__

#define _CONCAT_IMPL(a, b) a##b
#define _CONCAT(a, b)      _CONCAT_IMPL(a, b)

#define _REGISTER_DEVICE_IMPL(name, reg, rcca, rccb, ctr) \
    static const pdev_base_t _CONCAT(_B_, ctr) = {name}; \
    static const pdev_private_t _CONCAT(_P_, ctr) = {(uint32_t)(reg), (uint32_t)(rcca), (uint32_t)(rccb)}; \
    static const pdev_t _CONCAT(_D_, ctr) \
    __attribute__((section("pdev_table"), used)) = { \
        &_CONCAT(_B_, ctr), \
        &_CONCAT(_P_, ctr) \
    };

#define REGISTER_DEVICE(name, reg, rcc_addr, rcc_bit) \
    _REGISTER_DEVICE_IMPL(name, reg, rcc_addr, rcc_bit, __COUNTER__)

#define _REGISTER_DRIVER_IMPL(drvname, dep, sysfunc_ptr, ops_ptr, desc, ctr) \
    static const pdrv_base_t _CONCAT(_DRV_BASE_, ctr) = {drvname}; \
    static const pdrv_t _CONCAT(_DRV_, ctr) \
    __attribute__((section("pdrv_table"), used)) = { \
        &_CONCAT(_DRV_BASE_, ctr), \
        dep, \
        sysfunc_ptr, \
        ops_ptr \
    };

#define REGISTER_DRIVER(drvname, dep, sysfunc_ptr, ops_ptr, desc) \
    _REGISTER_DRIVER_IMPL(drvname, dep, sysfunc_ptr, ops_ptr, desc, __COUNTER__)

extern const pdev_t __start_pdev_table[];
extern const pdev_t __stop_pdev_table[];

extern const pdrv_t __start_pdrv_table[];
extern const pdrv_t __stop_pdrv_table[];

_Static_assert(sizeof(pdev_t) == 8, "pdev_t must be 8 bytes (2 pointers)");
_Static_assert(__alignof__(pdev_t) == 8, "pdev_t alignment must be 8");
_Static_assert(sizeof(pdrv_t) == 16, "pdrv_t must be 16 bytes (2 pointers + padding)");
_Static_assert(__alignof__(pdrv_t) == 16, "pdrv_t alignment must be 16");
#endif // __USE_STM32__
int null_func(struct dd_t* dev);  
int null_rw_func(struct dd_t* dev, void* data, uint32_t size, uint32_t mode);
int null_ioctl_func(struct dd_t* dev, const char* fmt, va_list ap);
void* null_callback(void* data);

#define OPEN_NULL_FUNC null_func
#define CLOSE_NULL_FUNC null_func
#define READ_NULL_FUNC null_rw_func
#define WRITE_NULL_FUNC null_rw_func
#define IOCTL_NULL_FUNC null_ioctl_func
#define CALLBACK_NULL_FUNC null_callback
#define DRIVER_DATA_NULL_FUNC NULL
#define USER_DATA_NULL_FUNC NULL

int bus_init(void);
dd_t* bus_getdriver(const char* driver_name);
int bus_rebind_dev(dd_t* dd, int slot, const char* dev_name);

int ddopen(dd_t* dd);
int ddclose(dd_t* dd);
int ddread(dd_t* dd, void* data, uint32_t size, uint32_t mode);
int ddwrite(dd_t* dd, void* data, uint32_t size, uint32_t mode);

int ddioctl(dd_t* dd, const char* fmt, ...);


#endif // __dd_h__
