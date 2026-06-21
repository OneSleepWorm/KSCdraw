////////////////////////////////////////////////////////////////
/**
 * @antuor   OneSleepWorm(一只瞌睡虫)
 * @brief    定义设备结构体+驱动结构体+dd设备描述符
 * 
 */
////////////////////////////////////////////////////////////////

/** 
 * @brief 设备驱动注册示例（仅供参考，实际注册见各驱动文件及 dd.h 顶部注释）
 */

#include "../inc/dd.h"
#include "../inc/KSCOSsystem.h"
#include "string.h"

__volatile static bus_t KSC_bus={0};
/**
 * @brief 初始化总线
 * @note 初始化总线时，需要分配设备表内存，初始化设备表，初始化驱动表，拷贝设备数据而非指针到总线。
 * @note 会分配sizeof(pdev_t) * 设备数量的内存给设备表。
 * @return int 0 成功 -1 失败
 */
int bus_init(void)
{
    // 初始化总线
    //分配设备表内存
    KSC_bus.dev_table = oscalloc(((const char*)__stop_pdev_table - (const char*)__start_pdev_table) / sizeof(pdev_t), sizeof(pdev_t));
    //检查内存是否成功分配
    if(KSC_bus.dev_table == NULL)
    {
        return -1;
    }
    //初始化设备表
    KSC_bus.dev_count = ((const char*)__stop_pdev_table - (const char*)__start_pdev_table) / sizeof(pdev_t);
    //初始化驱动表
    size_t drv_section_bytes = (const char*)__stop_pdrv_table - (const char*)__start_pdrv_table;
    KSC_bus.drv_count = drv_section_bytes / sizeof(pdrv_t);

    //拷贝设备数据到总线
    memcpy(KSC_bus.dev_table, __start_pdev_table, KSC_bus.dev_count * sizeof(pdev_t));

    //拷贝驱动数据到总线（sizeof(pdrv_t)=16 为 2 的幂，linker 按 sizeof 连续排布）
    KSC_bus.drv_table = oscalloc(KSC_bus.drv_count, sizeof(pdrv_t));
    if(KSC_bus.drv_table == NULL) return -1;
    memcpy(KSC_bus.drv_table, __start_pdrv_table, KSC_bus.drv_count * sizeof(pdrv_t));

    // //调试：打印数量
    // printf("dev_count: %d\n", KSC_bus.dev_count);
    // printf("drv_count: %d\n", KSC_bus.drv_count);
    // //调试：打印设备名
    // for(const pdev_t* dev = KSC_bus.dev_table; dev < KSC_bus.dev_table + KSC_bus.dev_count; dev++)
    // {
    //     printf("dev_name: %s, inst_no: %d\n", dev->base->device_name, dev->private->inst_no);
    // }
    // //调试：打印驱动名
    // for(const pdrv_t* drv = KSC_bus.drv_table; drv < KSC_bus.drv_table + KSC_bus.drv_count; drv++)
    // {
    //     printf("drv_name: %s\n", drv->base->driver_name);
    // }
    //调试结束
    return 0;
}
/**
 * @brief 解析 device_dependency 字符串，填充 dev 槽
 * @return 成功解析的设备数量，-1 失败
 */
static int resolve_deps(const char* dep_str, const pdev_t* slots[4])
{
    if (!dep_str || !dep_str[0] || dep_str[0] == '0')
        return 0;

    int count = dep_str[0] - '0';
    if (count <= 0 || count > 4) return -1;

    const char* p = dep_str + 2;
    for (int i = 0; i < count; i++) {
        size_t plen = strlen(p);
        int found = 0;
        for (uint32_t j = 0; j < KSC_bus.dev_count; j++) {
            const pdev_t* dev = &KSC_bus.dev_table[j];
            if (strncmp(p, dev->base->device_name, plen) == 0) {
                slots[i] = dev;
                found = 1;
                break;
            }
        }
        if (!found) return -1;
        p += plen + 1;
    }
    return count;
}

/**
 * @brief 获取驱动描述符
 * 
 * @param driver_name 驱动名，精确匹配 drv->base->driver_name
 * @return dd_t*      设备驱动描述符指针
 * @note 匹配成功后自动解析 device_dependency 填入 dev0~dev3
 *       并调用 probe
 */
dd_t* bus_getdriver(const char* driver_name)
{
    if (!driver_name) return NULL;

    for (const pdrv_t* drv = KSC_bus.drv_table; drv < KSC_bus.drv_table + KSC_bus.drv_count; drv++)
    {
        if (strcmp(driver_name, drv->base->driver_name) != 0)
            continue;

        dd_t* dd = osmalloc(sizeof(dd_t));
        if (!dd) return NULL;

        dd->driver   = drv;
        dd->dd_ops   = drv->ops;
        dd->dev0     = NULL;
        dd->dev1     = NULL;
        dd->dev2     = NULL;
        dd->dev3     = NULL;
        dd->callback = CALLBACK_NULL_FUNC;
        dd->driver_data = NULL;
        dd->user_data   = NULL;

        const pdev_t* slots[4] = {NULL, NULL, NULL, NULL};
        if (resolve_deps(drv->device_dependency, slots) < 0) {
            osfree(dd);
            return NULL;
        }
        dd->dev0 = slots[0];
        dd->dev1 = slots[1];
        dd->dev2 = slots[2];
        dd->dev3 = slots[3];

        for (int i = 0; i < 4; i++) {
            const pdev_t* d = slots[i];
            if (d && d->private->rcc_bit) {
                *(volatile uint32_t*)d->private->rcc_reg_addr |= d->private->rcc_bit;
                (void)*(volatile uint32_t*)d->private->rcc_reg_addr;
            }
        }

        dd->driver_data = oscalloc(1, 32);
        if (!dd->driver_data) {
            osfree(dd);
            return NULL;
        }

        return dd;
    }

    return NULL;
}

/**
 * @brief 重绑定 dd_t 的 dev 槽到指定设备
 * @param dd       设备描述符
 * @param slot     槽位 (0~3)
 * @param dev_name 目标设备完整名
 * @return int     0 成功，-1 失败（设备不存在或槽位无效）
 * @note 仅交换 dev 指针，不调 remove/probe
 */
int bus_rebind_dev(dd_t* dd, int slot, const char* dev_name)
{
    if (!dd || slot < 0 || slot > 3 || !dev_name)
        return -1;

    const pdev_t* dev = NULL;
    for (uint32_t i = 0; i < KSC_bus.dev_count; i++) {
        if (strcmp(dev_name, KSC_bus.dev_table[i].base->device_name) == 0) {
            dev = &KSC_bus.dev_table[i];
            break;
        }
    }
    if (!dev) return -1;

    const pdev_t* old_dev = NULL;
    switch (slot) {
        case 0: old_dev = dd->dev0; dd->dev0 = dev; break;
        case 1: old_dev = dd->dev1; dd->dev1 = dev; break;
        case 2: old_dev = dd->dev2; dd->dev2 = dev; break;
        case 3: old_dev = dd->dev3; dd->dev3 = dev; break;
    }
    if (old_dev && old_dev->private->rcc_bit) {
        *(volatile uint32_t*)old_dev->private->rcc_reg_addr &= ~old_dev->private->rcc_bit;
    }
    if (dev->private->rcc_bit) {
        *(volatile uint32_t*)dev->private->rcc_reg_addr |= dev->private->rcc_bit;
        (void)*(volatile uint32_t*)dev->private->rcc_reg_addr;
    }
    return 0;
}

int null_func(struct dd_t* dev){return 0;}
int null_rw_func(struct dd_t* dev, void* data, uint32_t size, uint32_t mode){return 0;}
int null_ioctl_func(struct dd_t* dev, const char* fmt, va_list ap){(void)dev;(void)fmt;(void)ap;return 0;}
void* null_callback(void* data){return NULL;}

int ddopen(dd_t* dd){
    if(dd->dd_ops->open == NULL) return -1;
    return dd->dd_ops->open(dd);
}
int ddclose(dd_t* dd){
    if(!dd) return -1;
    if(dd->dd_ops->close) dd->dd_ops->close(dd);
    if (dd->driver_data) {
        osfree(dd->driver_data);
        dd->driver_data = NULL;
    }
    osfree(dd);
    return 0;
}
int ddread(dd_t* dd, void* data, uint32_t size, uint32_t mode){
    if(dd->dd_ops->read == NULL) return -1;
    return dd->dd_ops->read(dd, data, size, mode);
}
int ddwrite(dd_t* dd, void* data, uint32_t size, uint32_t mode){
    if(dd->dd_ops->write == NULL) return -1;
    return dd->dd_ops->write(dd, data, size, mode);
}

int ddioctl(dd_t* dd, const char* fmt, ...) {
    if (!dd || !dd->dd_ops || !dd->dd_ops->ioctl) return -1;
    va_list ap;
    va_start(ap, fmt);
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int ret = dd->dd_ops->ioctl(dd, fmt, ap_copy);
    va_end(ap_copy);
    va_end(ap);
    return ret;
}


