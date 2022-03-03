//
// Created by 沈昊 on 2022/2/22.
//
#if 0
#include "VarySession.h"
#include <Util/onceToken.h>
#include <unordered_map>
#include <Extension/H264.h>
#include <Extension/H265.h>
#include <Extension/H264Rtp.h>
#include <Extension/H265Rtp.h>
using namespace toolkit;
using namespace mediakit;
VarySession::VarySession(){
    //_dispatcher = std::make_shared<VaryFrameWriterDispatcher>();
}

VarySession::~VarySession(){
    InfoL << "Session: " << session_id << ", 会话关闭";
    stop_l();
}

void VarySession::setOnSuccess(const std::function<void()>& f){
    _success_func = f;
}
/*
 * {
 *      session_id: "xxxxx"
 *      cmd    :  [ "vary" | "stop" ]
 *      src_url:  rtsp://xxxx/xx/xx，
 *      dst_url:  rtsp://xxxx/xx/xx,
 *      video:{
 *          codec: "H265 | H264",
 *          width: 1024,
 *          height: 768,
 *      },
 *      audio:{
 *
*       }
 * }
 *
 * */
void VarySession::Excute(const Json::Value& val){

    using cmd_function =  void (VarySession::*)(const Json::Value& val);
    static const std::unordered_map<std::string, cmd_function> request_map = {
        {"vary", &VarySession::vary},
        {"stop", &VarySession::stop}
    };

    bool _ = val.isMember("session_id") && val.isMember("cmd");
    if(!_){
        on_exception("请求的参数不正确, 必须含有参数<session_id><cmd>");
        return ;
    }
    const Json::Value& cmd_type = val.get("cmd", Json::Value::null);
    if(!cmd_type.isString()){
        on_exception("请求的参数值类型有误: cmd值类型必须是string");
        return;
    }
    auto it = request_map.find(cmd_type.asString());
    if( it == request_map.end()){
        on_exception("cmd: 命令行参数有误");
        return;
    }
    (this->*(it->second))(val);
}

//所有通道初始化好后
void VarySession::OnPlayerTrackInited(const std::vector<Track::Ptr>& tracks){
    InfoL << "tracks size: " << tracks.size();
    //拷贝tracks
    std::weak_ptr<VarySession> self = shared_from_this();
    //初始化源
    source = std::make_shared<RtspMuxerMediaSource>();
    Track::Ptr track_ = nullptr;
    for(auto& track : tracks){
        switch(track->getTrackType()){
            case mediakit::TrackVideo: {
                const auto& video = param.get("video", Json::Value::null);
                size_t width = video.get("width", 1024).asInt();
                size_t height = video.get("height", 768).asInt();
                AVCodecID _id = video.get("codec", "H264").asString()
                        == "H264" ? AV_CODEC_ID_H264 : AV_CODEC_ID_H265;
                //初始化好编码器
                //vary_writer =  std::make_shared<VaryFrameWriter>(width, height, _id);
                //设置转码后回调
                //vary_writer -> setOnVaryFrame(std::bind(&VarySession::OnVaryVideo,
                  //                                    this, std::placeholders::_1, std::placeholders::_2));
                //设置对应的码流通道
                if(_id == AV_CODEC_ID_H264) track_ = std::make_shared<H264Track>();
                else track_ = std::make_shared<H265Track>();
                //添加通道
                //vary_tracks.push_back(track_);
                //设置代理
                track->addDelegate(vary_writer);
                //设置转码后代理
                //_dispatcher->setOnVaryDispatcherFrame(std::bind(&VarySession::OnVaryVideo2, this, std::placeholders::_1));
                track_->addDelegate(_dispatcher);
                break;
            }
            case mediakit::TrackAudio:
                //default_write =  std::make_shared<DefaultFrameWriter>();
                //default_write -> setOnFrame(std::bind(&VarySession::OnAudio, this, std::placeholders::_1));
                //track->addDelegate(default_write);
                //连接源
                vary_tracks.push_back(track);
                break;
            default:
                WarnL << "已忽略音视频外的通道";
                break;
        }
    }
    //设置初始化完成回调
    source->setOnTrackReady(std::bind(&VarySession::readyPush, this));
}
void VarySession::OnVaryVideo(const CoderContext& context, FFmpeg::Frame::Ptr ptr){
    auto track_index = getTrackIndex(mediakit::TrackVideo);
    if(!track_index){
        WarnL << "视频通道不存在! 已忽略转码后的帧数据";
        return;
    }
    switch(context.Id()){
        case AV_CODEC_ID_H264: {
            auto track_ = std::static_pointer_cast<H264Track>(track_index);
            H264Frame::Ptr b = std::make_shared<H264Frame>();
            b->_buffer.append((const char*)ptr->data(), ptr->size());
            track_index->inputFrame(b);
            if(track_index->ready() && !source->hasVideoTrack()){
                source->addTrack(track_index);
            }
            break;
        }
        case AV_CODEC_ID_H265: {
            auto track_ = std::static_pointer_cast<H265Track>(track_index);
            H265Frame::Ptr b = std::make_shared<H265Frame>();
            b->_buffer.append((const char*)ptr->data(), ptr->size());
            track_index->inputFrame(b);
            if(track_index->ready() && !source->hasVideoTrack()){
                source->addTrack(track_index);
            }
            break;
        }
        default:break;
    }
}

void VarySession::OnAudio(const mediakit::Frame::Ptr &frame){
    auto track = getTrackIndex(mediakit::TrackAudio);
    if(track){
        //输入音频通道
        source->inputFrame(frame);
    }
    if(track->ready() && !source->hasVideoTrack()){
        source->addTrack(track);
    }
}

void VarySession::vary(const Json::Value& val){
    InfoL << "cmd: vary";
    bool _  = val.isMember("src_url") &&
              val.isMember("dst_url") &&
              val.isMember("video");
    if(!_){
        on_exception("vary请求必须含有src_url, dst_url, video字段");
        return;
    }
    const auto& video = val.get("video", Json::Value::null);
    if(!video.isObject()){
        on_exception("video 参数请求体有误");
        return;
    }
    const auto& vcodec = video.get("codec", Json::Value::null);
    if(!vcodec.isString()){
        on_exception("codec参数有误");
        return;
    }
    if(vcodec.asString() != "H265" && vcodec.asString() != "H264"){
        on_exception("codec参数有误");
        return;
    }

    const auto& width = video.get("width", Json::Value::null);
    const auto& height = video.get("height", Json::Value::null);
    if( !width.isInt() || !height.isInt()){
        on_exception("width或者height参数类型有误");
    }
    return vary_l(val);
}

void VarySession::vary_l(const Json::Value& val){
    std::weak_ptr<VarySession> self = shared_from_this();
    auto player_result = [self](const SockException& e){
        InfoL << e.what();
        if( e.getErrCode() != toolkit::ErrCode::Err_success){
            return;
        }
        auto stronger_self = self.lock();
        if( !stronger_self ){
            return;
        }
        auto tracks = stronger_self->player->getTracks();
        InfoL << "已经连接通道数: " << tracks.size();
        stronger_self->OnPlayerTrackInited(tracks);
    };

    std::call_once(flag, [&, player_result](){
        param = val;
        this->session_id = val.get("session_id", Json::Value::null).asString();
        InfoL << "Session: " << this->session_id;
        InfoL << "初始化拉流器";
        player = std::make_shared<RtspPlayerImp>(EventPollerPool::Instance().getPoller(false));
        player->setOnPlayResult(std::move(player_result));
        player->play(val.get("src_url", Json::Value::null).asString());
        InfoL << "初始化推流器";
    });
}
/* 关闭流的转码 */
void VarySession::stop(const Json::Value& val){
    return stop_l();
}
void VarySession::stop_l(){
    if(player)player->teardown();
    if(pushser) pushser->teardown();
}
void VarySession::readyPush(){
    std::weak_ptr<VarySession> self = shared_from_this();
    auto published_func = [self](const SockException &ex){
        auto stronger_self = self.lock();
        if(!stronger_self)
            return;
        if(ex.getErrCode() != ErrCode::Err_success){
            stronger_self->on_exception("推流失败");
            stronger_self->player->teardown();
            return;
        }
        //成功回调
        stronger_self->_success_func();
    };
    auto shutdown_func = [self](const SockException& ex){
        auto stronger_self = self.lock();
        if(!stronger_self)
            return;
        InfoL << "结束推拉流";
        //切换到自己到线程回复
        stronger_self->player->getPoller()->async([stronger_self](){
            stronger_self->player->teardown();
        });
        //在自己线程回复
        stronger_self->pushser->teardown();
    };


    pushser = std::make_shared<RtspPusherImp>(EventPollerPool::Instance().getPoller(false), source);
    //设置好回调
    pushser -> setOnPublished(published_func);
    pushser -> setOnShutdown(shutdown_func);

    const auto& poller = player->getPoller();
    _reader = source->getRtpRing()->attach(poller, false);
    _reader ->setReadCB([self](const RtpPacket::Ptr& rtp){
        auto stronger_self =  self.lock();
        if(!stronger_self)return;
        //输入rtp
        //rtp包不能判断是否为关键帧，设置为true刷新gop
        InfoL << "write to ring..";
        stronger_self-> source->onWrite(rtp, true);
        //流初始化好才开始推流
        std::call_once(stronger_self->flag2, [stronger_self](){
            stronger_self-> pushser -> publish(stronger_self->param.get("dst_url", Json::Value::null).asString());
        });
    });
    _reader->setDetachCB([self](){
        auto stronger_self =  self.lock();
        if(!stronger_self)return;
        //停止推拉流
        stronger_self->stop_l();
    });
}

Track::Ptr VarySession::getTrackIndex(TrackType type){
    for(auto& track : vary_tracks){
        if( track->getTrackType() == type)return track;
    }
    return nullptr;
}

void VarySession::OnVaryVideo2(const mediakit::Frame::Ptr& ptr){
    source->inputFrame(ptr);
}
#endif