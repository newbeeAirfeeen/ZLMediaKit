# 回放控制：ZLM 端实现说明

本分支从 `release` 的 `29e3696b` 开始实现。ZLM 接收标准 RTMP/RTSP 暂停与恢复请求，通过发布者提供的 HTTP hook 通知 streamproxy；streamproxy 负责云存/SD 卡的实际控制及任务期限。本分支不修改 streamproxy。

## Review 顺序

1. `src/Common/SessionMeta.h/.cpp`：两种播放协议共享的状态镜像、请求序号、HTTP 请求和响应版本校验。序号均使用十进制字符串。控制请求分配及 HTTP 完成在固定 owner poller 上处理，发布绑定/断开通过同一把锁同步。
2. `src/Rtmp/RtmpSessionAdapter.h/.cpp`：读取 connect 属性、发布后绑定、RTMP 暂停恢复、首次接入与恢复区分、标准 StreamIsRecorded 标识。业务放在 Adapter，基类仅提供扩展点和协议回复助手。
3. `src/Rtsp/RtspSessionAdapter.h/.cpp`：复用现有 Adapter，保留 ONVIF backchannel；首次 PLAY 仅接入，后续 PLAY 才发 resume，PAUSE 发 pause。`RtspSession` 保存原请求 CSeq/接收次序，包含异步鉴权及 GET_PARAMETER 在内的响应有序输出。
4. `server/AuthCenter.h/.cpp` 与 `RtmpMediaSourceImp`：路由在取得媒体源所有权后绑定，包含 vhost。源持有路由 lease，旧 owner 清理不能删除新 owner 的路由；注册表不强持有媒体源。
5. `RtmpSession::onError`、`RtmpMediaSourceImp::cancelReconnectRetention`：暂停断流立即释放，不进入续推窗口；非暂停断流保留原重推策略。新发布成功后解除旧 timer 对源的引用，防止下一次暂停断流仍被旧 timer 留住。
6. `server/main.cpp`、`api/source/mk_common.cpp`：普通、SSL 和 C API 的 RTMP/RTSP 服务统一使用 Adapter。

## 开启与对接

默认关闭，先完成 streamproxy 的协议对接再开启：

```ini
[playback]
control_enable=1

[protocol]
enable_rtsp=1
```

`enable_rtsp=1` 用于需要 RTSP 播放的场景。未改动工作区原有 `conf/config.ini`。受控回放发布时强制 `modify_stamp=false`，保留媒体时间戳；普通流维持原配置。

发布者 RTMP `connect` 属性示例：

```json
{
  "app": "live",
  "tcUrl": "rtmp://zlm/live",
  "hookUrl": "http://streamproxy/streamproxy/api/v1/task/stream_state",
  "sessionId": "task-123",
  "playbackControl": {
    "version": 1,
    "mode": "playback",
    "source_type": "cloud",
    "capabilities": {"pause": true, "resume": true},
    "target_state": "playing",
    "state_version": "0",
    "publish_epoch": "1"
  }
}
```

SD 卡使用 `source_type=sdcard`，ZLM 的控制流程相同。`all_track_ready` 后才允许控制。播放器的会话/源未就绪时直接返回协议错误，不发送假成功。

hook 请求携带 `type=control`、`session_id`、`publish_epoch`、`request_seq`、`action=pause/resume`、`player_id`、`protocol`、`params` 和原协议 `request_context`。响应约定：

```json
{
  "code": 0,
  "result": "accepted",
  "session_id": "task-123",
  "publish_epoch": "1",
  "request_seq": "1",
  "current": {
    "publish_epoch": "1",
    "last_accepted_seq": "1",
    "state_version": "1",
    "target_state": "paused"
  }
}
```

成功结果允许 `accepted/unchanged/duplicate`。错误码沿用设计稿的 40001、40401、40901～40904、41001、50101；ZLM 校验请求回显和当前状态。旧 SP 对未知 hook 返回的单独 `{"code":0}` 不会被当成成功。旧版本快照不能覆盖新状态；迟到的旧成功响应返回 stale，避免 RTMP 又发送过时的 Pause.Notify。

SP 也可在发布连接发送 AMF Data Message `onPlaybackState`，后接包含 `session_id`、`publish_epoch`、`state_version`、`target_state` 的对象。只有当前发布者、当前代次且更高版本的状态才会应用；该消息不会再次转成 control hook。

## 生命周期与日志

- 正常暂停保留连接和 reader，允许有限尾帧；恢复不重新 attach、不再次 flush GOP。
- 首次播放不恢复上游，第二播放器共享任务状态并记录 `event=second_player_join`。
- 播放器退出只清理自身；受控回放不被 record app 或通用无人观看策略自动关闭。
- 暂停中发布连接实际断开，立即释放媒体源及关联播放器；正常暂停的无音视频不代表断流。
- 发布端任意 RTMP 数据仍刷新连接存活计时。SP 暂停时必须发送 RTMP ping，间隔小于有效 `rtmp.keepAliveSecond`；该 SP 改动不在本分支。
- RTSP UDP 暂停时不因媒体反馈静默关闭；恢复后重新计算存活等待时间。
- 日志统一前缀 `PLAYBACK_STATE`，可按 `session_id`、`request_seq`、`state_version`、`event` grep。

播放器侧私有状态包、RTCP APP、seek、倍速和播放器特定适配不在本次范围。当前使用标准控制响应；不增加 relay 执行失败补偿策略。

## 验证

新增 `tests/test_playback_control.cpp`：序号精度与溢出、乱序/过期响应、旧代次、暂停断流终止、旧路由 lease 清理、vhost 隔离、旧续推 timer 引用释放。

新增 `tests/test_playback_control.py`：本地模拟 SP hook，生成短 FLV 测试媒体，通过真实 RTMP/RTSP 连接验证共享序号、HTTP 响应倒序、异步 DESCRIBE 与 OPTIONS、初始接入、暂停断流即时清理、普通流与 ONVIF backchannel。依赖 Python 3 和带 libx264/AAC 的 ffmpeg。临时端口只绑定 localhost，测试结束清理进程及文件。

本机验证构建使用系统 Clang、OpenSSL 3、独立 `/tmp/zlm-playback-build-apple` 目录和 `PlaybackReview` 输出目录，构建目标为 `MediaServer`、`mk_api`、`test_playback_control`。WebRTC、SRT、Prometheus 和 configctl 未启用。没有修改原有 Debug 构建及用户配置。

```sh
cmake --build /tmp/zlm-playback-build-apple --target MediaServer mk_api test_playback_control -j 6
./release/darwin/PlaybackReview/test_playback_control
python3 tests/test_playback_control.py ./release/darwin/PlaybackReview/MediaServer
```

SSL/C API 入口已通过编译；网络联调覆盖 RTMP 和 RTSP TCP，尚未与真实 streamproxy、设备 relay 或目标播放器做联调。

## RTMP 播放器主动退出

播放器发送 `closeStream` 或 `deleteStream` 后，`RtmpSessionAdapter` 主动关闭当前 TCP 连接，
不向播放器发送发布端的 `NetStream.Unpublish.Success`，也不直接关闭媒体源或发送源级 stop/resume。
直播、回放以及无 SessionMeta 的普通播放器均适用；发布端行为保持原样。

托管任务通过原有 `onError` / `playerLeft` 路径上报一次离开；析构兜底不会重复计数。
SP 按创建时的 policy 决定任务释放：`original` 保留旧回收逻辑，`any_close` 任一退出结束任务，
`all_close` 最后一个播放器退出结束任务。ZLM 不新增 policy 配置。

关闭请求的 INFO 日志可用 `STREAM_POLICY event=player_close_request` 检索：

```text
STREAM_POLICY event=player_close_request session_id=... stream=... player_id=... protocol=rtmp command=closeStream reason=client_close_stream
STREAM_POLICY event=player_close_request session_id=... stream=... player_id=... protocol=rtmp command=deleteStream reason=client_delete_stream
```

无托管元信息时 session_id 为 `-`。该日志表示开始关闭播放器；托管任务后续查
`STREAM_POLICY event=player_leave`，任务是否结束再查 SP 的 `event=release` 和 `event=task_stop`。

本次验证：`MediaServer` 和 `test_playback_control` 构建通过，C++ 状态测试及两个 Python 集成测试通过。
覆盖直播/回放的 2→1 与全部离开、四协议生命周期、离开幂等和关闭日志；覆盖暂停中的两种退出命令、
其他播放器连接保留、普通播放器退出，以及发布端 deleteStream 的原行为。
集成测试使用模拟 SP hook，不代表已完成真实 SP policy 与设备端联调。

```sh
cmake --build /tmp/zlm-stream-policy-build --target MediaServer test_playback_control -j 6
./release/darwin/StreamPolicyReview/test_playback_control
python3 tests/test_playback_control.py ./release/darwin/StreamPolicyReview/MediaServer
python3 tests/test_stream_lifecycle_integration.py ./release/darwin/StreamPolicyReview/MediaServer
```
