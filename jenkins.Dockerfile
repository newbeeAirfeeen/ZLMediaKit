# author: oaho
# date: 2026/05/25
# description: Build image for ZLMediaKit-fmp4. Keeps CentOS 7 (glibc 2.17) base for
#   broad x86_64 runtime compatibility, but upgrades the native compiler to GCC 9 via
#   devtoolset-9 so the minimum supported GCC is >= 7. For arm64 cross-builds it also
#   provisions a prebuilt aarch64 GCC (>=7) toolchain and a cross-built OpenSSL sysroot.
#   The Jenkins TARGET_ARCH parameter selects native (x86_64) vs cross (arm64) at build time.

FROM centos:7

# CentOS 7 已 EOL，官方镜像源下线，全部切到 vault 归档源
RUN sed -i 's|mirrorlist.centos.org|vault.centos.org|g' /etc/yum.repos.d/CentOS-Base.repo && \
    sed -i 's|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-Base.repo && \
    yum clean all && yum makecache

# 基础依赖
RUN yum install -y https://packages.endpointdev.com/rhel/7/os/x86_64/endpoint-repo.x86_64.rpm && \
    yum install -y gcc gcc-c++ make curl tar wget unzip git perl-IPC-Cmd xz bzip2

# 原生 GCC9（devtoolset-9）：满足"最低 GCC7"，且 devtoolset 静态链接新版 libstdc++，
# 产物仍可在 glibc 2.17 上运行，保持 x86 兼容性。SCL 源同样切 vault。
RUN yum install -y centos-release-scl && \
    sed -i 's|mirrorlist.centos.org|vault.centos.org|g' /etc/yum.repos.d/CentOS-SCLo-scl*.repo && \
    sed -i 's|# *baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-SCLo-scl*.repo && \
    yum clean all && yum makecache && \
    yum install -y devtoolset-9 devtoolset-9-libatomic-devel
ENV PATH=/opt/rh/devtoolset-9/root/usr/bin:$PATH \
    LD_LIBRARY_PATH=/opt/rh/devtoolset-9/root/usr/lib64

# aarch64 交叉工具链（Bootlin glibc 2.31 + GCC 9.3：GCC>=7，目标 glibc 兼容现代发行版且满足 ffmpeg 的 GLIBC_2.28 下限）
# 注：若目标机 glibc 与此不符，调整此处 Bootlin 版本即可。
ENV CROSS_TRIPLE=aarch64-linux \
    CROSS_ROOT=/opt/aarch64-toolchain \
    CROSS_SYSROOT=/opt/aarch64-sysroot
RUN mkdir -p ${CROSS_ROOT} && cd /tmp && \
    wget -q https://toolchains.bootlin.com/downloads/releases/toolchains/aarch64/tarballs/aarch64--glibc--stable-2020.08-1.tar.bz2 && \
    tar -xjf aarch64--glibc--stable-2020.08-1.tar.bz2 -C ${CROSS_ROOT} --strip-components=1 && \
    rm -f aarch64--glibc--stable-2020.08-1.tar.bz2
ENV PATH=${CROSS_ROOT}/bin:$PATH

# CMake（native，构建过程统一使用）
RUN cd /opt && \
    wget -q https://github.com/Kitware/CMake/releases/download/v3.28.0/cmake-3.28.0.tar.gz && \
    tar -zxf cmake-3.28.0.tar.gz && cd cmake-3.28.0 && \
    ./bootstrap --parallel=$(nproc) && make -j$(nproc) && make install && \
    cd /opt && rm -rf cmake-3.28.0 cmake-3.28.0.tar.gz

# 原生 x86_64 OpenSSL（安装到 /usr/local）
RUN cd /opt && \
    wget -q https://github.com/openssl/openssl/releases/download/OpenSSL_1_1_1w/openssl-1.1.1w.tar.gz && \
    tar -zxf openssl-1.1.1w.tar.gz && cd openssl-1.1.1w && \
    env CFLAGS="-ftrapv -fstack-protector-strong -D_FORTIFY_SOURCE=2" LDFLAGS="-Wl,-z,now -Wl,-z,relro" ./config && \
    make -j$(nproc) && make install && \
    cd /opt && rm -rf openssl-1.1.1w

# aarch64 交叉编译 OpenSSL 到 CROSS_SYSROOT（ZLMediaKit 的 WebRTC/SRT/OpenSSL 需要）
RUN cd /opt && \
    wget -q https://github.com/openssl/openssl/releases/download/OpenSSL_1_1_1w/openssl-1.1.1w.tar.gz && \
    tar -zxf openssl-1.1.1w.tar.gz && cd openssl-1.1.1w && \
    ./Configure linux-aarch64 --prefix=${CROSS_SYSROOT}/usr --openssldir=${CROSS_SYSROOT}/usr/ssl \
        --cross-compile-prefix=${CROSS_TRIPLE}- && \
    make -j$(nproc) && make install_sw && \
    cd /opt && rm -rf openssl-1.1.1w openssl-1.1.1w.tar.gz

RUN useradd -m jenkins
USER jenkins
WORKDIR /home/jenkins
