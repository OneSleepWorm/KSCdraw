# 注册专用文件：不进静态库，直接打进 exe 避免链接器丢弃 section 条目
# 这些文件中的 REGISTER_DEVICE / REGISTER_DRIVER 通过 linker section
# 注册设备/驱动，没有任何符号被外部直接引用，因此不能放在静态库中。
target_sources(KSCOS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sys_systime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sys_console.c
    ${CMAKE_CURRENT_SOURCE_DIR}/uart_printf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tim_clocktask.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gpio_keypad.c
)
