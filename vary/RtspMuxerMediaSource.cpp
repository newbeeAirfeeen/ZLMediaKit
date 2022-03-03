//
// Created by 沈昊 on 2022/2/23.
//
#include "RtspMuxerMediaSource.h"
std::string RtspMuxerMediaSource::null_stream;
using namespace mediakit;
RtspMuxerMediaSource::RtspMuxerMediaSource():RtspMediaSource(null_stream, null_stream, null_stream){
    muxer = std::make_shared<RtspMuxer>();
}
bool RtspMuxerMediaSource::addTrack(const Track::Ptr &track) {
    if(!muxer)return false;
    //准备好后才可以添加通道
    bool ret = muxer->addTrack(track);
    if(ret)
    {
        if(!_has_video_track)
            _has_video_track = track->getTrackType() == mediakit::TrackVideo;
        if(!_has_audio_track)
            _has_audio_track = track->getTrackType() == mediakit::TrackAudio;
        if(_has_video_track && _has_audio_track)
        {
            RtspMediaSource::setSdp(muxer->getSdp());
            if(on_ready_track)
                on_ready_track();
        }
    }
    return ret;
}

bool RtspMuxerMediaSource::inputFrame(const mediakit::Frame::Ptr &frame){
    return muxer && muxer->inputFrame(frame);
}
