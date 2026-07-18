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

add_subdirectory(src)

target_link_libraries(KSCOS PRIVATE KSCOS_SRC easyx)

