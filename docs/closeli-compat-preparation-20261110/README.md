# wlw Relay 适配

本次实现将 streamproxy 的 Relay 直播协议接入 wlw，通过配置选择摄像头业务，复用原媒体封装与通用转码。初期准备材料中的 Closeli 是旧程序的协议模块名称。新源码采用 Relay，旧名称仅用于线上的 API 和配置兼容。

- 播放、管理和切换的业务要求见 [业务需求](business-requirements.md)。
- 软件模块和启动时序见 [软件架构](architecture.md)。
- 本次完成项与待验收项见 [实现状态](implementation.md)。
- 配置、源码入口和测试命令见 [Relay 使用说明](../relay/README.md)。
- 外部库和本次源码构建记录见 [依赖清单](../relay/dependencies.md)。
