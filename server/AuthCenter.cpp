//
// Created by shenhao on 2024/9/2.
//
#include "AuthCenter.h"
#include "Http/HttpRequester.h"
#include "json/json.h"
#include "Util/logger.h"

using namespace toolkit;
using namespace mediakit;
AuthCenter &AuthCenter::instance() {
    static AuthCenter instance;
    return instance;
}

void AuthCenter::regist_auth(const mediakit::MediaInfo &info, const std::string &url, const std::string &session_id) {
    auto key = generate_key(info);
    std::lock_guard<std::mutex> lck(_mtx);
    AuthEntry entry;
    entry._url = url;
    entry._session_id = session_id;
}
void AuthCenter::unreigst_auth(const mediakit::MediaInfo &info) {
    auto key = generate_key(info);
    std::lock_guard<std::mutex> lck(_mtx);
    _auth_entry_map.erase(key);
}
auto AuthCenter::auth(const mediakit::MediaInfo &info, const mediakit::Broadcast::AuthInvoker &invoker) -> bool {
    auto key = generate_key(info);
    std::lock_guard<std::mutex> lck(_mtx);
    auto it = _auth_entry_map.find(key);
    if (it == _auth_entry_map.end()) {
        EventPollerPool::Instance().getPoller(false)->async([invoker]() {
            InfoL << "动态鉴权中心无人鉴权";
            invoker("");
        });
        return false;
    }
    auto uri = it->second._url;
    HttpRequester::Ptr requester(new HttpRequester);
    Json::Value value;
    value["type"] = "auth";
    value["session_id"] = it->second._session_id;
    requester->setMethod("POST");
    requester->setBody(value.toStyledString());
    requester->addHeader("Content-Type", "application/json");
    requester->startRequester(
        uri,
        [invoker, info, requester](const toolkit::SockException &ex, const Parser &response) {
            InfoL << "app=" << info._app << ", stream_id=" << info._streamid
                  << ", auth response:" << response.Content();
            if (ex) {
                invoker("hook failed...");
                return;
            }
            Json::Value result;
            try {
                std::stringstream ss(response.Content());
                ss >> result;
            } catch (...) {
                invoker("hook response parse failed...");
                return;
            }
            if (result["code"].asInt() != 0) {
                invoker("hook code response not 0...");
                return;
            }
            invoker("");
        },
        2.0);
    return true;
}
auto AuthCenter::media_changed(const std::string &app, const std::string &id, int reader_count, uint64_t first_played)
    -> bool {
    auto key = app + ":" + id;
    std::lock_guard<std::mutex> lck(_mtx);
    auto it = _auth_entry_map.find(key);
    if (it == _auth_entry_map.end()) {
        return false;
    }

    auto uri = it->second._url;
    HttpRequester::Ptr requester(new HttpRequester);
    Json::Value value;
    value["type"] = "media_changed";
    value["session_id"] = it->second._session_id;
    value["total_reader"] = reader_count;
    value["first_player_created"] = first_played;
    requester->setMethod("POST");
    requester->setBody(value.toStyledString());
    requester->addHeader("Content-Type", "application/json");
    requester->startRequester(
        uri,
        [app, id, requester](const toolkit::SockException &ex, const Parser &response) {
            InfoL << "app=" << app << ", stream_id=" << id << ", media_changed response:" << response.Content();
            if (ex) {
                return;
            }
        },
        2.0);
    return true;
}
std::string AuthCenter::generate_key(const mediakit::MediaInfo &info) {
    return info._app + ":" + info._streamid;
}