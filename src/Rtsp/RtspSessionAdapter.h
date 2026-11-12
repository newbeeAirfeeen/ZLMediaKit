//
// Created by shenhao on 2025/9/15.
//

#ifndef RTSPSESSIONADAPTER_H
#define RTSPSESSIONADAPTER_H


#include "RtspSession.h"


namespace mediakit {

    class RtspSessionAdapter : public RtspSession {
    public:
        using base_type = RtspSession;

    public:
        explicit RtspSessionAdapter(const toolkit::Socket::Ptr &sock);
        ~RtspSessionAdapter() override;

    protected:
        void onWholeRtspPacket(Parser &parser) override;
        void handleReq_Play(const Parser &parser) override;
        void handleReq_Pause(const Parser &parser) override;
        void handleReq_Teardown(const Parser &parser) override;
        bool shouldAutoResumeSource() const override;
        bool isPlaybackPaused() const override;
        void onPlayerAttached() override;
        void onError(const toolkit::SockException &err) override;
    private:
        void onWholeRtspPacket_l(Parser &parser);
        void handleReq_Describe_l(const Parser &parser);
        void handleReq_Setup_l(const Parser &parser);
        void handleReq_Play_l(const Parser &parser);

    private:
        void replayControl(bool paused, const Parser &parser);
        SessionMeta::Ptr _player_meta;
        bool _play_started = false;
        std::string _initial_zero_range;
        bool _player_closed = false;
        bool _is_adapter_mode = false;
        std::string _session_id_saved;
    };
using RtspSessionAdapterWithSSL = toolkit::SessionWithSSL<RtspSessionAdapter>;
};











#endif //RTSPSESSIONADAPTER_H
