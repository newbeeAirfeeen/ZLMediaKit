# author: oaho
# date: 2026/05/25
# description: Build image for ZLMediaKit-fmp4. Single native build for both x86_64 and
#   arm64 — the target architecture is selected purely by `docker build --platform`
#   (driven by the Jenkins TARGET_ARCH parameter). On an arm64 build the whole CentOS 7
#   userspace runs under the host's qemu/binfmt emulation, so the stock gcc 4.8.5 produces
#   binaries whose glibc 2.17 / libstdc++ 6.0.19 ABI matches the CentOS7-arm64 target
#   exactly. No cross toolchain / sysroot needed.
FROM centos:7

# CentOS 7 已 EOL，官方镜像源下线，全部切到 vault 归档源（amd64 与 arm64/altarch 同样适用）
RUN sed -i 's|mirrorlist.centos.org|vault.centos.org|g' /etc/yum.repos.d/CentOS-Base.repo && \
    sed -i 's|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-Base.repo && \
    yum clean all && yum makecache

# 基础依赖（用 CentOS7 自带 gcc 4.8.5，与目标机 ABI 一致；源码检出/子模块由 Jenkins 在宿主完成，无需新版 git）
RUN yum install -y gcc gcc-c++ make curl tar wget unzip git perl-IPC-Cmd xz bzip2

# CMake：用 Kitware 预编译二进制，按当前架构(uname -m: x86_64/aarch64)选对应包
RUN cd /opt && arch="$(uname -m)" && \
    curl -fsSL -o cmake.tar.gz "https://github.com/Kitware/CMake/releases/download/v3.28.0/cmake-3.28.0-linux-${arch}.tar.gz" && \
    tar -zxf cmake.tar.gz && rm -f cmake.tar.gz && \
    mv "cmake-3.28.0-linux-${arch}" /opt/cmake
ENV PATH=/opt/cmake/bin:$PATH

# OpenSSL 1.1.1w 源码构建（CentOS7 自带仅 1.0.2，ZLMediaKit 的 WebRTC 需要 1.1.1+）
RUN cd /opt && \
    wget -q https://github.com/openssl/openssl/releases/download/OpenSSL_1_1_1w/openssl-1.1.1w.tar.gz && \
    tar -zxf openssl-1.1.1w.tar.gz && cd openssl-1.1.1w && \
    env CFLAGS="-ftrapv -fstack-protector-strong -D_FORTIFY_SOURCE=2" LDFLAGS="-Wl,-z,now -Wl,-z,relro" ./config && \
    make -j$(nproc) && make install && \
    cd /opt && rm -rf openssl-1.1.1w openssl-1.1.1w.tar.gz

RUN useradd -m jenkins
USER jenkins
WORKDIR /home/jenkins
