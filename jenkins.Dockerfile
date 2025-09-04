# 使用 CentOS 7 作为基础镜像，因为它默认带有 GCC 4.8.5
FROM centos:7
# 更改centos的安装软件下载镜像为vault
RUN sed -i 's|mirrorlist.centos.org|vault.centos.org|g' /etc/yum.repos.d/CentOS-Base.repo && \
    sed -i 's|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-Base.repo && \
    yum clean all && \
    yum makecache \
RUN yum install -y https://packages.endpointdev.com/rhel/7/os/x86_64/endpoint-repo.x86_64.rpm
# 安装依赖项
RUN yum update -y && yum install -y gcc gcc-c++ make curl tar wget unzip git perl-IPC-Cmd


WORKDIR /opt
RUN wget https://github.com/openssl/openssl/releases/download/OpenSSL_1_1_1w/openssl-1.1.1w.tar.gz && \
    tar -zxvf openssl-1.1.1w.tar.gz && \
    cd openssl-1.1.1w && \
    ./config --prefix=/opt/openssl && \
    make -j$(nproc)  && \
    make install

RUN wget https://github.com/Kitware/CMake/archive/refs/tags/v3.28.0.zip && \
    unzip v3.28.0.zip && \
    cd CMake-3.28.0 && \
    ./bootstrap && \
    make -j$(nproc) && \
    make install
# 创建 jenkins 用户和家目录
RUN useradd -m jenkins
USER jenkins
WORKDIR /home/jenkins
# 安装 Rust
#RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
