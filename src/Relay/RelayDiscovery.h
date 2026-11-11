#pragma once

#include "Http/HttpRequester.h"

namespace mediakit {
namespace relay {

struct Device {
    std::string id, token, unified_id, host;
    uint16_t port = 0;
};

class Discovery : public std::enable_shared_from_this<Discovery> {
public:
    using Result = std::function<void(const toolkit::SockException &, const Device &)>;
    explicit Discovery(const toolkit::EventPoller::Ptr &poller)
        : _poller(poller) {}
    void resolve(const std::string &public_key, float timeout, Result result);

private:
    void post(const std::string &path, bool lookup);
    void received(bool lookup, const toolkit::SockException &error, const Parser &response);
    void finish(const toolkit::SockException &error);
    toolkit::EventPoller::Ptr _poller;
    HttpRequester::Ptr _request;
    Device _device;
    Result _result;
    std::string _base;
    float _timeout = 0;
};

} // namespace relay
} // namespace mediakit
