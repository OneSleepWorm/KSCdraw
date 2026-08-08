if(STM32_FIRMWARE)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gpio_port.c
        ${CMAKE_CURRENT_SOURCE_DIR}/super_spi.c
    )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tim_clock.c
    ${CMAKE_CURRENT_SOURCE_DIR}/button16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/snake.c
    ${CMAKE_CURRENT_SOURCE_DIR}/kscgui.c
    ${CMAKE_CURRENT_SOURCE_DIR}/w25qxx_base.c
    ${CMAKE_CURRENT_SOURCE_DIR}/littlefs_fs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/terminal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/open.c
    ${CMAKE_CURRENT_SOURCE_DIR}/transfer.c
)
