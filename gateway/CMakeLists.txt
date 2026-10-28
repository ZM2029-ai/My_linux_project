cmake_minimum_required(VERSION 3.13)

# chroot 内 cmake 为 3.13.4，上面的版本号不能更高
project(edgelink VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

add_executable(edgelink
    src/main.cpp
)

target_include_directories(edgelink PRIVATE src)

target_compile_definitions(edgelink PRIVATE
    EDGELINK_VERSION="${PROJECT_VERSION}"
)

# -O2/-g 由构建类型决定，这里只加警告
target_compile_options(edgelink PRIVATE -Wall -Wextra)

install(TARGETS edgelink RUNTIME DESTINATION bin)
