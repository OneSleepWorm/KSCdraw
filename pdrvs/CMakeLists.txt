# 注册专用文件：不进静态库，直接打进 exe 避免链接器丢弃 section 条目
# 这些文件中的 REGISTER_DEVICE / REGISTER_DRIVER 通过 linker section
# 注册设备/驱动，没有任何符号被外部直接引用，因此不能放在静态库中。
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sys_time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sys_console.c
    # ${CMAKE_CURRENT_SOURCE_DIR}/uart_printf.c  # 已由 apps/uart_serial.c 替代
    # ${CMAKE_CURRENT_SOURCE_DIR}/tim_clocktask.c  # 已由 apps/tim_clock.c 替代
    ${CMAKE_CURRENT_SOURCE_DIR}/gpio_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/spi_master.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sys_clock.c
    # ${CMAKE_CURRENT_SOURCE_DIR}/uart_serial.c  # 已由 apps/uart_serial.c 替代
)
