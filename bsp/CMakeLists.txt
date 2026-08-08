# BSP 平台驱动 — 按目标平台编译对应实现。
# 含 REGISTER_APP 的文件必须进 target_sources (LTO + --gc-sections 丢 orphan section)。
if(STM32_FIRMWARE)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/stm32/uart_serial.c
    )
else()
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/pc/uart_serial.c
    )
endif()
