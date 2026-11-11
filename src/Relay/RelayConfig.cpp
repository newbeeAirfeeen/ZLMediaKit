#include "RelayConfig.h"
#include "Common/Parser.h"
#include "Util/mini.h"
#include "Util/util.h"
#include <map>
#include <mutex>
#include <tuple>

namespace mediakit {
namespace relay {

std::string setting(const char *key, const char *legacy_key, const char *fallback) {
    const auto &config = toolkit::mINI::Instance();
    auto it = config.find(std::string("relay.") + key);
    if (it == config.end() && legacy_key) {
        it = config.find(std::string("closeli.") + legacy_key);
    }
    return it == config.end() ? std::string(fallback) : std::string(it->second);
}

bool matches(const std::string &vhost, const std::string &app, const std::string &stream) {
    auto prefix = setting("streamPrefix", nullptr);
    return !app.empty() && app == setting("app", nullptr) && vhost == setting("vhost", nullptr, "__defaultVhost__")
        && stream.compare(0, prefix.size(), prefix) == 0;
}

// The first HLS viewer waits for registration before it can own a ring reader.
namespace {
using Key = std::tuple<std::string, std::string, std::string>;
std::mutex pending_mutex;
std::map<Key, unsigned> pending_readers;
} // namespace

std::shared_ptr<void> holdPendingReader(const std::string &vhost, const std::string &app, const std::string &stream) {
    Key key(vhost, app, stream);
    std::lock_guard<std::mutex> lock(pending_mutex);
    ++pending_readers[key];
    return std::shared_ptr<void>(nullptr, [key](void *) {
        std::lock_guard<std::mutex> lock(pending_mutex);
        auto it = pending_readers.find(key);
        if (--it->second == 0) {
            pending_readers.erase(it);
        }
    });
}

bool hasPendingReader(const std::string &vhost, const std::string &app, const std::string &stream) {
    std::lock_guard<std::mutex> lock(pending_mutex);
    return pending_readers.count(Key(vhost, app, stream)) != 0;
}

std::string sourceUrl(const std::string &schema, const std::string &stream, const std::string &params) {
    std::string key;
    if (schema == "hls") {
        key = stream.substr(stream.find_last_of('/') + 1);
        if (key.size() >= 5 && key.compare(key.size() - 5, 5, ".m3u8") == 0) {
            key.resize(key.size() - 5);
        }
    } else if (schema == "rtsp") {
        key = Parser::parseArgs(params)["auth_key"];
    } else {
        throw std::invalid_argument("not rtsp or hls");
    }
    if (key.empty()) {
        throw std::invalid_argument("missing relay public key");
    }
    // Internal source identifier. Routing credentials come only from ESD.
    return "relay://" + key;
}
} // namespace relay
} // namespace mediakit
