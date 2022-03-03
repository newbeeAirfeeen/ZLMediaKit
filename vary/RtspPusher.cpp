//
// Created by 沈昊 on 2022/2/24.
//
#include "RtspPusher.hpp"
#include "Util/logger.h"
namespace vary{
    using namespace toolkit;
    RtspPusher::RtspPusher(const toolkit::EventPoller::Ptr &poller, const mediakit::RtspMuxer::Ptr &muxer):
                                                            mediakit::RtspPusherImp(poller, nullptr){
        _muxer_source = muxer;
    }

    void RtspPusher::sendAnnounce(){
        auto muxer_source = _muxer_source.lock();
        if (!muxer_source) {
            ErrorL << "rtsp复用器的源已经被销毁或者未初始化";
            return;
        }
        //解析sdp
        _sdp_parser.load(muxer_source->getSdp());
        _track_vec = _sdp_parser.getAvailableTrack();
        if (_track_vec.empty()) {
           ErrorL << "无有效的Sdp Track";
        }
        _rtcp_context.clear();
        for (auto &track : _track_vec) {
            _rtcp_context.emplace_back(std::make_shared<mediakit::RtcpContextForSend>());
        }
        _on_res_func = std::bind(&RtspPusher::handleResAnnounce, this, std::placeholders::_1);
        sendRtspRequest("ANNOUNCE", _url, {}, muxer_source->getSdp());
    }

    void RtspPusher::sendRecord(){
        _on_res_func = [this](const mediakit::Parser &parser) {
            auto source = _muxer_source.lock();
            if (!source) {
                ErrorL << "rtsp复用器的源已经被销毁";
            }

            _reader = source->getRtpRing()->attach(getPoller());
            std::weak_ptr<RtspPusher> weak_self = std::static_pointer_cast<RtspPusher>(shared_from_this());
            _reader->setReadCB([weak_self](const mediakit::RtpPacket::Ptr& rtp) {
                auto strong_self = weak_self.lock();
                if (!strong_self) {
                    return;
                }
                //直接发送
                strong_self->send(rtp);
            });
            _reader->setDetachCB([weak_self]() {
                auto strong_self = weak_self.lock();
                if (strong_self) {
                    strong_self->onPublishResult_l(SockException(Err_other, "媒体源被释放"),
                                                   !strong_self->_publish_timer);
                }
            });
            if (_rtp_type != mediakit::Rtsp::RTP_TCP) {
                /////////////////////////心跳/////////////////////////////////
                _beat_timer.reset(new Timer((*this)[mediakit::Client::kBeatIntervalMS].as<int>() / 1000.0f, [weak_self]() {
                    auto strong_self = weak_self.lock();
                    if (!strong_self) {
                        return false;
                    }
                    strong_self->sendOptions();
                    return true;
                }, getPoller()));
            }
            onPublishResult_l(SockException(Err_success, "success"), false);
            //提升发送性能
            setSocketFlags();
        };
        sendRtspRequest("RECORD", _content_base, {"Range", "npt=0.000-"});
    }
};