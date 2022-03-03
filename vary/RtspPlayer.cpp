//
// Created by 沈昊 on 2022/2/24.
//
#include "RtspPlayer.hpp"
namespace vary{
    bool RtspPlayer::onCheckSDP(const std::string &sdp){
        _demuxer = std::make_shared<RtspDemuxer>();
        _demuxer->setTrackListener(this, (*this)[Client::kWaitTrackReady].as<bool>());
        _demuxer->loadSdp(sdp);
    }

    void RtspPlayer::onRecvRTP(RtpPacket::Ptr rtp, const SdpTrack::Ptr &track){
        _demuxer->inputRtp(rtp);
    }
}