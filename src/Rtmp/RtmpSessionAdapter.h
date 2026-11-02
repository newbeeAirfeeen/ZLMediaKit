#pragma once
#include "RtmpSession.h"

namespace mediakit {
class RtmpSessionAdapter : public RtmpSession {
public:
    explicit RtmpSessionAdapter(const toolkit::Socket::Ptr &sock) : RtmpSession(sock) {}
    ~RtmpSessionAdapter() override;
    void onError(const toolkit::SockException &err) override;
protected:
    void onConnectProperties(const AMFValue &params) override;
    void onPublishOptions(ProtocolOption &option) override;
    bool onPublished(const RtmpMediaSourceImp::Ptr &src) override;
    bool onDataMessage(const std::string &type, AMFDecoder &dec) override;
    bool shouldAutoResumeSource() const override;
    bool allowContinuePush() const override;
    void onPlayerAttached() override;
    void onCmd_pause(AMFDecoder &dec) override;
    void onCmd_play(AMFDecoder &dec) override;
    void onCmd_play2(AMFDecoder &dec) override;
    void onCmd_seek(AMFDecoder &dec) override;
    void onCmd_playCtrl(AMFDecoder &dec) override;
private:
    bool rejectUnsupported();
    bool _controlled = false;
    SessionMeta::Snapshot _candidate;
    SessionMeta::Ptr _player_meta;
};
using RtmpSessionAdapterWithSSL = toolkit::SessionWithSSL<RtmpSessionAdapter>;
} // namespace mediakit
