//
// Created by 沈昊 on 2022/2/24.
//
#include "VaryTask.hpp"
#include <Util/logger.h>
#include <Network/Socket.h>
#include "VaryFrameWriter.h"
#include <Poller/EventPoller.h>
using namespace toolkit;
VaryTask::VaryTask(const toolkit::EventPoller::Ptr& poller){
    if(!poller){
        this->poller = this->poller = EventPollerPool::Instance().getPoller(false);
    }
    else this->poller = poller;
    /* 默认初始化任务状态回调为空 */
    this->_status_invoke = [](int, const std::string&)->int{
        return 0;
    };
    setState(TaskStatus::unknown);
    _muxer_source = std::make_shared<mediakit::RtspMuxer>();
}
VaryTask::~VaryTask(){
    stop_l();
}

void VaryTask::start_l(){

    if(getState() == TaskStatus::Initing){
        WarnL << "任务已经初始化";
        return;
    }
    setState(TaskStatus::Initing);
    /* 在初始化前进行一次回调 */
    InfoL << "任务正在初始化中";
    auto ret = _status_invoke(getState(), "任务正在初始化中");
    if( ret < 0){
        return stop_l();
    }


    /* 开始初始化拉流器 */
    std::weak_ptr<VaryTask> self = shared_from_this();

    /* 拉流器拉流结果 */
    auto player_result = [self](const SockException& e){
        auto stronger_self = self.lock();
        if( !stronger_self ){
            return;
        }
        int err = 0;
        std::string msg;
        switch(e.getErrCode()){
            case ErrCode::Err_success:
                err = TaskStatus::pull_success;
                msg = "拉流成功";
                break;
            case ErrCode::Err_timeout:
                err = TaskStatus::pull_timeout;
                msg = "拉流超时";
                break;
            case ErrCode::Err_dns:
                err = TaskStatus::pull_dns_err;
                msg = "拉流dns解析失败";
                break;
            case ErrCode::Err_eof:
                err = TaskStatus::pull_ative_err;
                msg = "对端主动关闭连接";
                break;
            case ErrCode::Err_refused:
                err = TaskStatus::pull_reset_conn;
                msg = "连接被重置";
                break;
            case ErrCode::Err_other:
                err = TaskStatus::other_err;
                msg = e.what();
                break;
        }
        //切换到自己的线程回复
        stronger_self->poller->async([err, msg, stronger_self](){
            /* 设置当前任务的状态并调用回调 */
            stronger_self->setState(err);
            int ret = stronger_self->_status_invoke(err, msg);
            if( ret < 0 || err != TaskStatus::pull_success){
                stronger_self -> stop_l();
                return;
            }
            /* 尝试初始化编解码器 */
            stronger_self -> startInitCoder();
        });
    };
    InfoL << "正在初始化拉流器";
    player = std::make_shared<mediakit::RtspPlayerImp>(EventPollerPool::Instance().getPoller(false));
    player ->setOnPlayResult(std::move(player_result));
    /* 开始拉流 */
    player -> play("rtsp://127.0.0.1/live/stream");
}

void VaryTask::stop_l(){
    InfoL << "任务被取消";
    setState(TaskStatus::die);
    {

    }
    _status_invoke(getState(), "任务被取消");
}

void VaryTask::setOnTaskStatusInvoke(const OnTaskStatusInvoke& f){
    if(f) _status_invoke = f;
}

void VaryTask::startInitCoder(){
    /* 函数在自己线程 */
    /* 设置状态 */
    setState(TaskStatus::coder_initing);
    auto ret = _status_invoke(getState(), "正在初始化编解码器");
    //停止任务
    if(ret < 0){
        return stop_l();
    }
    _vary_coder = std::make_shared<VaryCoder>();
    tracks = player->getTracks();
    if(!tracks.size()){
        InfoL << "拉流端的通道为空";
        setState(TaskStatus::pull_track_empty);
        _status_invoke(getState(),"拉流端的通道为空");
        return stop_l();
    }

    /* 映射到对应的源码流id */
    auto get_avcodec_id_func = [](mediakit::CodecId _id) -> AVCodecID{
        switch(_id){
            case mediakit::CodecH264: return AV_CODEC_ID_H264;
            case mediakit::CodecH265: return AV_CODEC_ID_H265;
            case mediakit::CodecAAC:  return AV_CODEC_ID_MPEG4;
            default:return AV_CODEC_ID_NONE;
        }
    };

    /*******************************************************/
    /* 这里暂时先写死 */
    for(const auto& track : tracks){
        AVCodeContext _ctx;
        _ctx.src_id = _ctx.dst_id = get_avcodec_id_func(track->getCodecId());
        if(_ctx.src_id == AV_CODEC_ID_NONE){
            ErrorL << "编解码器初始化失败, 不支持此解码";
            setState(TaskStatus::coder_init_failed);
            _status_invoke(getState(), "编解码器初始化失败, 不支持此解码");
            return stop_l();
        }
        //这里暂时写死
        if(track->getTrackType() == mediakit::TrackType::TrackVideo){
            _ctx.dst_height = 480;
            _ctx.dst_width = 720;
            _ctx.dst_id = AV_CODEC_ID_H264;
        }
        //这里需要判断返回值
        _vary_coder->AddCoder(_ctx, track);
        //设置代理
        track->addDelegate(_vary_coder);
    }
    /* 设置转码后通道初始化完毕回调 */
    _vary_coder->setAllTrackReady([this](const mediakit::Track::Ptr& _video_track, const mediakit::Track::Ptr& _audio_track){
        InfoL << "添加复用器tracks";
        this->_muxer_source->addTrack(_video_track);
        this->_muxer_source->addTrack(_video_track);

        //添加转发代理
        auto _delegate = std::make_shared<VaryDefaultWriter>();
        std::weak_ptr<mediakit::RtspMuxer> muxer_self = _muxer_source;
        _delegate ->setOnFrame([muxer_self](const mediakit::Frame::Ptr &frame){
            auto stronger_muxer = muxer_self.lock();
            if(!stronger_muxer)return;
            stronger_muxer->inputFrame(frame);
        });
        InfoL << "添加对应的流通道到复用器";
        if(_video_track) _video_track->addDelegate(_delegate);
        if(_audio_track) _audio_track->addDelegate(_delegate);
        InfoL << "尝试开始推流";
        this->start_push();
    });

}

void VaryTask::start_push(){
    /* 此函数在自己的线程执行 */
    setState(TaskStatus::pushing);
    InfoL << "正在尝试推流";
    auto ret = _status_invoke(getState(), "正在尝试推流");
    if( ret < 0){
        return stop_l();
    }
    std::weak_ptr<VaryTask> self = shared_from_this();
    //推流结果回调
    auto publish_func = [self](const SockException& e){
        auto stronger_self = self.lock();
        if(!stronger_self)
            return;
        int err = 0;
        std::string msg;
        switch(e.getErrCode()){
        case ErrCode::Err_success:
            err = TaskStatus::push_success;
            msg = "推流成功";
            break;
        case ErrCode::Err_timeout:
            err = TaskStatus::push_timeout;
            msg = "推流超时";
            break;
        case ErrCode::Err_dns:
            err = TaskStatus::push_dns_err;
            msg = "推流dns解析失败";
            break;
        case ErrCode::Err_eof:
            err = TaskStatus::push_ative_err;
            msg = "收流对端主动关闭连接";
            break;
        case ErrCode::Err_refused:
            err = TaskStatus::push_reset_conn;
            msg = "连接被重置";
            break;
        case ErrCode::Err_other:
            err = TaskStatus::other_err;
            msg = e.what();
            break;
        }
        //切换到自己的线程回复
        stronger_self->poller->async([err, msg, stronger_self](){
            /* 设置当前任务的状态并调用回调 */
            stronger_self->setState(err);
            int ret = stronger_self->_status_invoke(err, msg);
            if( ret < 0 || err != TaskStatus::push_success){
                stronger_self -> stop_l();
                return;
            }
        });
    };

    InfoL << "添加通道到rtsp_muxer复用器, " << "size = " << tracks.size();
    pusher = std::make_shared<vary::RtspPusher>(EventPollerPool::Instance().getPoller(false), _muxer_source);
    //设置推流回调
    pusher ->setOnPublished(std::move(publish_func));
    pusher ->publish("rtsp://127.0.0.1/live/stream2");
}


void VaryTask::Commit(AVCodeContext context){
    /* 可能在其他线程commit*/
    std::weak_ptr<VaryTask> self = shared_from_this();
    //可能在其他线程执行，需要切换到自己的线程执行
    this->poller->async([self, context](){
        auto stronger_self = self.lock();
        if(!stronger_self)
            return;
        std::call_once(stronger_self->_commit_flag, [stronger_self, context](){
            stronger_self -> context = context;
            //这里执行初始化工作
        });
    });
}