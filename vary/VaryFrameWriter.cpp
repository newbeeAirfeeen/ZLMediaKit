//
// Created by 沈昊 on 2022/2/22.
//
#include "VaryFrameWriter.h"
#include <Util/logger.h>
#include <Extension/H264.h>
#include <Extension/H265.h>

static void setSrcCodeId(AVCodecID& src_id, mediakit::CodecId id){
    switch(id){
        case mediakit::CodecH264: src_id = AV_CODEC_ID_H264;  break;
        case mediakit::CodecH265: src_id = AV_CODEC_ID_H265;  break;
        case mediakit::CodecAAC:  src_id = AV_CODEC_ID_MPEG4; break;
        default:src_id = AV_CODEC_ID_NONE;break;
    }
}


VaryCoder::VaryCoder(){

}

void VaryCoder::addDelegate(const mediakit::FrameWriterInterface::Ptr& _delegate, mediakit::TrackType type){
    if(type == mediakit::TrackVideo && video_track)video_track->addDelegate(_delegate);
    if(type == mediakit::TrackAudio && audio_track)audio_track->addDelegate(_delegate);
}

void VaryCoder::AddCoder(const AVCodeContext& context, const mediakit::Track::Ptr& _track){
    //找到原始码流的编码
    AVMediaType src_type = avcodec_get_type(context.src_id);
    AVMediaType dst_type = avcodec_get_type(context.dst_id);

    if(src_type != AVMEDIA_TYPE_VIDEO && src_type != AVMEDIA_TYPE_AUDIO){
        ErrorL << "源码流的编码类型不是音视频编码类型,不添加编解码器";
        return;
    }
    if(dst_type != AVMEDIA_TYPE_VIDEO && dst_type != AVMEDIA_TYPE_AUDIO){
        ErrorL << "目标码流的编码类型不是音视频编码类型,不添加编解码器";
        return;
    }
    if( dst_type != src_type){
        ErrorL << "目标码流与源码流的编码不是同一个类型,不添加编解码器";
        return;
    }
    switch (src_type)
    {
        case AVMEDIA_TYPE_VIDEO:
            video_code_ = std::make_shared<Coder>();
            video_code_->context = context;
            video_code_->decoder = std::make_shared<FFmpegDecoder>(context.src_id);
            video_code_->encoder = std::make_shared<FFmpegEncoder<CoderTransfer<Video>>>(context.dst_id);
            video_code_->decoder->setOnDecoder([this](const CoderContext& _dec_ctx, typename Rawpgm::Ptr ptr){
                bool init = false;
                if(_video_coder_init.compare_exchange_weak(init, true)){
                    video_code_->encoder->Scale(video_code_->context.dst_width, video_code_->context.dst_height);
                    video_code_->encoder->setTimeBase(_dec_ctx.TimeBase());
                    video_code_->encoder->PixFmt(_dec_ctx.PixFormat());
                    video_code_->encoder->CreateTransfer(_dec_ctx.Width(), _dec_ctx.Height(), _dec_ctx.PixFormat(),
                                                  _dec_ctx.TimeBase(), _dec_ctx.SampleAspectRatio());
                    video_code_->encoder->setOnFrame(std::bind(&VaryCoder::OutputFrame, this ,
                                                        std::placeholders::_1, std::placeholders::_2));
                    //初始化好编码器
                }
                video_code_->encoder->readFrame(ptr);
            });
            //初始化好转码视频通道
            makeVideoTrack(context.dst_id);
            break;
        case AVMEDIA_TYPE_AUDIO:
            if(!_track)return;
            ErrorL << "暂不支持音频编解码, 当前为透传音频";
            if(_track->getTrackType() == mediakit::TrackType::TrackAudio)
                audio_track = _track->clone();
            return;
            audio_coder_ = std::make_shared<Coder>();
            audio_coder_->context = context;
            audio_coder_->decoder = std::make_shared<FFmpegDecoder>(context.src_id);
            audio_coder_->encoder = std::make_shared<FFmpegEncoder<CoderTransfer<Video>>>(context.dst_id);
            makeAudioTrack(context.dst_id);
            break;
        default:break;
    }
}

bool VaryCoder::inputFrame(const mediakit::Frame::Ptr &frame){
    /* 设置对应的编码id */
    if(frame->getTrackType() == mediakit::TrackAudio){
        if(audio_track && audio_track->getCodecId() == frame->getCodecId())
            audio_track->inputFrame(frame);
        return true;
    }
    //检查编码是否为解码器的id
    AVCodecID _id = AV_CODEC_ID_NONE;
    setSrcCodeId(_id, frame->getCodecId());
    if(_id == video_code_->context.src_id){
        /* 保存当前编码时间戳 */
        video_dts = frame->dts();
        video_pts = frame->pts();
        video_code_->decoder->inputFrame(frame->data(), frame->size());
        return true;
    }
    return false;
}

//这里要根据对应的id在输入对应的track
void VaryCoder::OutputFrame(const CoderContext& context, FFmpeg::Frame::Ptr frame){
    //如此编码前的id
    switch(context.Id()){
        case AV_CODEC_ID_H264:
        {
            if(!video_track) return;
            mediakit::H264Frame::Ptr avc_frame = std::make_shared<mediakit::H264Frame>();
            avc_frame->_buffer.append((const char*)frame->data(), frame->size());
            avc_frame->_dts = video_dts;
            avc_frame->_pts = video_pts;
            video_track->inputFrame(std::move(avc_frame));
            break;
        }
        case AV_CODEC_ID_H265:
        {
            mediakit::H265Frame::Ptr hevc_frame = std::make_shared<mediakit::H265Frame>();
            hevc_frame->_buffer.append((const char*)frame->data(), frame->size());
            hevc_frame->_dts = video_dts;
            hevc_frame->_pts = video_pts;
            video_track->inputFrame(std::move(hevc_frame));
            break;
        }
        default:{
            ErrorL << "未设置对应的编码通道, 已忽略";
            break;
        }
    }
    if(!_all_track_ready_func)return;
    /* 反正忽略音频，干脆不判断 */
    if(video_track && video_track->ready() && !track_ready_func_invoke)
    {
        if(audio_track && audio_track -> ready())
        {
            track_ready_func_invoke.store(true);
            _all_track_ready_func(video_track, audio_track);
        }
        else if(!audio_track){
            track_ready_func_invoke.store(true);
            _all_track_ready_func(video_track, audio_track);
        }
    }


}

void VaryCoder::makeVideoTrack(const AVCodecID& id){
    switch (id) {
        case AV_CODEC_ID_H264: video_track = std::make_shared<mediakit::H264Track>(); break;
        case AV_CODEC_ID_H265: video_track = std::make_shared<mediakit::H265Track>(); break;
        default:break;
    }
}
/* 设置音频通道 */
void VaryCoder::makeAudioTrack(const AVCodecID& id){
}

void VaryCoder::setAllTrackReady(const OnAllTrackReady& f){
    this->_all_track_ready_func = f;
}