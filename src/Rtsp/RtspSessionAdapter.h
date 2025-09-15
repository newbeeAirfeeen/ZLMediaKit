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
        ~RtspSessionAdapter() override = default;

    protected:
        void onWholeRtspPacket(Parser &parser) override;

    private:
        void onWholeRtspPacket_l(Parser &parser);
        void handleReq_Describe_l(const Parser &parser);
        void handleReq_Setup_l(const Parser &parser);
        void handleReq_Play_l(const Parser &parser);

    private:
        bool _is_adapter_mode = false;
    };
};











#endif //RTSPSESSIONADAPTER_H
