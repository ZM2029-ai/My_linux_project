# EdgeLink 交叉编译工具链配置
#
# 目标环境（实测，详见 复现文档/阶段一/ 与 README/configuration_of_imax6ull.md）：
#   - i.MX6ULL / armv7l / armhf 硬浮点
#   - Debian 10 (buster)，glibc 2.28，libstdc++ GLIBCXX_3.4.25
#   - 内核 4.19.35-imx6
#
# 因此工具链必须是 GCC 8.3（Ubuntu 24.04 自带的 GCC 13 会要求 GLIBC_2.34，板上跑不起来）。
# 本工具链位于 Debian 10 chroot（默认 /opt/buster）中，二进制带 -8 后缀。
#
# 用法（在 chroot 内构建时）：
#   cmake -S /work/gateway -B /build \
#     -DCMAKE_TOOLCHAIN_FILE=/work/gateway/cmake/toolchain-arm-linux-gnueabihf.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 绝对路径，避免与宿主 /usr/bin/arm-linux-gnueabihf-g++（GCC 13）混淆
set(CMAKE_C_COMPILER   /usr/bin/arm-linux-gnueabihf-gcc-8)
set(CMAKE_CXX_COMPILER /usr/bin/arm-linux-gnueabihf-g++-8)

# 说明：Debian 交叉编译器的 sysroot 编译期已内置，由 gcc 自行解析，
# 不需要也不建议硬编码 CMAKE_SYSROOT。
# 如需确认实际值：arm-linux-gnueabihf-gcc-8 -print-sysroot

# 程序（构建期工具）在宿主机找；库与头文件只在目标 sysroot 找
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
