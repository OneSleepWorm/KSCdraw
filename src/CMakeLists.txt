set(KSCOS_CORE_SRCS app.c KSCOSsystem.c KSCdraw.c KSCfont.c KSCimg.c)

if(STM32_FIRMWARE)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${KSCOS_CORE_SRCS} main.c)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../inc)

elseif(LINUX_NATIVE)
    # Linux: 框架核心保持纯 C, 直接进可执行文件 target_sources。
    # 不走 add_library —— README 已强调 REGISTER_APP 的 .c 进 library 会被
    # --gc-sections 丢弃, 导致 app_table 段为空。
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${KSCOS_CORE_SRCS})
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../inc)

else()
    # PC (MinGW + easyx): easyx 仅支持 C++, 故核心按 CXX 编译
    add_library(KSCOS_SRC ${KSCOS_CORE_SRCS})
    set_source_files_properties(${KSCOS_CORE_SRCS} PROPERTIES LANGUAGE CXX)

    target_compile_definitions(KSCOS_SRC PUBLIC __USE_AUTO__=1 __USE_PC__=1 __USE_STM32__=0)
    target_include_directories(KSCOS_SRC PUBLIC ../third_party/easyx/include)
    target_link_libraries(KSCOS_SRC PUBLIC easyx)
endif()
