# Relay 接入设计

业务依据现有链接和运行时配置区分。Discovery 负责设备和中继查询，RelayPlayer 实现拉流及协议控制，Demuxer 产出媒体帧，FrameTranscoder 复用共享编解码组件。原型中的接口层已收敛到实际调用路径。

[软件架构](architecture.md) · [源码与配置](../relay/README.md)
