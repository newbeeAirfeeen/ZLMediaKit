/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SRC_RTMP_RTMPTORTSPMEDIASOURCE_H_
#define SRC_RTMP_RTMPTORTSPMEDIASOURCE_H_

#include <mutex>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include "amf.h"
#include "Rtmp.h"
#include "RtmpMediaSource.h"
#include "Common/MultiMediaSourceMuxer.h"
#include "Common/SessionMeta.h"

namespace mediakit {
class RtmpDemuxer;
class RtmpMediaSourceImp final : public RtmpMediaSource, private TrackListener, public MultiMediaSourceMuxer::Listener {
public:
    using Ptr = std::shared_ptr<RtmpMediaSourceImp>;

    /**
     * 构造函数
     * @param vhost 虚拟主机
     * @param app 应用名
     * @param id 流id
     * @param ringSize 环形缓存大小
     */
    RtmpMediaSourceImp(const std::string &vhost, const std::string &app, const std::string &id, int ringSize = RTMP_GOP_SIZE);

    ~RtmpMediaSourceImp() override { if (_session_meta) { _session_meta->stop(); } }
    const SessionMeta::Ptr &sessionMeta() const { return _session_meta; }
    void setSessionMeta(const SessionMeta::Ptr &meta) {
        _session_meta = meta;
        if (meta) { meta->setReady(_all_track_ready); }
    }
    void setAuthLease(std::shared_ptr<void> lease) { _auth_lease = std::move(lease); }
    void retainForReconnect(const toolkit::EventPoller::Ptr &poller, uint32_t delay_ms);
    void cancelReconnectRetention();

    /**
     * 设置metadata
     */
    void setMetaData(const AMFValue &metadata) override;

    /**
     * 输入rtmp并解析
     */
    void onWrite(RtmpPacket::Ptr pkt, bool = true) override;

    /**
     * 获取观看总人数，包括(hls/rtsp/rtmp)
     */
    int totalReaderCount() override;

    /**
     * 设置协议转换
     */
    void setProtocolOption(const ProtocolOption &option);

    const ProtocolOption &getProtocolOption() const {
        return _option;
    }

    /**
     * _demuxer触发的添加Track事件
     */
    bool addTrack(const Track::Ptr &track) override;

    /**
     * _demuxer触发的Track添加完毕事件
     */
    void addTrackCompleted() override;

    void resetTracks() override;

    /**
     * _muxer触发的所有Track就绪的事件
     */
    void onAllTrackReady() override;

    /**
     * 设置事件监听器
     * @param listener 监听器
     */
    void setListener(const std::weak_ptr<MediaSourceEvent> &listener) override;

private:
    struct ReconnectRetention {
        std::mutex mutex;
        Ptr source;
        void release() {
            Ptr old;
            { std::lock_guard<std::mutex> lock(mutex); old.swap(source); }
        }
    };
    std::weak_ptr<ReconnectRetention> _reconnect_retention;
    SessionMeta::Ptr _session_meta;
    std::shared_ptr<void> _auth_lease;
    bool _all_track_ready = false;
    bool _recreate_metadata = false;
    ProtocolOption _option;
    AMFValue _metadata;
    std::shared_ptr<RtmpDemuxer> _demuxer;
    MultiMediaSourceMuxer::Ptr _muxer;

};
} /* namespace mediakit */

#endif /* SRC_RTMP_RTMPTORTSPMEDIASOURCE_H_ */
