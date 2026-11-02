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
    _controlled = enabled && SessionMeta::parseConnect(params, _candidate);
}
void RtmpSessionAdapter::onPublishOptions(ProtocolOption &option) {
    if (_controlled) {
        // 暂停经过的真实时间不能被写入媒体时间轴。
        option.modify_stamp = false;
    }
}
bool RtmpSessionAdapter::onPublished(const RtmpMediaSourceImp::Ptr &src) {
    auto meta = src->sessionMeta();
    if (meta) {
        if (!_controlled || !meta->bind(_candidate)) { return false; }
    } else if (_controlled) {
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
    return !SessionMeta::find(mediaInfo());
}
bool RtmpSessionAdapter::allowContinuePush() const {
    auto src = publishSource();
    return !src || !src->sessionMeta() || !src->sessionMeta()->isPaused();
}
void RtmpSessionAdapter::onError(const SockException &err) {
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
    auto meta = SessionMeta::find(mediaInfo());
    if (_player_meta && _player_meta != meta) { _player_meta->playerLeft(getIdentifier()); }
    _player_meta = meta;
    if (meta) { meta->playerJoined(getIdentifier()); }
}
void RtmpSessionAdapter::onCmd_pause(AMFDecoder &dec) {
    auto meta = _player_meta ? _player_meta : SessionMeta::find(mediaInfo());
    if (!meta) { RtmpSession::onCmd_pause(dec); return; }
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
    if (!_player_meta && !SessionMeta::find(mediaInfo())) { return false; }
    replyControl(false, requestId(), _now_stream_index, {50101, "unsupported"});
    return true;
}
void RtmpSessionAdapter::onCmd_play(AMFDecoder &dec) {
    if (_player_meta && rejectUnsupported()) { return; }
    RtmpSession::onCmd_play(dec);
}
void RtmpSessionAdapter::onCmd_play2(AMFDecoder &dec) {
    if (!rejectUnsupported()) { RtmpSession::onCmd_play2(dec); }
}
void RtmpSessionAdapter::onCmd_seek(AMFDecoder &dec) {
    if (!rejectUnsupported()) { RtmpSession::onCmd_seek(dec); }
}
void RtmpSessionAdapter::onCmd_playCtrl(AMFDecoder &dec) {
    if (!rejectUnsupported()) { RtmpSession::onCmd_playCtrl(dec); }
}
} // namespace mediakit
