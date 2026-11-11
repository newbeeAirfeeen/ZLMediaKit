# Relay 摄像头接入

wlw 按现有播放地址的 vhost、app 和可选 stream 前缀识别 Relay 业务。物联网原业务继续走现有推拉流、鉴权、观看上报和回放控制。部署时将原摄像头地址所在的范围配置为 Relay，业务方继续使用原链接。

```ini
[relay]
vhost=__defaultVhost__
app=cameras
streamPrefix=
esdAddress=esd.example.internal:80
esdAccessKey=<部署值>
esdSecret=<部署值>
videoH265Transcode=1
audioTranscode=1
reduceResolution=1
transcodeHeight=720
```

示例中的 app 应填线上已有值。如果同一 app 同时承载物联网和摄像头，可将 streamPrefix 配成已有路径前缀，例如 `devices/`。空 app 表示关闭 Relay 路由。若两种业务的 vhost、app 和流名前缀完全重叠，切换时需要在原服务入口上区分。旧 `[closeli]` 中的 ESD 和转码键作为新键缺失时的回退，包含 `esdAdder` 和 `TranscodeHeigh` 的历史拼写。

## 从链接到媒体

1. 已授权的 HLS 或 RTSP 请求找不到流时，缺流处理器检查上述范围。HLS 使用流名末段作为 publicKey，RTSP 读取 params 中的 auth_key。
2. 服务在现有代理表中按 vhost/app/stream 查找或创建 PlayerProxy。内部 `relay://` 源由 RelayPlayer 处理。播放器在指定 poller 上发起两次 ESD POST，取得设备凭据与中继地址。代理重试会重新发现设备。
3. RelayPlayer 按 streamproxy 的直播协议发送登录和播放命令，处理长度前缀、消息分片与合并、心跳、断开及媒体超时。生成的 proto2 消息与匹配的 protobuf-lite 都随项目源码编译。
4. Demuxer 将 H.264、H.265、AAC、G711A 配置及数据包转换为 wlw 原生 Track 和 Frame。需要转换时，通过通用 FrameTranscoder 组合已有 FFmpegDecoder、FFmpegSws 和 FFmpegEncoder。缩放先按比例计算偶数尺寸，再交给编码器。G711A 转码输出单声道 8 kHz AAC。
5. 输出轨道挂到 PlayerProxy 后交付启动期缓存的帧，继续走原 MultiMediaSourceMuxer 和 HLS 封装。首个 HLS 请求等待媒体注册期间，代理保留上游。已有 cookie 遇到重连会重新查找媒体源。无人观看后沿用原延时配置回收。

显式管理通过 `/index/api/addRelayProxy` 和 `/index/api/delRelayProxy`。原 addCloseliProxy/delCloseliProxy 两条路径作为相同处理器的协议别名保留。两个接口均检查目标是否匹配 Relay 配置范围，沿用旧调用的凭据要求。范围内的代理与 addStreamProxy 使用同一注册表。

| 操作 | 参数 | 响应 |
| --- | --- | --- |
| addRelayProxy | 必填 vhost、app、stream、schema。schema 为 hls 或 rtsp。rtsp 从 params 取得 auth_key。支持原 ProtocolOption | 成功返回 code=0、data.key。上游失败返回 code=-1 和 msg |
| delRelayProxy | 必填 vhost、app、stream、schema，使用前三项定位代理 | code=0、data.flag 为布尔值 |
| 重复 add | 同一个三元组 | 复用现有代理并返回 key。API 成功和 HLS 首个分片是两个可观测事件 |

## 源码位置

| 代码 | 职责 |
| --- | --- |
| [RelayConfig](../../src/Relay/RelayConfig.cpp) | 路由匹配、原参数映射、启动期观看者 |
| [Discovery](../../src/Relay/RelayDiscovery.cpp) | ESD 签名、请求与设备/中继响应检查 |
| [RelayPlayer](../../src/Relay/RelayPlayer.cpp) | 私有 TCP 协议、超时及 PlayerProxy 生命周期衔接 |
| [Demuxer](../../src/Relay/RelayDemuxer.cpp) | 私有配置和媒体负载转原生轨道 |
| [FrameTranscoder](../../src/Codec/FrameTranscoder.cpp) | 协议无关的音视频转换 |
| [Relay 消息](../../src/Relay/RelayServerMessage.proto) | streamproxy 的完整 proto2 消息定义 |
| [依赖与源码构建](dependencies.md) | 源码来源、构建命令、需要讨论的外部库 |
| [业务需求](../closeli-compat-preparation-20261110/business-requirements.md) | 验收范围与本次实现边界 |

## 本地功能验证

在仓库根目录，第一条使用普通 MediaServer 构建，第二条使用启用通用 FFmpeg 后端的构建。测试以回环 ESD 和 TCP 中继送入实际媒体，通过真实 HTTP 入口取播放列表和分片并解码。

```sh
python3 tests/test_relay_live.py /path/to/MediaServer
python3 tests/test_relay_live.py /path/to/ffmpeg-enabled/MediaServer --transcode
python3 tests/test_stream_lifecycle_integration.py /path/to/MediaServer
python3 tests/test_playback_control.py /path/to/MediaServer
```

第二条覆盖 128×96 HEVC 转 64×48 H.264、G711A 转 AAC、同 key 复用、连接中断后重新发现、无人观看回收及删除后同名重建。现场设备和实际业务客户端仍需联调。真实设备配置变化、非标准媒体负载以及所有旧管理接口的全量对照仍待验收。
