# Relay 摄像头接入

wlw 按播放地址的 vhost 和路径前缀识别 Relay 业务。物联网原业务继续走现有推拉流、鉴权、观看上报和回放控制。部署时将原摄像头地址所在的范围配置为 Relay，业务方继续使用原链接。

```ini
[relay]
vhost=__defaultVhost__
streamPrefix=/hlsapi/live/hls/;/live/1/main/h264/tcp/unicast/;/live/2/sub1/h264/tcp/unicast/
esdAddress=esd.example.internal:80
esdAccessKey=<部署值>
esdSecret=<部署值>
videoH265Transcode=1
audioTranscode=1
reduceResolution=1
transcodeHeight=720
```

这三个前缀在同一个 MediaServer 实例上分别接入原 HLS、RTSP 主码流和 RTSP 子码流。按 MediaServer 实际收到的路径填写。HLS 的 32 位随机 query 和 RTSP 的 auth_key 均不参与路径匹配。

| streamPrefix 中的一项 | 匹配对象 | 示例 |
| --- | --- | --- |
| 以 `/` 开头 | 当前 vhost 下的 `/app/stream_id` | `/hlsapi/live/hls/` 仅接入 hlsapi 下的对应路径 |
| 其他非空值 | 当前 vhost 下任意 app 的 `stream_id` | `devices/` 匹配 `devices/A`，不匹配 `archive/devices/A` |

配置按半角分号切分，去掉每项两端空白并忽略空项；任一项从开头匹配即进入 Relay。目录前缀使用末尾的 `/` 区分同名开头的其他目录。空值和仅含分隔符都关闭 Relay 路由。

迁移原先的配置时，如需保留原范围，将 `app=cameras` 和 `streamPrefix=devices/` 合并为 `streamPrefix=/cameras/devices/`；原来希望匹配整个 cameras app 时写 `/cameras/`。app 仍是原 URL 和管理 API 中流身份的一部分。旧 `[closeli]` 的 ESD 和转码键在对应新键缺失时回退，包含历史拼写 `esdAdder` 和 `TranscodeHeigh`。

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
