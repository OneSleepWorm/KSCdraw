# BSP 平台驱动 — 按目标平台编译对应实现。
# 含 REGISTER_APP 的文件必须进 target_sources (LTO + --gc-sections 丢 orphan section)。
# mempool 是共享池体 (bsp/share/src), 两平台都编译。
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/share/src/mempool.c
)

if(STM32_FIRMWARE)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/stm32/gpio_port.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stm32/super_spi.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stm32/uart_serial.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stm32/w25qxx_base.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stm32/tim_clock.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stm32/button16.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stm32/gui_drv.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stm32/system.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stm32/system_zone.c
    )
elseif(LINUX_NATIVE)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/linux/uart_serial.c
        ${CMAKE_CURRENT_SOURCE_DIR}/linux/w25qxx_base.c
        ${CMAKE_CURRENT_SOURCE_DIR}/linux/tim_clock.c
        ${CMAKE_CURRENT_SOURCE_DIR}/linux/button16.c
        ${CMAKE_CURRENT_SOURCE_DIR}/linux/gui_drv.c
        ${CMAKE_CURRENT_SOURCE_DIR}/linux/system.c
        ${CMAKE_CURRENT_SOURCE_DIR}/linux/system_zone.c
    )
else()
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/pc/uart_serial.c
        ${CMAKE_CURRENT_SOURCE_DIR}/pc/w25qxx_base.c
        ${CMAKE_CURRENT_SOURCE_DIR}/pc/tim_clock.c
        ${CMAKE_CURRENT_SOURCE_DIR}/pc/button16.c
        ${CMAKE_CURRENT_SOURCE_DIR}/pc/gui_drv.c
        ${CMAKE_CURRENT_SOURCE_DIR}/pc/system.c
        ${CMAKE_CURRENT_SOURCE_DIR}/pc/system_zone.c
    )
endif()
