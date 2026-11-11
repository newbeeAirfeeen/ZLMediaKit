# Relay 的源码构建与外部依赖

## 纳入提交的源码

直播接收逻辑来自本地 streamproxy 提交 `db3ff54b2db41cbf01daed4c5d001522bd5556fd` 的 `3rdlib/NetWorkTool/src/Closeli` 及相应 Extension 解码器。新代码按 wlw 的 TcpClient、PlayerProxy、Track 和 Frame 接口整理。消息定义与生成的 C++ 来自同一目录，保留协议字段编号和生成代码中的历史枚举拼写。

protobuf 取自该 NetWorkTool 配套的 `3rdpart/protobuf`（3.20.3）。消息定义指定 LITE_RUNTIME，所以采用上游 cmake/libprotobuf-lite.cmake 的 32 个编译单元及其所需头文件。CMake 通过 add_subdirectory 构建 wlw_protobuf_lite 静态目标，zlmediakit 按目标依赖链接。提交中包含 protobuf 的 LICENSE。正常构建直接编译随源码提供的 pb.cc，不需要系统 protoc；测试对端使用 protoc 检查实际登录和播放消息。

## 已有的外部库

| 库 | 现有构建入口 | 本次验证中的来源 |
| --- | --- | --- |
| OpenSSL libssl/libcrypto | 根 CMake 使用 find_package(OpenSSL REQUIRED)，configctl 也查找 OpenSSL | **MediaServer 实际链接本机已安装的 OpenSSL 3 动态库**。jenkins.Dockerfile 另有从源码编译 OpenSSL 的路径 |
| libavutil、libavcodec、libswscale、libswresample | ENABLE_FFMPEG 通过 pkg-config 或 find_library 查找 | 转码测试使用本次从 FFmpeg n4.4.5 源码编译的四个库。普通透传构建可以关闭 FFmpeg |
| libx264 | FFmpeg H.264 软件编码器依赖，也有项目原有 ENABLE_X264 的直接链接路径 | 转码测试的 libx264 由源码编译，作为 libavcodec 的依赖 |
| HEVC 编码器 | 保留 HEVC 且确需缩放时，FFmpeg 后端要提供 HEVC encoder | 本次验证 HEVC 解码和 H.264 编码。HEVC 重编码组合需要另行构建并验证 |
| MySQL、FAAC | 原有 ENABLE_MYSQL、ENABLE_FAAC 条件查找 | 本次构建关闭 |
| jemalloc | 默认探测，找到可加入链接；ENABLE_JEMALLOC_STATIC 另有源码构建脚本 | 本次链接清单无 jemalloc |
| libsrtp2、usrsctp | WebRTC 子目录通过 FindSRTP、FindSCTP 查找 | 本次构建关闭 WebRTC |
| SDL2 | 独立 player 目标通过 pkg-config 或 FindSDL2 查找 | 转码测试只构建 MediaServer |
| 平台运行库 | pthread、libc++、libSystem；其他平台的 dl、atomic、Windows 网络库等 | 由系统工具链提供 |

普通 dockerfile 会安装部分发行版开发包，包括 x264 和 MySQL 客户端；其中的 ffmpeg 命令属于独立程序。源码树中的 jsoncpp、ZLToolKit、mpeg/mov/flv 和新增 protobuf-lite 均由本项目构建。CMake 的链接参数形式不能单独判定来源，例如本次从源码产生的库最终也会出现在系统链接器命令中。

**待讨论的决定**是将 OpenSSL 和启用的编解码库作为仓库内源码、固定版本的构建步骤，还是继续允许指定外部源码安装前缀。其他可选模块同样需要在启用时决定来源。本提交未引入随仓库分发的预构建库。

## 复现源码编解码后端

转码验证所用 FFmpeg 提交为 `9bcede27c26b2f7cd469ab6b5c8b9694c30cfca3`，x264 提交为 `c24e06c2e184345ceb33eb20a15d1024d9fd3497`。下面命令分别在对应源码目录执行，两次构建使用同一个绝对路径前缀。

```sh
# x264 源码目录
./configure --prefix="$RELAY_CODEC_PREFIX" --enable-shared --disable-cli
make -j8 && make install

# FFmpeg 源码目录
PKG_CONFIG_PATH="$RELAY_CODEC_PREFIX/lib/pkgconfig" ./configure \\
  --prefix="$RELAY_CODEC_PREFIX" --disable-autodetect --disable-all --disable-programs \\
  --disable-doc --enable-shared --disable-static --enable-pic --enable-gpl --enable-libx264 \\
  --enable-avcodec --enable-avutil --enable-swresample --enable-swscale \\
  --enable-decoder=h264,hevc,aac,pcm_alaw --enable-parser=h264,hevc,aac \\
  --enable-encoder=libx264,aac
make -j8 && make install

# wlw 源码目录
PKG_CONFIG_PATH="$RELAY_CODEC_PREFIX/lib/pkgconfig" cmake -S . -B build-relay \\
  -DENABLE_FFMPEG=ON -DENABLE_PLAYER=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \\
  -DCMAKE_CXX_FLAGS="-I$RELAY_CODEC_PREFIX/include"
cmake --build build-relay --target MediaServer -j8
```

用 CMakeCache 的 pkgcfg_lib 项核对四个库的解析路径，再以 otool -L 或 ldd 验证可执行文件的实际动态依赖。生产部署需一起提供该前缀中的动态库，或按部署环境另行构建静态后端。
