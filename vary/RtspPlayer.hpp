//
// Created by 沈昊 on 2022/2/24.
//
#include <Rtsp/RtspPlayer.h>
#ifndef MEDIAKIT_RTSPPLAYER_HPP
#define MEDIAKIT_RTSPPLAYER_HPP
#include "Rtsp/RtspDemuxer.h"
namespace vary{

    class RtspPlayer: public mediakit::RtspPlayer{
    public:
        RtspPlayer(const toolkit::EventPoller::Ptr &poller): mediakit::RtspPlayer(poller){}
    protected:
        //派生类回调函数
        bool onCheckSDP(const std::string &sdp) override;
        void onRecvRTP(RtpPacket::Ptr rtp, const SdpTrack::Ptr &track) override;
    private:
        mediakit::RtspDemuxer::Ptr _demuxer;
    };

}


#endif // MEDIAKIT_RTSPPLAYER_HPP
