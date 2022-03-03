//
// Created by 沈昊 on 2022/2/24.
//

#ifndef MEDIAKIT_RTSPPUSHER_HPP
#define MEDIAKIT_RTSPPUSHER_HPP
#include <Rtsp/RtspMuxer.h>
#include <Rtsp/RtspPusher.h>
namespace vary{

    /* 此客户端不再用GOP缓存，因为仅仅当客户端来使用 */
    class RtspPusher : public mediakit::RtspPusherImp{
    public:
        RtspPusher(const toolkit::EventPoller::Ptr &poller,const mediakit::RtspMuxer::Ptr &muxer);

    protected:
        /* 重写Announce为可以从rtsp_muxer中获取sdp */
        void sendAnnounce() override;
        /* 重写Record为可以从rtsp_muxer中直接发送rtp */
        void sendRecord() override;
    private:
        /* rtsp 复用源 */
        std::weak_ptr<mediakit::RtspMuxer> _muxer_source;
        typename mediakit::RtpRing::RingType::RingReader::Ptr _reader;
    };



}


#endif // MEDIAKIT_RTSPPUSHER_HPP
