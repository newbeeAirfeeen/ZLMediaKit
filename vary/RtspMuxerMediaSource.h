//
// Created by 沈昊 on 2022/2/23.
//

#ifndef ZLMEDIAKIT_RTSPMUXERMEDIASOURCE_H
#define ZLMEDIAKIT_RTSPMUXERMEDIASOURCE_H

#include "Rtsp/RtspMediaSource.h"
#include "Rtsp/RtspMuxer.h"

class RtspMuxerMediaSource : public mediakit::RtspMediaSource{
private:
    static std::string null_stream;
public:
    using Ptr = std::shared_ptr<RtspMuxerMediaSource>;
public:
    RtspMuxerMediaSource();

    bool addTrack(const mediakit::Track::Ptr & track);

    bool inputFrame(const mediakit::Frame::Ptr &frame);

    void setOnTrackReady(const std::function<void()>& f){
        this->on_ready_track = f;
    }
    mediakit::RtpRing::RingType::Ptr getRtpRing() const{
        return  muxer->getRtpRing();
    }

    inline bool hasVideoTrack()const { return _has_video_track;}
    inline bool hasAudioTrack()const { return _has_audio_track;}
private:
    mediakit::RtspMuxer::Ptr muxer;
    std::function<void()> on_ready_track;
    bool _has_video_track = false;
    bool _has_audio_track = false;
};


#endif // ZLMEDIAKIT_RTSPMUXERMEDIASOURCE_H
