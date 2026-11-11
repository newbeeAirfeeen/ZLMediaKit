# 物联网业务与 Relay 摄像头接入需求

wlw 在现有物联网直播、回放服务中增加 Relay 摄像头接入。原物联网业务继续使用现有鉴权、会话归属和观看事件。摄像头业务使用已有 HLS 或 RTSP 地址自动拉流，最终由 wlw 提供播放、重连和回收。

## 业务需求与实现范围

| 需求 | 业务合同 | 本次实现与验证 |
| --- | --- | --- |
| 物联网与 Relay 共存 | 原物联网地址、API 和 SP 会话行为保持兼容，新增机制按配置选用 | 精确 vhost/app 加可选 streamPrefix 识别，默认关闭。物联网生命周期与回放有独立回归 |
| 首次播放 | 播放原摄像头链接即可拉起，HLS 调用方无需增加 token 或先创建任务 | 匹配范围后进入原 HLS 访问控制及缺流等待，自动发现和拉流 |
| 管理 API | 新名称为 addRelayProxy、delRelayProxy，原调用方可继续工作 | 两个旧 URL 保留别名。query/form/JSON 复用现有 API 框架 |
| 发现与登录 | 业务 key 映射到设备和 relay，服务完成私有登录 | 从 ESD 取得 deviceId/token/unifiedId 和中继，使用 streamproxy 的 protobuf 消息 |
| 流身份 | 多次访问同一 vhost/app/stream 复用任务 | 共用代理表。迟到的失败或关闭只删除原对象 |
| 原生媒体 | 接收 H.264、H.265、AAC、G711A | 已接入 Track/Frame 和现有多协议封装 |
| 转码 | 根据配置完成 H.265→H.264、等比缩放、G711A→AAC | 复用通用转码，以媒体分片实际解码及输出格式验收 |
| 故障与回收 | 自动重连、显式删除、无人观看后释放；下一次原链接仍可使用 | 重连重新查询 ESD，删除返回 data.flag，保留正在等待首个 HLS 索引的请求 |
| 从源码构建 | Relay 和 protobuf 编译进项目，列出外部依赖以供决定 | protobuf-lite 随库编译。共享编解码库和既有依赖见构建清单 |
| 原业务对照 | 同一真实设备、原链接与原调用在两个版本得到相容结果 | 本次已用本地协议对端测试；真实业务验收待执行 |

配置和链接足以区分两类入口。软件层使用 Discovery、Demuxer、RelayPlayer 和通用 FrameTranscoder，复用现有 PlayerProxy 发布媒体。Relay 业务无需新增编译宏。编解码后端仍遵循现有 ENABLE_FFMPEG 构建能力，转码开关在运行时读取。

本次提交聚焦 Relay 直播接入。原版本对比中列出的标准管理 API 差异，包括帧/GOP 统计及 RTP 的 only_audio、recv_stream_id、close_delay_ms，属于整体替换验收的后续工作。真实 ESD、现场摄像头及原客户端实际调用仍须对照验证。

[接口、软件结构与测试入口](../relay/README.md) · [外部依赖清单](../relay/dependencies.md)
