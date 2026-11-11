#include "RelayDiscovery.h"
#include "RelayConfig.h"
#include "Util/MD5.h"
#include "json/json.h"

namespace mediakit {
namespace relay {
namespace {
class Requester : public HttpRequester {
public:
    explicit Requester(const toolkit::EventPoller::Ptr &poller) { setPoller(poller); }
};
} // namespace

void Discovery::resolve(const std::string &key, float timeout, Result result) {
    _result = std::move(result);
    _timeout = timeout;
    _base = setting("esdAddress", "esdAdder");
    auto access = setting("esdAccessKey", "esdAccessKey");
    auto secret = setting("esdSecret", "esdSecret");
    if (_base.empty() || access.empty() || secret.empty() || key.empty()) {
        finish({ toolkit::Err_other, "incomplete relay discovery configuration" });
        return;
    }
    if (_base.find("://") == std::string::npos) {
        _base.insert(0, "http://");
    }
    if (_base.back() == '/') {
        _base.pop_back();
    }
    HttpArgs query;
    query["accessKey"] = access;
    query["publicKey"] = key;
    query["signature"] = toolkit::MD5(secret + "accessKey=" + access + "publicKey=" + key).hexdigest();
    post("/lecam/openapiSig/hls/getDeviceByKey?" + query.make(), false);
}

void Discovery::post(const std::string &path, bool lookup) {
    _request = std::make_shared<Requester>(_poller);
    _request->setMethod("POST");
    _request->setBody("null\n");
    _request->addHeader("Content-Type", "application/json");
    std::weak_ptr<Discovery> weak = shared_from_this();
    _request->startRequester(
        _base + path,
        [weak, lookup](const toolkit::SockException &error, const Parser &response) {
            if (auto self = weak.lock()) {
                self->received(lookup, error, response);
            }
        },
        _timeout);
}

void Discovery::received(bool lookup, const toolkit::SockException &error, const Parser &response) {
    Json::Value json;
    Json::Reader reader;
    if (error || response.Url() != "200" || !reader.parse(response.Content(), json) || !json.isObject()
        || !(json["failflag"].isString() || json["failflag"].isIntegral()) || json["failflag"].asString() != "0") {
        finish({ toolkit::Err_other, lookup ? "relay lookup failed" : "device lookup failed" });
        return;
    }
    if (!lookup) {
        const auto &data = json["data"];
        if (!data.isObject() || !data["deviceId"].isString() || !data["token"].isString()
            || !data["unifiedId"].isString() || data["deviceId"].asString().empty()
            || data["token"].asString().empty()) {
            finish({ toolkit::Err_other, "invalid device lookup response" });
            return;
        }
        _device.id = data["deviceId"].asString();
        _device.token = data["token"].asString();
        _device.unified_id = data["unifiedId"].asString();
        Json::Value body;
        body["deviceid"] = _device.id;
        body["token"] = _device.token;
        HttpArgs query;
        query["jsonObject"] = Json::FastWriter().write(body);
        post("/lookup/openapi/getRelayIPList?" + query.make(), true);
        return;
    }
    const auto &list = json["iplist"];
    if (list.isArray() && !list.empty() && list[0].isObject()) {
        const auto &entry = list[0];
        auto &port = entry["relayport"];
        if (entry["publicip"].isString() && (port.isUInt() || port.isString())) {
            auto value = port.asString();
            if (!value.empty() && value.size() <= 5 && value.find_first_not_of("0123456789") == std::string::npos) {
                auto number = std::stoul(value);
                if (number && number <= 65535) {
                    _device.port = static_cast<uint16_t>(number);
                    _device.host = entry["publicip"].asString();
                }
            }
        }
    }
    finish(
        _device.host.empty() ? toolkit::SockException(toolkit::Err_other, "invalid relay endpoint")
                             : toolkit::SockException());
}

void Discovery::finish(const toolkit::SockException &error) {
    auto result = std::move(_result);
    if (result) {
        result(error, _device);
    }
}
} // namespace relay
} // namespace mediakit
