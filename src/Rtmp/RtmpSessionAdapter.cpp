#include "RtmpSessionAdapter.h"
#include "Common/config.h"
using namespace std;
using namespace toolkit;
namespace mediakit {
RtmpSessionAdapter::~RtmpSessionAdapter() {
    if (_player_meta) { _player_meta->playerLeft(getIdentifier()); }
}
void RtmpSessionAdapter::onConnectProperties(const AMFValue &params) {
    GET_CONFIG(bool, enabled, Playback::kControlEnable);
    bool has_lifecycle = false;
    params.object_for_each([&](const string &key, const AMFValue &) {
        if (key == "streamLifecycle") { has_lifecycle = true; }
    });
    SessionMeta::Snapshot lifecycle, control;
    auto observed = SessionMeta::parseLifecycle(params, lifecycle);
    _invalid_lifecycle = has_lifecycle && !observed;
    _controlled = enabled && SessionMeta::parseConnect(params, control);
    _candidate = _controlled ? control : lifecycle;
    if (observed && _controlled) {
        _invalid_lifecycle = _invalid_lifecycle || lifecycle.mode != "playback" || lifecycle.epoch != control.epoch;
        _candidate.observed = true;
        _candidate.task_instance = lifecycle.task_instance;
    }
    _managed = observed || _controlled;
}
void RtmpSessionAdapter::onPublishOptions(ProtocolOption &option) {
    if (_controlled) {
        // 暂停经过的真实时间不能被写入媒体时间轴。
        option.modify_stamp = false;
    }
}
bool RtmpSessionAdapter::onPublished(const RtmpMediaSourceImp::Ptr &src) {
    if (_invalid_lifecycle) { return false; }
    auto meta = src->sessionMeta();
    if (meta) {
        if (!_managed || !meta->bind(_candidate)) { return false; }
    } else if (_managed) {
        // 已初始化的普通源不能在续推时变为受控源，否则会沿用旧时间戳策略。
        if (src->getRing()) { return false; }
        meta = make_shared<SessionMeta>(mediaInfo(), _candidate, getPoller());
        src->setSessionMeta(meta);
    }
    return true;
}
bool RtmpSessionAdapter::onDataMessage(const string &type, AMFDecoder &dec) {
    if (type != "onPlaybackState") { return false; }
    auto src = publishSource();
    if (src && src->sessionMeta()) { src->sessionMeta()->update(dec.load<AMFValue>()); }
    return true;
}
bool RtmpSessionAdapter::shouldAutoResumeSource() const {
    auto meta = SessionMeta::find(mediaInfo());
    return !meta || !meta->isControlled();
}
bool RtmpSessionAdapter::isPlaybackStream() const {
    auto meta = SessionMeta::find(mediaInfo());
    return meta && meta->snapshot().mode == "playback";
}
bool RtmpSessionAdapter::allowContinuePush() const {
    auto src = publishSource();
    return !src || !src->sessionMeta() || !src->sessionMeta()->isPaused();
}
void RtmpSessionAdapter::onError(const SockException &err) {
    _player_closed = true;
    if (_player_meta) { _player_meta->playerLeft(getIdentifier()); }
    auto src = publishSource();
    if (src && src->sessionMeta()) {
        auto meta = src->sessionMeta();
        auto s = meta->snapshot();
        meta->disconnected();
        InfoL << "PLAYBACK_STATE event=publisher_disconnect session_id=" << s.session_id
              << " target_state=" << (s.paused ? "paused" : "playing")
              << " reason=" << (s.paused ? "stream_lost_while_paused" : "publisher_disconnected");
    }
    // base 在暂停时直接释放源，不进入 continue_push_ms 延迟保留路径。
    RtmpSession::onError(err);
}
void RtmpSessionAdapter::onPlayerAttached() {
    if (_player_closed) { return; }
    auto meta = SessionMeta::find(mediaInfo());
    if (_player_meta && _player_meta != meta) { _player_meta->playerLeft(getIdentifier()); }
    _player_meta = meta;
    if (meta) { meta->playerJoined(getIdentifier(), "rtmp"); }
}
void RtmpSessionAdapter::closePlayerConnection(const char *command, const char *reason) {
    if (_player_closed) { return; }
    _player_closed = true;
    auto meta = _player_meta ? _player_meta : SessionMeta::find(mediaInfo());
    InfoL << "STREAM_POLICY event=player_close_request session_id=" << (meta ? meta->snapshot().session_id : "-")
          << " stream=" << mediaInfo().shortUrl() << " player_id=" << getIdentifier()
          << " protocol=rtmp command=" << command << " reason=" << reason;
    // 只断开此播放器，不关闭媒体源。onError 上报离开，SP 按 policy 决定任务是否结束。
    // playerLeft 自身幂等，析构时的兜底通知不会重复增加离开计数。
    shutdown(SockException(Err_shutdown, reason));
}
void RtmpSessionAdapter::onCmd_closeStream(AMFDecoder &dec) {
    if (publishSource()) { RtmpSession::onCmd_closeStream(dec); return; }
    closePlayerConnection("closeStream", "client_close_stream");
}
void RtmpSessionAdapter::onCmd_deleteStream(AMFDecoder &dec) {
    if (publishSource()) { RtmpSession::onCmd_deleteStream(dec); return; }
    // 播放器退出不发送属于发布端的 NetStream.Unpublish.Success。
    closePlayerConnection("deleteStream", "client_delete_stream");
}
void RtmpSessionAdapter::onCmd_pause(AMFDecoder &dec) {
    auto meta = _player_meta ? _player_meta : SessionMeta::find(mediaInfo());
    if (!meta || !meta->isControlled()) { RtmpSession::onCmd_pause(dec); return; }
    dec.load<AMFValue>();
    bool paused = dec.load<bool>();
    auto request_id = requestId();
    auto stream_id = _now_stream_index;
    if (!_player_meta || !playSource() || stream_id != STREAM_MEDIA) {
        replyControl(paused, request_id, stream_id, {40904, "not_ready"}); return;
    }
    Json::Value context;
    context["transaction_id"] = request_id;
    context["message_stream_id"] = stream_id;
    weak_ptr<RtmpSessionAdapter> weak_self = dynamic_pointer_cast<RtmpSessionAdapter>(shared_from_this());
    meta->control(paused, getIdentifier(), "rtmp", context, getPoller(),
        [weak_self, paused, request_id, stream_id](const SessionMeta::Result &result) {
            if (auto self = weak_self.lock()) { self->replyControl(paused, request_id, stream_id, result); }
        });
}
bool RtmpSessionAdapter::rejectUnsupported() {
    auto meta = _player_meta ? _player_meta : SessionMeta::find(mediaInfo());
    if (!meta || !meta->isControlled()) { return false; }
    replyControl(false, requestId(), _now_stream_index, {50101, "unsupported"});
    return true;
}
void RtmpSessionAdapter::onCmd_play(AMFDecoder &dec) {
    if (_player_closed) { return; }
    if (_player_meta && rejectUnsupported()) { return; }
    RtmpSession::onCmd_play(dec);
}
void RtmpSessionAdapter::onCmd_play2(AMFDecoder &dec) {
    if (_player_closed) { return; }
    if (!rejectUnsupported()) { RtmpSession::onCmd_play2(dec); }
}
void RtmpSessionAdapter::onCmd_seek(AMFDecoder &dec) {
    if (!rejectUnsupported()) { RtmpSession::onCmd_seek(dec); }
}
void RtmpSessionAdapter::onCmd_playCtrl(AMFDecoder &dec) {
    if (!rejectUnsupported()) { RtmpSession::onCmd_playCtrl(dec); }
}
} // namespace mediakit
