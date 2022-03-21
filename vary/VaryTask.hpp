//
// Created by 沈昊 on 2022/2/24.
//

#ifndef MEDIAKIT_VARYTASK_HPP
#define MEDIAKIT_VARYTASK_HPP
#include <memory>
#include <Rtsp/RtspMuxer.h>
#include <functional>
#include <Rtsp/RtspPlayerImp.h>
#include "RtspPusher.hpp"
#include "AVCodeContext.hpp"
#include "VaryFrameWriter.h"
class VaryTask : public std::enable_shared_from_this<VaryTask>{
public:
    using Ptr = std::shared_ptr<VaryTask>;
public:
    explicit VaryTask(const toolkit::EventPoller::Ptr& poller = nullptr);

    ~VaryTask();
    /*
     * 开启拉流转码后推流
     * */
    void start_l();
    /*
     * 关闭任务
     * */
    void stop_l();
    /*
     * 添加转码任务
     * @param 转换的目标编码
     *
     * */
    void Commit(AVCodeContext context);
private:
    /*
     * 初始化编解码器
     * */
    void startInitCoder();
    /*
     * 开始尝试去推流
     * */
    void start_push();
private:
    /* 任务所在的线程 */
    toolkit::EventPoller::Ptr poller;
    /* rtsp muxer复用源 */
    mediakit::RtspMuxer::Ptr _muxer_source;
    /* 拉流器 */
    mediakit::RtspPlayerImp::Ptr player;
    /* 拉流器的所有tracks */
    std::vector<mediakit::Track::Ptr> tracks;
    /* rtsp 推流器 */
    vary::RtspPusher::Ptr pusher;
    /* 目标转码参数 */
    AVCodeContext context;
    /* 是否已经初始化过 */
    std::once_flag _commit_flag;
    /* 编解码器 */
    VaryCoder::Ptr _vary_coder;
};



#endif // MEDIAKIT_VARYTASK_HPP
