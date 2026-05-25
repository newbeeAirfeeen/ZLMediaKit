# author: oaho
# date: 2026/05/25
# description: CMake cross-compilation toolchain file for aarch64 (arm64) targets.
#   Selected when Jenkins TARGET_ARCH=arm64. The cross GCC (>=7) toolchain and an
#   aarch64 sysroot (with cross-built OpenSSL) are provisioned inside jenkins.Dockerfile.
#   The cross prefix and sysroot are passed in via the CROSS_TRIPLE / CROSS_SYSROOT
#   environment variables so the same file works regardless of the exact toolchain vendor.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# 交叉工具链三元组前缀，由 Dockerfile 导出（默认 aarch64-linux-gnu）
if(DEFINED ENV{CROSS_TRIPLE})
    set(CROSS_TRIPLE $ENV{CROSS_TRIPLE})
else()
    set(CROSS_TRIPLE "aarch64-linux-gnu")
endif()

set(CMAKE_C_COMPILER   ${CROSS_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${CROSS_TRIPLE}-g++)
set(CMAKE_AR           ${CROSS_TRIPLE}-ar)
set(CMAKE_RANLIB       ${CROSS_TRIPLE}-ranlib)
set(CMAKE_STRIP        ${CROSS_TRIPLE}-strip)

# 目标 sysroot（含交叉编译的 openssl 等依赖），由 Dockerfile 导出
if(DEFINED ENV{CROSS_SYSROOT})
    set(CMAKE_SYSROOT $ENV{CROSS_SYSROOT})
    set(CMAKE_FIND_ROOT_PATH $ENV{CROSS_SYSROOT})
endif()

# 在 sysroot 内查找库/头文件，但程序(编译器/工具)仍用宿主机的
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
