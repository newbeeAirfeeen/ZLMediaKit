# Relay 第一版实现与验收

源码提供 addRelayProxy/delRelayProxy、旧 URL 别名、按链接分流、ESD 查询、私有协议登录/播放/心跳、H.264/H.265/AAC/G711A 解复用、通用转码接入、重连及无人观看回收。使用与 streamproxy 生成代码匹配的 protobuf-lite 源码构建。

本地验证通过真实 MediaServer 的 HTTP 入口、回环 ESD 和 TCP 中继运行，检查播放列表、分片编码与解码结果，也覆盖重复 add、删除中途任务、同名重建和物联网路径的 token 规则。已有物联网生命周期和回放控制另有回归脚本。具体复现命令见 [Relay 使用说明](../relay/README.md)。

## 原业务链接的兼容缺口

本地已用真实 MediaServer 和模拟 ESD、中继验证 `/hlsapi/live/hls/fixture.m3u8?{32位随机串}`，覆盖透传和转码。两次变更 query 的请求复用了同一代理；同一实例的 RTSP V4 主码流与 V3 子码流地址也有 DESCRIBE 路由用例，检查返回 SDP。不同前缀、跨 app、单前缀、空配置和分号空项均有集成断言。[测试入口](../../tests/test_relay_live.py)

| 优先级 | 已知差异与影响 | 补齐或验收条件 |
| --- | --- | --- |
| 优先确认 | V3 的设备 key 只在 query。wlw 将问号后的参数单独保存，RTSP 鉴权后按 `schema/vhost/app/stream` 查已有媒体源，代理也按三元 key 复用。同一实例、同一 vhost 和频道下，两台设备的原始 V3 URL 可能解析为同一个流。旧二进制的代理 helper 同样先按三元 key 查表、后取 auth_key；旧部署是否改写 URL 未知。 | 用脱敏的入站 DESCRIBE 核对网关后的路径。如果仍是同路径，在首次媒体查找前形成含设备标识的内部身份，同步核对代理创建、删除、查流、回收及原 API 返回 key。验收两台同频道设备相互独立。[URL 解析](../../src/Common/MediaSource.cpp) · [RTSP 查流](../../src/Rtsp/RtspSession.cpp) |
| 待现场核对 | V4 路径含 playKey。V3/V4 均从 auth_key 发现设备。当前登录使用 ESD 返回的设备 ID，channel_name 固定为 720p，未将 URL 的 main/sub1 单独传给 relay。旧二进制中也观察到 720p。 | 核对 playKey 是否已经绑定码流，用真机分别播放主码流和子码流。1507 按上游提供的规则发生在 OpenAPI 生成 URL 时，无需在 MediaServer 重做型号判断。[登录字段](../../src/Relay/RelayPlayer.cpp) |
| 合同差异 | 缺流和无人观看在匹配范围内直接调用本进程，旧配置则向自身 add/del 发 HTTP Hook。旧 ESD helper 读取 failmsg 和 hook.timeoutSec，当前将上游错误归并为阶段错误，启动阶段受播放器 deadline 约束。 | 对照设备离线、签名失败、中继为空及超时的业务响应。如果运维或调用方依赖原错误消息、Hook 通知或重试次数，按实际合同补齐。[发现](../../src/Relay/RelayDiscovery.cpp) · [缺流及回收](../../server/WebHook.cpp) |
| 接口缺项 | getMediaInfo/getMediaList 缺 frames、key_frames、gop_size、gop_interval_ms 和 GOP 辅助 fps。openRtpServer 缺 only_audio。startSendRtp 与 startSendRtpPassive 缺 recv_stream_id，后者还缺 close_delay_ms 的 HTTP 映射。 | 根据业务调用清单安排。收发 RTP 和纯音频需验证实际媒体效果，响应成功本身不足以证明参数生效。[管理接口](../../server/WebApi.cpp) |
| 媒体验收 | 本地已验证 H.264 透传、HEVC→H.264、缩放和 G711A→AAC。旧版音频转换与当前 FFmpeg 实现不同。配置变化会触发当前 Demuxer 报错并由播放器处理断流。 | 补现场 AAC、16 kHz G711A、纯音频/纯视频、晚到第二轨、分辨率变化、HEVC 保留编码时缩放及长会话音画同步。对原业务实际使用的 RTSP、FLV、TS、fMP4 逐项验证。[解复用](../../src/Relay/RelayDemuxer.cpp) |

切换配置还应核对 ESD 字段回退和原 Hook。新 ESD 键存在但值为空时会覆盖旧键；把两个配置段简单拼接可能造成发现失败。wlw 原有 AuthCenter、SessionMeta 和推流来源规则仍按其业务配置生效；共同 API 的额外监控字段或编译可选路由，只有在调用方依赖精确接口清单或 JSON 字段集合时才需要处理。

[源码构建和依赖清单](../relay/dependencies.md)
