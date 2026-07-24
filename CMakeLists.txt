cmake_minimum_required(VERSION 3.22)
project(KSCOS)

option(STM32_FIRMWARE "Build for STM32 target" OFF)

# === 平台分支 ===
if(STM32_FIRMWARE)
    enable_language(C ASM)

    add_executable(KSCOS)

    target_compile_definitions(KSCOS PRIVATE __USE_STM32__=1 __USE_PC__=0 STM32F103xB)
    target_compile_options(KSCOS PRIVATE -Wall -fdata-sections -ffunction-sections -fstack-usage -flto)

    target_link_options(KSCOS PRIVATE
        -T "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stm32/STM32F103XX_FLASH.ld"
        -Wl,-Map=${PROJECT_NAME}.map -Wl,--gc-sections -Wl,--print-memory-usage -flto)

    target_sources(KSCOS PRIVATE
        third_party/stm32/startup/startup_stm32f103xb.s
        third_party/stm32/src/system_stm32f1xx.c
        third_party/stm32/src/syscalls.c
        third_party/stm32/src/sysmem.c
        third_party/stm32/src/stm32f1xx_it.c
    )
    target_include_directories(KSCOS PRIVATE
        third_party/stm32/inc/CMSIS
        third_party/stm32/inc/CMSIS_Device
    )

else()
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    enable_language(C CXX)

    add_library(easyx STATIC IMPORTED)
    set_target_properties(easyx PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/easyx/lib/libeasyx.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/easyx/include"
    )

    add_executable(KSCOS src/main.c)
    set_source_files_properties(src/main.c PROPERTIES LANGUAGE CXX)

    target_compile_definitions(KSCOS PRIVATE __USE_AUTO__=1 __USE_PC__=1 __USE_STM32__=0)
    target_compile_options(KSCOS PRIVATE -Wall -Wextra)
endif()

# === 共享选项 ===
target_compile_definitions(KSCOS PRIVATE LFS_NO_ASSERT)
# 强制注入 KSCconfig.h 到所有编译单元 (使 lfs.c 等第三方文件能读到 __USE_LITTLEFS_READONLY__)
target_compile_options(KSCOS PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/inc/KSCconfig.h")

set_target_properties(KSCOS PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# === 共享源文件 ===
add_subdirectory(src)
add_subdirectory(apps)

target_sources(KSCOS PRIVATE
    third_party/littlefs/lfs.c
    third_party/littlefs/lfs_util.c
    third_party/async_xmodem/xmodem_server.c
)
target_include_directories(KSCOS PRIVATE
    inc apps
    third_party/littlefs
    third_party/async_xmodem
)

# === PC 额外链接 ===
if(NOT STM32_FIRMWARE)
    target_link_libraries(KSCOS PRIVATE KSCOS_SRC easyx)
endif()

