//
// Created by 沈昊 on 2022/2/22.
//

#ifndef ZLMEDIAKIT_VARYFRAMEWRITER_H
#define ZLMEDIAKIT_VARYFRAMEWRITER_H

#include "AVCodeContext.hpp"
#include "FFmpeg/CoderVideoTransfer.hpp"
#include "FFmpeg/FFmpegDecoder.hpp"
#include "FFmpeg/FFmpegEncoder.hpp"
#include "FFmpeg/impl/FFmpegEncoder.hpp"
#include <Extension/Frame.h>
#include <Extension/Track.h>
#include <memory>
/*
 * 此类会反射所有通道过来的流。
 * */
class VaryDefaultWriter: public mediakit::FrameWriterInterface{
public:
    using Ptr = std::shared_ptr<VaryDefaultWriter>;
public:
    /*
     * 输入的帧
     * */
    bool inputFrame(const mediakit::Frame::Ptr &frame) override{
        if(on_frame){
            //反射所有过来的包
            on_frame(frame);
        }
        return true;
    }
    void setOnFrame(std::function<void(const mediakit::Frame::Ptr &frame)> f){
        on_frame = f;
    }
private:
    std::function<void(const mediakit::Frame::Ptr &frame)> on_frame;
};

/*
 * 对应的编解码器上下文
 * */
struct Coder{
    /* 目标码流参数 */
    AVCodeContext context;
    /* 解码器 */
    std::shared_ptr<FFmpegDecoder> decoder;
    /* 编码器 */
    std::shared_ptr<FFmpegEncoder<CoderTransfer<Video>>> encoder;

};


/* 编解码器 */
class VaryCoder : public mediakit::FrameWriterInterface, public toolkit::noncopyable{
public:
    using Ptr = std::shared_ptr<VaryCoder>;
    using OnCoderFrame = std::function<void(const mediakit::Frame::Ptr&)>;
    /*
     * @param 1: 视频track
     * @param 2: 音频track
     * */
    using OnAllTrackReady = std::function<void(const mediakit::Track::Ptr&, const mediakit::Track::Ptr)>;
public:
    VaryCoder();
public:
    //添加转码后代理
    void addDelegate(const FrameWriterInterface::Ptr& _delegate, mediakit::TrackType type);
    /*
     * 线程不安全, 添加通道转码
     * */
    void AddCoder(const AVCodeContext&, const mediakit::Track::Ptr& _track = nullptr);
    /*
     * 这里可能会有多个通道的原始码流进入
     * */
    bool inputFrame(const mediakit::Frame::Ptr &frame) override;
    /*
     * 所有转码通道准备好后回调
     * */
    void setAllTrackReady(const OnAllTrackReady&);
private:
    /* 转码后输出通道 */
    void OutputFrame(const CoderContext&, FFmpeg::Frame::Ptr);
    /* 设置视频通道 */
    void makeVideoTrack(const AVCodecID&);
    /* 设置音频通道 */
    void makeAudioTrack(const AVCodecID&);
private:
    /* 视频编解码器 */
    std::shared_ptr<Coder> video_code_;
    /* 音频编解码器 */
    std::shared_ptr<Coder> audio_coder_;
    /* 初始化编码器使用 */
    std::atomic_bool _video_coder_init{false};
    std::atomic_bool _audio_coder_init{false};
    /* 视频转码后的通道 */
    mediakit::Track::Ptr video_track;
    /* 音频转码后的通道 */
    mediakit::Track::Ptr audio_track;
    /* 转码后回调函数 */
    std::function<void(const mediakit::Frame::Ptr&)> on_frame_func;
    /* 是否已经调用 */
    std::atomic_bool track_ready_func_invoke{false};
    /* 所有转码完成后回调 */
    OnAllTrackReady _all_track_ready_func;
    /* 解码前的时间戳 */
    uint32_t video_dts = 0;
    uint32_t video_pts = 0;
};


#endif // ZLMEDIAKIT_VARYFRAMEWRITER_H
