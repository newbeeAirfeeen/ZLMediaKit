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

    auto onRes = [this, full_url](const string &err, const ProtocolOption &option) {
        if (!err.empty()) {
            sendRtspResponse("401 Unauthorized", { "Content-Type", "text/plain" }, err);
            shutdown(SockException(Err_shutdown, StrPrinter << "401 Unauthorized:" << err));
            return;
        }
        assert(!base_type::_push_src);
        auto src = MediaSource::find(RTSP_SCHEMA, _media_info._vhost, _media_info._app, _media_info._streamid);
        auto push_failed = (bool)src;
        while (src) {
            //尝试断连后继续推流
            auto rtsp_src = dynamic_pointer_cast<RtspMediaSourceImp>(src);
            if (!rtsp_src) {
                //源不是rtsp推流产生的
                DebugL << "ANNOUNCE: push src is not rtsp:" << _media_info.shortUrl() << endl;
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



