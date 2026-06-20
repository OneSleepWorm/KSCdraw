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
 * @brief 获取设备驱动描述符
 * 
 * @param device_name 设备类型名(如 "tim", "uart"), 精确匹配 dev->base->device_name
 * @param dev_no 设备实例号(0=不指定,匹配第一个;非0=精确匹配 dev->private->inst_no)
 * @param driver_ops_name 驱动操作名(如 "clock", "keypad"), 可为 NULL
 * @return dd_t* 设备驱动描述符指针
 * @note 设备匹配: 精确匹配 type + inst_no
 * @note 驱动匹配: 前缀匹配 + ops_name 筛选
 */
dd_t* bus_getdriver(char* device_name, uint8_t dev_no, char* driver_ops_name)
{
    if(device_name == NULL)
        return NULL;

    // 设备匹配：精确匹配类型名 + inst_no 筛选设备
    for(const pdev_t* dev = KSC_bus.dev_table; dev < KSC_bus.dev_table + KSC_bus.dev_count; dev++)
    {
        if(strcmp(device_name, dev->base->device_name) != 0)
            continue;

        if(dev_no != 0 && (!dev->private || dev->private->inst_no != dev_no))
            continue;

        // 驱动匹配：设备前缀 + ops_name 筛选驱动
        for(const pdrv_t* drv = KSC_bus.drv_table; drv < KSC_bus.drv_table + KSC_bus.drv_count; drv++)
        {
            if(strncmp(device_name, drv->base->driver_name, strlen(device_name)) != 0)
                continue;

            if(driver_ops_name != NULL && strncmp(driver_ops_name, drv->ops->ops_name, strlen(driver_ops_name)) != 0)
                continue;

            dd_t* dd = osmalloc(sizeof(dd_t));
            if(dd == NULL) return NULL;
            dd->dev = dev;
            dd->driver = drv;
            dd->dd_ops = drv->ops;
            dd->callback = CALLBACK_NULL_FUNC;
            dd->driver_data = NULL;
            dd->user_data = NULL;

            if(drv->sysfunc && drv->sysfunc->probe)
                if(drv->sysfunc->probe(dd) != 0) { osfree(dd); return NULL; }

            return dd;
        }
    }

    return NULL;
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
    dd_t* d = dd;
    if(d->driver && d->driver->sysfunc && d->driver->sysfunc->remove)
        d->driver->sysfunc->remove(d);
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


