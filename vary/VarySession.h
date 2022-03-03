//
// Created by 沈昊 on 2022/2/22.
//
#if 0
#ifndef ZLMEDIAKIT_VARYSESSION_H
#define ZLMEDIAKIT_VARYSESSION_H
#include "RtspMuxerMediaSource.h"
#include <Rtsp/RtspPlayerImp.h>
#include <Rtsp/RtspPusher.h>
#include <memory>
#include <mutex>
#include <VaryFrameWriter.h>
#include <FFmpeg/Frame.hpp>
#include <jsoncpp/json.h>

class VarySession : public std::enable_shared_from_this<VarySession>{
public:
    using Ptr = std::shared_ptr<VarySession>;
    void Excute(const Json::Value& val);
public:
    VarySession();
    ~VarySession();
public:
    /* 成功后回调 */
    void setOnSuccess(const std::function<void()>& ff);
    void setExceptionHandler(std::function<void(const std::string&)>&& f){ this->on_exception = std::move(f);}
private:
    //当所有track初始化好后
    void OnPlayerTrackInited(const std::vector<mediakit::Track::Ptr>& tracks);
    void OnVaryVideo(const CoderContext& context, FFmpeg::Frame::Ptr ptr);
    void OnAudio(const mediakit::Frame::Ptr &frame);


    void OnVaryVideo2(const mediakit::Frame::Ptr&);
private:
    /* 开启流的转码 */
    void vary(const Json::Value& );
    void vary_l(const Json::Value& val);
    /* 关闭流的转码 */
    void stop(const Json::Value&);
    void stop_l();
private:
    void readyPush();
    mediakit::Track::Ptr getTrackIndex(mediakit::TrackType);
private:
    RtspMuxerMediaSource::Ptr source;
    //读取器
    typename mediakit::RtpRing::RingType::RingReader::Ptr _reader;
    mediakit::RtspPlayerImp::Ptr player;
    mediakit::RtspPusherImp::Ptr pushser;
    /* 转码通道 */
    //VaryFrameWriter::Ptr vary_writer;
    /* 不转码的通道 */
    VaryDefaultWriter::Ptr default_write;
    std::once_flag flag;
    std::once_flag flag2;
    std::string session_id;
    Json::Value param;
    std::function<void(const std::string&)> on_exception;
    std::function<void()> _success_func;
    //所有转码后的通道
    std::vector<mediakit::Track::Ptr> vary_tracks;
    //std::shared_ptr<VaryFrameWriterDispatcher> _dispatcher;
};


#endif // ZLMEDIAKIT_VARYSESSION_H
#endif