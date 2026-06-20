#ifndef __dd_h__
#define __dd_h__

#include "KSCconfig.h"
#include <stdarg.h>

/**
 * @brief KSCOS 设备-驱动(Device-Driver)总线框架
 *
 * ============================================================
 * 一、设计思想
 * ============================================================
 * 受 Linux 设备驱动模型启发，结合嵌入式场景简化而来。
 * 核心思想：用 "名字前缀匹配" 将设备(Device)与驱动(Driver)桥接，
 * 设备只需知道自己是什么(名字)，驱动只需声明自己能驱动什么(名字)，
 * 总线(bus)在运行时根据名字前缀自动完成匹配。
 *
 * 此处的"设备"不是寄存器地址或中断号，而是一个逻辑名称——
 * 它代表一个可被驱动服务的外设实例。
 *
 * ============================================================
 * 二、三层架构
 * ============================================================
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │  第1层: pdev_t (设备)                                    │
 * │  作用: 描述硬件外设实例                                   │
 * │  内容: base(类型名) + private(寄存器基址/实例号/父设备)    │
 * │  注册: REGISTER_DEVICE(type, inst, reg) → pdev_table 段  │
 * │  命名: type 如 "tim", "spi", "uart"                      │
 * │                                                         │
 * │  第2层: pdrv_t (驱动)                                    │
 * │  作用: 描述一类驱动程序的能力                             │
 * │  内容: 驱动名 + sysfunc(probe/remove) + ops + 描述       │
 * │  注册: REGISTER_DRIVER(name, sysfunc, ops, desc)         │
 * │  命名: 设备前缀_功能名, 如 tim_clocktask / sys_systime    │
 * │                                                         │
 * │  第3层: dd_t (设备描述符)                                │
 * │  作用: 用户与驱动交互的运行时句柄                          │
 * │  内容: dd_ops(操作集) + callback + driver_data + user_data│
 * │  获取: bus_getdriver(dev_name, dev_no, ops_name) → dd_t*│
 * └─────────────────────────────────────────────────────────┘
 *
 *
 * ============================================================
 * 三、匹配规则（三步前缀匹配）
 * ============================================================
 * bus_getdriver(device_name, dev_no, driver_ops_name) 执行:
 *
 *   第1步: 遍历设备表, 精确匹配 device_name == dev->base->device_name
 *          例: "tim" → 匹配所有 base->device_name=="tim" 的设备
 *          若 dev_no != 0, 额外匹配 dev->private->inst_no
 *          例: "tim" + dev_no=1 → 精确匹配实例号 1
 *
 *   第2步: 遍历驱动表, 找 device_name 前缀匹配的驱动
 *          例: "tim" → 匹配 "tim_clocktask"
 *          按注册顺序返回第一个匹配的驱动
 *
 *   第3步: 匹配 driver_ops_name 与驱动内 ops_name
 *          例: "clock" → 匹配 ops_name="clock"
 *          若 driver_ops_name 为 NULL, 返回第一个匹配的驱动
 *
 *   返回: 新分配的 dd_t* (需用后释放由总线管理, 暂由用户保证生命周期)
 *
 * ★ 关键: device_name 是连接设备与驱动的"钥匙"——
 *        设备名和驱动名必须有相同的前缀, 才能配对.
 *
 *
 * ============================================================
 * 四、命名规范
 * ============================================================
 * 设备名   →  type (单独)           如 "tim", "sys"
 * 实例号   →  inst_no (单独)        如 1, 0
 * 驱动名   →  设备前缀_功能          如 "tim_clocktask"
 * ops_name →  具体功能名             如 "clock", "systime"
 *
 * 驱动名推荐用 "设备前缀_功能" 格式:
 *   - tim_clocktask   (定时器 → 时钟任务)
 *   - tim_pwm         (定时器 → PWM 输出)
 *   - spi1_lcdst7789  (SPI1 → ST7789 屏)
 *   - i2c1_mpu6050    (I2C1 → MPU6050 传感器)
 *
 *
 * ============================================================
 * 五、使用流程
 * ============================================================
 *
 *   // 1. 总线初始化 (一次, 程序入口)
 *   bus_init();
 *
 *   // 2. 获取设备描述符
 *   dd_t* tmr = bus_getdriver("tim", 0, "clock");
 *   //         ↑ 设备前缀  ↑设备号  ↑ ops_name
 *
 *   // 3. 配置参数
 *   tmr->user_data = (void*)(uintptr_t)500;  // 500ms
 *   tmr->callback  = my_tick_callback;
 *
 *   // 4. 打开 (驱动开始工作)
 *   ddopen(tmr);
 *
 *   // 5. 使用 ...
 *   Sleep(3000);
 *
 *   // 6. 关闭
 *   ddclose(tmr);
 *
 *   // 7. 读写 (可选)
 *   uint32_t t;
 *   ddread(tmr, &t, sizeof(t), 0);
 *
 *
 * ============================================================
 * 六、注意事项
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
 *    add_library(...). 参见 timer.c / device.c 在 CMakeLists.txt
 *    中的位置.
 *
 * 3. 设备号指定
 *    bus_getdriver 第三个参数为设备号, 0 表示不指定(前缀匹配第一个),
 *    非 0 则精确匹配该号对应的设备实例. 例如:
 *      bus_getdriver("tim", 1, "clock") → 精确匹配 tim1
 *      bus_getdriver("tim", 0, "clock") → 匹配第一个 tim 前缀设备
 *
 * 4. dd_t 生命周期
 *    bus_getdriver 每次返回新分配的 dd_t, 目前总线不负责回收.
 *    简单场景下用户可直接使用无需 free; 高频创建/销毁场景需
 *    用户自行管理.
 *
 * 5. callback 与 driver_data / user_data 的约定
 *    - callback: 由驱动在特定事件(如定时器到期)时调用, 参数为
 *                user_data
 *    - driver_data: 由驱动的 probe 预分配, close 释放, 用户不应直接修改
 *    - user_data: 由用户在 ddopen 前配置, 驱动在 open 时读取
 
 * 6. probe 回调
 *    bus_getdriver 匹配成功后自动调用 drv->sysfunc->probe(dd),
 *    用于预分配 driver_data。probe 为 NULL 则跳过。
 *    
 * 7. dd_t.dev 指针
 *    匹配到的 pdev_t 通过 dd->dev 传递给驱动,
 *    驱动可访问 dev->private->device_register / inst_no 等设备信息。
 *
 * ============================================================
 * 七、完整注册示例
 * ============================================================
 *
 *   // === 设备注册 (device.c) ===
 *   REGISTER_DEVICE("tim", 1, TIM2_BASE);
 *
 *   // === 驱动注册 (timer.c) ===
 *   static int timer_probe(dd_t* dd) {
 *       timer_ctx_t* ctx = osmalloc(sizeof(timer_ctx_t));
 *       if(!ctx) return -1;
 *       ctx->reg_base = dd->dev->private->device_register;
 *       dd->driver_data = ctx;
 *       return 0;
 *   }
 *   static const pdrv_sysfunc_t timer_sysfunc = {
 *       .probe = timer_probe,
 *   };
 *   static pdrv_ops_t timer_ops = {
 *       .ops_name = "clock",
 *       .open = timer_open,
 *       .close = timer_close,
 *       .read = timer_read,
 *   };
 *   REGISTER_DRIVER("tim_clocktask", &timer_sysfunc, &timer_ops, "TIM clock");
 *
 *   // === 用户使用 ===
 *   dd_t* tmr = bus_getdriver("tim", 0, "clock");
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
 * @brief 设备基类: 仅包含设备名称
 * @note  命名格式 type+实例号, 如 "tim1", "sys0", "spi2"
 */
typedef struct pdev_base_t{
    const char* device_name;
} pdev_base_t;

typedef struct pdev_private_t{
    struct pdev_t* parent;
    uint32_t device_register;
    uint8_t inst_no;
} pdev_private_t;

/**
 * @brief 设备完整结构
 * @note  2 个指针，8 字节 (32-bit)，对齐 8
 */
typedef struct __attribute__((aligned(8))) pdev_t {
    const pdev_base_t* base;
    const pdev_private_t* private;
}pdev_t;

/**
 * @brief 总线结构
 * @note  保存设备表与驱动表的运行时副本
 */
typedef struct bus_t bus_t;
typedef struct bus_t{
    pdev_t* dev_table;   // 设备表 (从 pdev_table 段拷贝)
    uint32_t dev_count;
    pdrv_t* drv_table;   // 驱动表 (从 pdrv_table 段拷贝)
    uint32_t drv_count;
} bus_t;

/**
 * @brief 驱动基类: 仅包含驱动名称
 * @note  命名格式 设备前缀_功能, 如 "tim_clocktask", "sys_systime"
 *        驱动名必须与设备名有相同前缀, 才能被总线匹配
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
 * @note  基类 + 指向单个驱动操作集的指针
 *        sizeof=16（2 的幂），与 16 字节对齐一致，
 *        确保链接器在 section 中按 sizeof 为单位连续排布
 */
typedef struct __attribute__((aligned(16))) pdrv_t {
    const pdrv_base_t* base;
    const pdrv_sysfunc_t* sysfunc;
    const pdrv_ops_t* ops;
    const char* driver_desc;
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
 *        dev         — 匹配到的 pdev_t, 驱动访问设备寄存器/实例号
 *        driver      — 匹配到的 pdrv_t
 *        dd_ops      — 指向某个 pdrv_ops_t, 决定 open/close/read/write/ioctl
 *        callback    — 异步回调 (如定时器到期), 参数为 user_data
 *        driver_data — 由 probe 预分配, close 释放
 *        user_data   — 用户自定义参数, 在 ddopen 前设置
 */
typedef struct dd_t{
    const pdev_t* dev;
    const pdrv_t* driver;
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
 * @note  每个操作集有独立 ops_name, 同一驱动可包含多个操作集
 *        (例如一个驱动同时提供 clock 和 pwm 两种操作)
 */
typedef struct pdrv_ops_t{
    const char* ops_name;       // 操作集名称, 用于 bus_getdriver 匹配
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
 * @param type  设备类型名 (如 "tim", "spi", "uart")
 * @param inst  实例号 (0=无实例)
 * @param reg   寄存器基地址 (无硬件传 0)
 * @note  展开为 pdev_base_t + pdev_private_t + pdev_t 三个静态对象，
 *        编入 "pdev_table" 链接段
 */
#define _REGISTER_DEVICE_IMPL(type, inst, reg, ctr) \
    static const pdev_base_t _CONCAT(_B_, ctr) = {type}; \
    static const pdev_private_t _CONCAT(_P_, ctr) = { \
        .parent = NULL, \
        .device_register = (uint32_t)(reg), \
        .inst_no = (uint8_t)(inst) \
    }; \
    static const pdev_t _CONCAT(_D_, ctr) \
    __attribute__((section("pdev_table"), used)) = { \
        &_CONCAT(_B_, ctr), \
        &_CONCAT(_P_, ctr) \
    };

#define REGISTER_DEVICE(type, inst, reg) \
    _REGISTER_DEVICE_IMPL(type, inst, reg, __COUNTER__)

#define _REGISTER_DRIVER_IMPL(drvname, sysfunc_ptr, ops_ptr, desc, ctr) \
    static const pdrv_base_t _CONCAT(_DRV_BASE_, ctr) = {drvname}; \
    static const pdrv_t _CONCAT(_DRV_, ctr) \
    __attribute__((section("pdrv_table"), used)) = { \
        &_CONCAT(_DRV_BASE_, ctr), \
        sysfunc_ptr, \
        ops_ptr, \
        desc \
    };

#define REGISTER_DRIVER(drvname, sysfunc_ptr, ops_ptr, desc) \
    _REGISTER_DRIVER_IMPL(drvname, sysfunc_ptr, ops_ptr, desc, __COUNTER__)

extern const pdev_t __start_pdev_table[];
extern const pdev_t __stop_pdev_table[];

extern const pdrv_t __start_pdrv_table[];
extern const pdrv_t __stop_pdrv_table[];
#else // __USE_STM32__

#define _CONCAT_IMPL(a, b) a##b
#define _CONCAT(a, b)      _CONCAT_IMPL(a, b)

#define _REGISTER_DEVICE_IMPL(type, inst, reg, ctr) \
    static const pdev_base_t _CONCAT(_B_, ctr) = {type}; \
    static const pdev_private_t _CONCAT(_P_, ctr) = { \
        .parent = NULL, \
        .device_register = (uint32_t)(reg), \
        .inst_no = (uint8_t)(inst) \
    }; \
    static const pdev_t _CONCAT(_D_, ctr) \
    __attribute__((section("pdev_table"), used)) = { \
        &_CONCAT(_B_, ctr), \
        &_CONCAT(_P_, ctr) \
    };

#define REGISTER_DEVICE(type, inst, reg) \
    _REGISTER_DEVICE_IMPL(type, inst, reg, __COUNTER__)

#define _REGISTER_DRIVER_IMPL(drvname, sysfunc_ptr, ops_ptr, desc, ctr) \
    static const pdrv_base_t _CONCAT(_DRV_BASE_, ctr) = {drvname}; \
    static const pdrv_t _CONCAT(_DRV_, ctr) \
    __attribute__((section("pdrv_table"), used)) = { \
        &_CONCAT(_DRV_BASE_, ctr), \
        sysfunc_ptr, \
        ops_ptr, \
        desc \
    };

#define REGISTER_DRIVER(drvname, sysfunc_ptr, ops_ptr, desc) \
    _REGISTER_DRIVER_IMPL(drvname, sysfunc_ptr, ops_ptr, desc, __COUNTER__)

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
dd_t* bus_getdriver(char* device_name, uint8_t dev_no, char* driver_ops_name);

int ddopen(dd_t* dd);
int ddclose(dd_t* dd);
int ddread(dd_t* dd, void* data, uint32_t size, uint32_t mode);
int ddwrite(dd_t* dd, void* data, uint32_t size, uint32_t mode);

int ddioctl(dd_t* dd, const char* fmt, ...);


#endif // __dd_h__
