cmake_minimum_required(VERSION 3.10)

project(KSCOS)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(easyx STATIC IMPORTED)

set_target_properties(easyx PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_party/easyx/lib/libeasyx.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/easyx/include"
)

add_executable(KSCOS master.c)
set_source_files_properties(master.c PROPERTIES LANGUAGE CXX)

target_compile_options(KSCOS PRIVATE -Wall -Wextra)
target_compile_definitions(KSCOS PRIVATE LFS_NO_ASSERT LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR)

add_subdirectory(src)

target_sources(KSCOS PRIVATE
    apps/uart_serial.c apps/kscgui.c apps/tim_clock.c apps/button16.c apps/snake.c apps/list.c apps/terminal.c apps/open.c
    apps/w25qxx_base.c apps/littlefs_fs.c
    third_party/littlefs/lfs.c third_party/littlefs/lfs_util.c)
set_source_files_properties(
    apps/uart_serial.c apps/kscgui.c apps/tim_clock.c apps/button16.c apps/snake.c apps/list.c apps/terminal.c apps/open.c
    apps/w25qxx_base.c apps/littlefs_fs.c PROPERTIES LANGUAGE CXX)
set_source_files_properties(
    third_party/littlefs/lfs.c third_party/littlefs/lfs_util.c PROPERTIES LANGUAGE C)

target_include_directories(KSCOS PRIVATE third_party/littlefs)

target_link_libraries(KSCOS PRIVATE KSCOS_SRC easyx)

