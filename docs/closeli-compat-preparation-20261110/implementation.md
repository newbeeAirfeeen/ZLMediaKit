# Relay 第一版实现与验收

源码提供 addRelayProxy/delRelayProxy、旧 URL 别名、按链接分流、ESD 查询、私有协议登录/播放/心跳、H.264/H.265/AAC/G711A 解复用、通用转码接入、重连及无人观看回收。使用与 streamproxy 生成代码匹配的 protobuf-lite 源码构建。

本地验证通过真实 MediaServer 的 HTTP 入口、回环 ESD 和 TCP 中继运行，检查播放列表、分片编码与解码结果，也覆盖重复 add、删除中途任务、同名重建和物联网路径的 token 规则。已有物联网生命周期和回放控制另有回归脚本。具体复现命令见 [Relay 使用说明](../relay/README.md)。

下一次业务验收使用原客户端和测试摄像头，核对真实 ESD 错误、设备音视频配置、长会话和重连后的时间轴。目标旧版的其他标准 API 差异包括 RTP 回传参数、纯音频 RTP 接收及帧/GOP 统计，需要按实际调用另行补齐。

[源码构建和依赖清单](../relay/dependencies.md)
