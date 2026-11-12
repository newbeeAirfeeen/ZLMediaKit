// Live client protocol adapted from streamproxy NetWorkTool (MIT).
#include "RelayPlayer.h"
#include "Common/config.h"
#include "relay_message.pb.h"
#include <cstring>
#include <ctime>

using namespace toolkit;

namespace mediakit {
namespace {
constexpr size_t maxMessage = 16 * 1024 * 1024;
}

void RelayPlayer::play(const std::string &url) {
    auto weak = std::weak_ptr<RelayPlayer>(std::dynamic_pointer_cast<RelayPlayer>(shared_from_this()));
    getPoller()->async([weak, url]() {
        auto self = weak.lock();
        if (!self) {
            return;
        }
        self->_state = State::Discovering;
        try {
            self->_demuxer.reset(new relay::Demuxer([weak](const Frame::Ptr &frame) {
                if (auto player = weak.lock()) {
                    player->output(frame);
                }
            }));
            auto timeout = (*self)[Client::kTimeoutMS].as<int>() / 1000.0f;
            self->_deadline = std::make_shared<Timer>(
                timeout,
                [weak]() {
                    if (auto player = weak.lock()) {
                        player->fail({ Err_timeout, "relay startup timed out" });
                    }
                    return false;
                },
                self->getPoller());
            self->_discovery = std::make_shared<relay::Discovery>(self->getPoller());
            self->_discovery->resolve(
                url.substr(sizeof("relay://") - 1), timeout,
                [weak, timeout](const SockException &err, const relay::Device &device) {
                    auto player = weak.lock();
                    if (!player || player->_state != State::Discovering) {
                        return;
                    }
                    if (err) {
                        player->fail(err);
                        return;
                    }
                    player->_device = device;
                    player->_state = State::Login;
                    player->startConnect(device.host, device.port, timeout);
                });
        } catch (const std::exception &err) {
            self->fail({ Err_other, err.what() });
        }
    });
}

void RelayPlayer::onConnect(const SockException &err) {
    if (err) {
        fail(err);
        return;
    }
    RelayMessage msg;
    msg.set_message_type(RelayMessage::REQUEST);
    auto req = msg.mutable_request();
    req->set_user_name("");
    req->set_password("");
    req->set_device_name("");
    req->set_device_id("pc_ZLmediakit_" + _device.token);
    req->set_file_name(std::to_string(std::time(nullptr)) + _device.id + ".raw");
    req->set_key("ipcamcodec01");
    req->set_type(9);
    req->set_version("12.0");
    req->set_channel_name("720p");
    req->set_ipcam_id(_device.id);
    req->set_cloud_token(_device.token);
    req->set_unified_id(_device.unified_id);
    req->set_is_record(false);
    req->set_is_manage_event(true);
    req->set_use_zlib(true);
    req->set_head_len(4);
    write(msg);
}

void RelayPlayer::write(const RelayMessage &msg) {
    std::string payload;
    if (!msg.SerializeToString(&payload) || payload.size() > maxMessage) {
        fail({ Err_other, "invalid outgoing relay message" });
        return;
    }
    uint32_t length = htonl(static_cast<uint32_t>(payload.size()));
    payload.insert(0, reinterpret_cast<const char *>(&length), sizeof(length));
    if (send(std::make_shared<BufferString>(std::move(payload))) < 0) {
        fail({ Err_other, "relay send failed" });
    }
}

void RelayPlayer::onRecv(const Buffer::Ptr &buffer) {
    if (_state == State::Closed) {
        return;
    }
    try {
        _pending_bytes.append(buffer->data(), buffer->size());
        size_t offset = 0;
        while (_pending_bytes.size() - offset >= 4) {
            uint32_t length;
            std::memcpy(&length, _pending_bytes.data() + offset, sizeof(length));
            length = ntohl(length);
            if (!length || length > maxMessage) {
                throw std::runtime_error("invalid relay message length");
            }
            if (_pending_bytes.size() - offset - 4 < length) {
                break;
            }
            RelayMessage msg;
            if (!msg.ParseFromArray(_pending_bytes.data() + offset + 4, length)) {
                throw std::runtime_error("invalid relay message");
            }
            offset += 4 + length;
            receive(msg);
            if (_state == State::Closed) {
                return;
            }
        }
        _pending_bytes.erase(0, offset);
    } catch (const std::exception &err) {
        fail({ Err_other, err.what() });
    }
}

void RelayPlayer::receive(const RelayMessage &msg) {
    if (_state == State::Login) {
        if (msg.message_type() != RelayMessage::RESPONE || !msg.has_response() || msg.response().result() != 0) {
            throw std::runtime_error("relay login rejected");
        }
        RelayMessage play;
        play.set_message_type(RelayMessage::CLIENTCMD);
        play.mutable_client_cmd()->set_type(1);
        play.mutable_client_cmd()->set_device_id("pc_ZLmediakit_" + _device.token);
        _state = State::Priming;
        write(play);
        return;
    }
    RelayMessage reply;
    switch (msg.message_type()) {
        case RelayMessage::MEDIAPACKAGE:
            if (!msg.has_media_package()) {
                throw std::runtime_error("missing relay media package");
            }
            _last_media.resetTime();
            _demuxer->input(msg.media_package());
            break;
        case RelayMessage::PING:
            reply.set_message_type(RelayMessage::PONG);
            reply.mutable_pong()->set_id(0);
            write(reply);
            break;
        case RelayMessage::REQUEST: {
            reply.set_message_type(RelayMessage::RESPONE);
            auto response = reply.mutable_response();
            response->set_result(0);
            response->set_session_id(0);
            response->set_cloud_token("");
            response->set_cloud_did(0);
            response->set_server_time(std::to_string(std::time(nullptr)));
            write(reply);
            break;
        }
        case RelayMessage::CLIENTCMD:
            if (msg.has_client_cmd() && msg.client_cmd().type() == 2) {
                throw std::runtime_error("relay closed stream");
            }
            break;
        default: break;
    }
}

void RelayPlayer::output(const Frame::Ptr &frame) {
    if (_state == State::Priming) {
        _pending_size += frame->size();
        if (_pending_size > maxMessage || _pending_frames.size() >= 1024) {
            throw std::runtime_error("relay startup frame limit exceeded");
        }
        _pending_frames.emplace_back(Frame::getCacheAbleFrame(frame));
        if (frame->configFrame()) {
            return;
        }
        if (_demuxer->tracks().size() == 2) {
            publish();
            return;
        }
        // A single-track camera must not wait forever for an absent second track.
        if (!_track_timer) {
            auto weak = std::weak_ptr<RelayPlayer>(std::dynamic_pointer_cast<RelayPlayer>(shared_from_this()));
            GET_CONFIG(uint32_t, wait_ms, General::kWaitAddTrackMS);
            _track_timer = std::make_shared<Timer>(
                std::max(1u, wait_ms) / 1000.0f,
                [weak]() {
                    if (auto self = weak.lock()) {
                        self->publish();
                    }
                    return false;
                },
                getPoller());
        }
    } else if (_state == State::Playing) {
        for (const auto &track : _demuxer->tracks()) {
            if (track->getCodecId() == frame->getCodecId()) {
                track->inputFrame(frame);
                break;
            }
        }
    }
}

void RelayPlayer::publish() {
    if (_state != State::Priming) {
        return;
    }
    _state = State::Playing;
    _deadline.reset();
    _track_timer.reset();
    _last_media.resetTime();
    _last_ping.resetTime();
    onPlayResult(SockException());
    auto frames = std::move(_pending_frames);
    _pending_size = 0;
    for (const auto &frame : frames) {
        output(frame);
    }
}

std::vector<Track::Ptr> RelayPlayer::getTracks(bool ready) const {
    std::vector<Track::Ptr> result;
    if (_demuxer) {
        for (const auto &track : _demuxer->tracks()) {
            if (!ready || track->ready()) {
                result.emplace_back(track);
            }
        }
    }
    return result;
}

void RelayPlayer::onManager() {
    if (_state != State::Playing) {
        return;
    }
    if (_last_media.elapsedTime() > (*this)[Client::kMediaTimeoutMS].as<uint64_t>()) {
        fail({ Err_timeout, "relay media timeout" });
    } else if (_last_ping.elapsedTime() >= 20000) {
        RelayMessage ping;
        ping.set_message_type(RelayMessage::PING);
        ping.mutable_ping()->set_id(0);
        _last_ping.resetTime();
        write(ping);
    }
}

void RelayPlayer::teardown() {
    _state = State::Closed;
    _deadline.reset();
    _track_timer.reset();
    _discovery.reset();
    if (alive()) {
        shutdown({ Err_shutdown, "relay stopped" });
    }
}

void RelayPlayer::fail(const SockException &err) {
    if (_state == State::Closed) {
        return;
    }
    bool started = _state == State::Playing;
    teardown();
    if (started) {
        onShutdown(err);
    } else {
        onPlayResult(err);
    }
}

void RelayPlayer::onErr(const SockException &err) {
    fail(err);
}
} // namespace mediakit
