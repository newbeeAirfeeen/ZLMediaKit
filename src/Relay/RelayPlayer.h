#pragma once

#include "Network/TcpClient.h"
#include "Player/PlayerBase.h"
#include "Poller/Timer.h"
#include "RelayDemuxer.h"
#include "RelayDiscovery.h"

class RelayMessage;

namespace mediakit {

// The upstream wire messages follow streamproxy's live relay client. MediaPlayer
// creates a fresh delegate on each retry, including a fresh ESD lookup.
class RelayPlayer final
    : public PlayerImp<PlayerBase, PlayerBase>
    , public toolkit::TcpClient {
public:
    explicit RelayPlayer(const toolkit::EventPoller::Ptr &poller)
        : TcpClient(poller) {}
    void play(const std::string &url) override;
    void teardown() override;
    std::vector<Track::Ptr> getTracks(bool ready) const override;

private:
    enum class State { Discovering, Login, Priming, Playing, Closed };
    void onConnect(const toolkit::SockException &error) override;
    void onRecv(const toolkit::Buffer::Ptr &buffer) override;
    void onErr(const toolkit::SockException &error) override;
    void onManager() override;
    void receive(const RelayMessage &message);
    void write(const RelayMessage &message);
    void output(const Frame::Ptr &frame);
    void publish();
    void fail(const toolkit::SockException &error);

    State _state = State::Closed;
    std::shared_ptr<relay::Discovery> _discovery;
    relay::Device _device;
    std::unique_ptr<relay::Demuxer> _demuxer;
    std::string _pending_bytes;
    std::vector<Frame::Ptr> _pending_frames;
    size_t _pending_size = 0;
    toolkit::Timer::Ptr _deadline, _track_timer;
    toolkit::Ticker _last_media, _last_ping;
};

} // namespace mediakit
