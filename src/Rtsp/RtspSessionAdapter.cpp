//
// Created by shenhao on 2025/9/15.
//
#include "RtspSessionAdapter.h"
#include "Util/onceToken.h"
#include "Rtcp/RtcpContext.h"
#include <vector>
#include <algorithm>
using namespace mediakit;
using namespace std;
using namespace toolkit;
// Jenkins 使用 GCC 4.8.5，其 std::regex 不完整；逐字符校验，避免编译成功但匹配恒失败。
// 仅接受开放终点的零 NPT（秒或 hh:mm:ss），不用 atof，非法字符不能被当成 0。
static bool isOpenZeroRange(const string &range) {
    if (range.size() < 6 || (range[0] != 'n' && range[0] != 'N') ||
        (range[1] != 'p' && range[1] != 'P') || (range[2] != 't' && range[2] != 'T') || range[3] != '=') {
        return false;
    }
    size_t pos = 4;
    auto zeros = [&]() {
        const auto begin = pos;
        while (pos < range.size() && range[pos] == '0') { ++pos; }
        return pos - begin;
    };
    if (!zeros()) { return false; }
    if (pos < range.size() && range[pos] == ':') {
        for (int field = 0; field < 2; ++field) {
            if (pos == range.size() || range[pos++] != ':') { return false; }
            const auto digits = zeros();
            if (digits < 1 || digits > 2) { return false; }
        }
    }
    if (pos < range.size() && range[pos] == '.') { ++pos; zeros(); }
    return pos + 1 == range.size() && range[pos] == '-';
}

static auto make_sdp() -> std::string {
    GET_CONFIG(uint32_t, pt, RtpProxy::kG711APT);
    const char* fmt = "v=0\r\n"
                      "o=- 0 0 IN IP4 0.0.0.0\r\n"
                      "s=NMSL\r\n"
                      "c=IN IP4 0.0.0.0\r\n"
                      "a=range:npt=now-\r\n"
                      "m=audio 0 RTP/AVP %d\r\n"
                      "a=control:trackID=1\r\n"
                      "a=rtpmap:%d PCMA/8000/1\r\n";
    char buf[1024] = {0};
    snprintf(buf, sizeof(buf) - 1, fmt, pt, pt);
    return buf;
}
static auto make_sdp_track() -> std::vector<SdpTrack::Ptr> {
    SdpParser sdp_parser(make_sdp());
    return sdp_parser.getAvailableTrack();
}

RtspSessionAdapter::RtspSessionAdapter(const toolkit::Socket::Ptr &sock):RtspSession(sock){}
void RtspSessionAdapter::onWholeRtspPacket(Parser &parser) {
    auto& method = parser.Method();
    // 需要适配
    if (method == "DESCRIBE") {
        auto require = parser["Require"];
        if (require == "www.onvif.org/ver20/backchannel") {
            _is_adapter_mode = true;
        }
    }
    onWholeRtspPacket_l(parser);
}

void RtspSessionAdapter::onWholeRtspPacket_l(Parser &parser) {
    if (!_is_adapter_mode) {
        return base_type::onWholeRtspPacket(parser);
    }
    auto method = parser.Method(); //提取出请求命令字
    base_type::_cseq = atoi(parser["CSeq"].data());
    if (_content_base.empty() && method != "GET") {
        base_type::_content_base = parser.Url();
        base_type::_media_info.parse(parser.FullUrl());
        base_type::_media_info._schema = RTSP_SCHEMA;
    }
    static std::vector<std::string> METHOD_NOT_ALLOWED = {"ANNOUNCE", "RECORD"};
    auto not_allowed_it = std::find_if(METHOD_NOT_ALLOWED.begin(), METHOD_NOT_ALLOWED.end(), [&](const std::string& target) {
        return target == method;
    });
    if (not_allowed_it != METHOD_NOT_ALLOWED.end()) {
        beginRequest(parser);
        sendRtspResponse("405 Forbidden", {"Connection","Close"}, "Adapter Rtsp Method Not Allowed");
        return;
    }

    using rtsp_request_handler = void (RtspSessionAdapter::*)(const Parser &parser);
    static unordered_map<string, rtsp_request_handler> s_cmd_functions;
    static onceToken token([]() {
        s_cmd_functions.emplace("DESCRIBE", &RtspSessionAdapter::handleReq_Describe_l);
        s_cmd_functions.emplace("SETUP", &RtspSessionAdapter::handleReq_Setup_l);
        s_cmd_functions.emplace("PLAY", &RtspSessionAdapter::handleReq_Play_l);
    });

    auto it = s_cmd_functions.find(method);
    if (it == s_cmd_functions.end()) {
        return base_type::onWholeRtspPacket(parser);
    }

    beginRequest(parser);
    (this->*(it->second))(parser);
    parser.Clear();
}

void RtspSessionAdapter::handleReq_Describe_l(const Parser &parser) {
    auto full_url = parser.FullUrl();
    _content_base = full_url;
    if (end_with(full_url, ".sdp")) {
        //去除.sdp后缀，防止EasyDarwin推流器强制添加.sdp后缀
        full_url = full_url.substr(0, full_url.length() - 4);
        _media_info.parse(full_url);
    }

    if (_media_info._app.empty() || _media_info._streamid.empty()) {
        //推流rtsp url必须最少两级(rtsp://host/app/stream_id)，不允许莫名其妙的推流url
        static constexpr auto err = "rtsp推流url非法,最少确保两级rtsp url";
        sendRtspResponse("403 Forbidden", {"Content-Type", "text/plain"}, err);
        throw SockException(Err_shutdown, StrPrinter << err << ":" << full_url);
    }

    auto request = _request_context;
    auto onRes = [this, full_url, request](const string &err, const ProtocolOption &option) {
        auto scope = useRequest(request);
        if (!err.empty()) {
            sendRtspResponse("401 Unauthorized", { "Content-Type", "text/plain" }, err);
            shutdown(SockException(Err_shutdown, StrPrinter << "401 Unauthorized:" << err));
            return;
        }
        assert(!base_type::_push_src);
        auto src = MediaSource::find(RTSP_SCHEMA, _media_info._vhost, _media_info._app, _media_info._streamid);
        auto push_failed = (bool)src;
        while (src) {
            DebugL << "Found MediaSource type: " << typeid(*src).name() << endl;
            //尝试断连后继续推流
            auto rtsp_src = dynamic_pointer_cast<RtspMediaSourceImp>(src);
            if (!rtsp_src) {
                //源不是rtsp推流产生的
                DebugL << "dynamic_pointer_cast failed, actual type: "
                       << typeid(*src).name()
                       << ", expected: RtspMediaSourceImp" << endl;
                break;
            }
            auto ownership = rtsp_src->getOwnership();
            if (!ownership) {
                //获取推流源所有权失败
                DebugL << "ANNOUNCE: get push src ownership failed:" << _media_info.shortUrl() << endl;
                break;
            }
            _push_src = std::move(rtsp_src);
            _push_src_ownership = std::move(ownership);
            push_failed = false;
            break;
        }
        if (push_failed) {
            sendRtspResponse("406 Not Acceptable", { "Content-Type", "text/plain" }, "Already publishing.");
            string err = StrPrinter << "ANNOUNCE: Already publishing:" << _media_info.shortUrl() << endl;
            throw SockException(Err_shutdown, err);
        }
        // 这里需要自己创建SDP_TRACK
        base_type::_sessionid = makeRandStr(12);
        this->_session_id_saved = base_type::_sessionid;
        base_type::_sdp_track = make_sdp_track();
        if (_sdp_track.empty()) {
            // sdp无效
            static constexpr auto err = "无有效track";
            sendRtspResponse("403 Forbidden", { "Content-Type", "text/plain" }, err);
            shutdown(SockException(Err_shutdown, StrPrinter << err << ":" << full_url));
            return;
        }

        base_type::_rtcp_context.clear();
        for (auto &track : base_type::_sdp_track) {
            base_type::_rtcp_context.emplace_back(std::make_shared<RtcpContextForRecv>());
        }
        if (!base_type::_push_src) {
            base_type::_push_src = std::make_shared<RtspMediaSourceImp>(_media_info._vhost, _media_info._app, _media_info._streamid);
            //获取所有权
            base_type::_push_src_ownership = _push_src->getOwnership();
            base_type::_push_src->setProtocolOption(option);
            base_type::_push_src->setSdp(make_sdp());
        }
        base_type::_push_src->setListener(dynamic_pointer_cast<MediaSourceEvent>(shared_from_this()));
        base_type::_continue_push_ms = option.continue_push_ms;
        // 发送额外的sdp适配
        sendRtspResponse("200 OK", {"Cache-Control", "must-revalidate"},make_sdp());
    };

    weak_ptr<RtspSessionAdapter> weak_self = dynamic_pointer_cast<RtspSessionAdapter>(shared_from_this());
    Broadcast::PublishAuthInvoker invoker = [weak_self, onRes](const string &err, const ProtocolOption &option) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        strong_self->async([weak_self, onRes, err, option]() {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }
            onRes(err, option);
        });
    };

    auto flag = NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastMediaPublish, MediaOriginType::rtsp_push, _media_info, invoker, static_cast<SockInfo &>(*this));
    if (!flag) {
        //该事件无人监听,默认不鉴权
        onRes("", ProtocolOption());
    }
}
void RtspSessionAdapter::onError(const toolkit::SockException &err) {
    _player_closed = true;
    // 防止断流续推
    if (_player_meta) { _player_meta->playerLeft(getIdentifier()); }
    if (_is_adapter_mode) { _push_src = nullptr; }
    base_type::onError(err);
}
void RtspSessionAdapter::handleReq_Setup_l(const Parser &parser) {
    auto parser_ = const_cast<Parser&>(parser);
    // Transport: RTP/AVP/TCP;unicast;interleaved=2-3;mode=play -> Transport: RTP/AVP/TCP;unicast;interleaved=2-3;mode=record
    auto& transport = const_cast<string&>(parser_["Transport"]);
    // 我需要在transport 中找到mode=play并替换成mode=record
    // 查找 "mode=play"
    size_t pos = transport.find("mode=play");
    if (pos != string::npos) {
        // 替换为 "mode=record"
        transport.replace(pos, 9, "mode=record"); // "mode=play" 长度为 9
    }
    base_type::handleReq_Setup(parser);
}
void RtspSessionAdapter::handleReq_Play_l(const Parser &parser) {
    auto& method = const_cast<string&>(parser.Method());
    method = "RECORD";
    auto& session_id = const_cast<string&>(parser["Session"]);
    trim(session_id);
    if (session_id.empty()) {
        WarnL << "Session is empty, revise it with: " << this->_session_id_saved;
        const_cast<string&>(parser["Session"]) = this->_session_id_saved;
    }
    base_type::handleReq_RECORD(parser);
}




RtspSessionAdapter::~RtspSessionAdapter() {
    if (_player_meta) { _player_meta->playerLeft(getIdentifier()); }
}
bool RtspSessionAdapter::shouldAutoResumeSource() const {
    auto meta = SessionMeta::find(_media_info);
    // 首次默认起点只接入当前媒体，直播也不对共享源触发 seek / resume。
    if (!_initial_zero_range.empty()) { return false; }
    return _is_adapter_mode || !meta || !meta->isControlled();
}
bool RtspSessionAdapter::isPlaybackPaused() const {
    return _player_meta && _player_meta->isPaused();
}
void RtspSessionAdapter::onPlayerAttached() {
    if (_is_adapter_mode || _player_closed) { return; }
    // 只有基类完成正常 PLAY / reader 接入后才消耗首次机会，失败请求不计入。
    _play_started = true;
    auto meta = SessionMeta::find(_media_info);
    if (_player_meta && _player_meta != meta) { _player_meta->playerLeft(getIdentifier()); }
    _player_meta = meta;
    if (meta) {
        meta->playerJoined(getIdentifier(), "rtsp");
    }
    if (!_initial_zero_range.empty()) {
        InfoL << "STREAM_POLICY event=initial_play_range session_id=" << (meta ? meta->snapshot().session_id : "-")
              << " stream=" << _media_info.shortUrl() << " player_id=" << getIdentifier()
              << " protocol=rtsp range=" << _initial_zero_range << " action=attach";
    }
}
void RtspSessionAdapter::handleReq_Play(const Parser &parser) {
    auto meta = _is_adapter_mode ? nullptr : SessionMeta::find(_media_info);
    auto scale = parser["Scale"];
    auto range = parser["Range"];
    trim(scale); trim(range);
    const bool default_scale = scale.empty() || scale == "1" || scale == "1.0";
    const bool initial_zero = !_play_started && default_scale && isOpenZeroRange(range);
    if (meta && meta->isControlled()) {
        if (_sessionid.empty() || parser["Session"] != _sessionid || _play_src.expired()) {
            sendRtspResponse("454 Session Not Found"); return;
        }
        if (_close_after_response || none_of(_sdp_track.begin(), _sdp_track.end(),
                                           [](const SdpTrack::Ptr &track) { return track->_inited; })) {
            sendRtspResponse("455 Method Not Valid in This State"); return;
        }
        // 仅首次默认起点兼容零值。后续零起点仍是 seek，不能误发 resume。
        if (!default_scale || (!range.empty() && range != "npt=now-" && !initial_zero)) {
            // 记录实际拒绝条件，方便区分 Range 解析、后续 seek 和倍速请求；不输出带 token 的 URL。
            InfoL << "PLAYBACK_STATE event=play_rejected session_id=" << meta->snapshot().session_id
                  << " stream=" << _media_info.shortUrl() << " player_id=" << getIdentifier()
                  << " protocol=rtsp cseq=" << _request_context.cseq << " first_play=" << !_play_started
                  << " range=\"" << range << "\" scale=\"" << scale << "\" status=501 reason="
                  << (!default_scale ? "unsupported_scale" : "unsupported_range");
            sendRtspResponse("501 Not Implemented"); return;
        }
        if (_play_started) { replayControl(false, parser); return; }
        _player_meta = meta;
    }
    // 仍走基类完整的状态校验、200/RTP-Info 响应和 reader 接入，不直接回复假成功。
    // 作用域结束即恢复普通源原有控制行为，包括后续 PLAY 和非默认倍速请求。
    onceToken range_scope([&]() { _initial_zero_range = initial_zero ? range : ""; },
                          [&]() { _initial_zero_range.clear(); });
    base_type::handleReq_Play(parser);
}
void RtspSessionAdapter::handleReq_Pause(const Parser &parser) {
    auto meta = _player_meta ? _player_meta : SessionMeta::find(_media_info);
    if (_is_adapter_mode || !meta || !meta->isControlled()) {
        base_type::handleReq_Pause(parser); return;
    }
    replayControl(true, parser);
}
void RtspSessionAdapter::replayControl(bool paused, const Parser &parser) {
    if (_sessionid.empty() || parser["Session"] != _sessionid) {
        sendRtspResponse("454 Session Not Found"); return;
    }
    if (!_player_meta || !_play_started || _play_src.expired() || _close_after_response) {
        sendRtspResponse("455 Method Not Valid in This State"); return;
    }
    auto request = _request_context;
    Json::Value context;
    context["cseq"] = request.cseq;
    weak_ptr<RtspSessionAdapter> weak_self = dynamic_pointer_cast<RtspSessionAdapter>(shared_from_this());
    _player_meta->control(paused, getIdentifier(), "rtsp", context, getPoller(),
        [weak_self, request](const SessionMeta::Result &result) {
            auto self = weak_self.lock();
            if (!self) { return; }
            auto scope = self->useRequest(request);
            const char *status = "500 Internal Server Error";
            if (result.accepted()) { status = "200 OK"; }
            else if (result.code == 40401 || result.code == 41001) { status = "454 Session Not Found"; }
            else if (result.code >= 40901 && result.code <= 40904) { status = "455 Method Not Valid in This State"; }
            else if (result.code == 50101) { status = "501 Not Implemented"; }
            // 保持同一个 reader；恢复不重新 attach，因此不会再次 flush GOP。
            self->sendRtspResponse(status);
        });
}
void RtspSessionAdapter::handleReq_Teardown(const Parser &parser) {
    if (!_player_meta) { base_type::handleReq_Teardown(parser); return; }
    if (parser["Session"] != _sessionid) { sendRtspResponse("454 Session Not Found"); return; }
    _player_closed = true;
    _player_meta->playerLeft(getIdentifier());
    _play_reader = nullptr;
    // 上报一次离开事实，是否结束任务由 SP 决定；响应仍按原请求顺序发送。
    _close_after_response = _request_context.ordinal;
    sendRtspResponse("200 OK");
}
