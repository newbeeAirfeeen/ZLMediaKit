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

std::shared_ptr<void> AuthCenter::regist_auth(const MediaInfo &info, const std::string &url,
                                            const std::string &session_id, const SessionMeta::Ptr &meta) {
    auto key = generate_key(info);
    std::lock_guard<std::mutex> lck(_mtx);
    AuthEntry entry;
    entry._url = url;
    entry._session_id = session_id;
    entry._owner = ++_next_owner;
    entry._meta = meta;
    auto owner = entry._owner;
    _auth_entry_map[key] = entry;
    return std::shared_ptr<void>(new uint64_t(owner), [this, info, owner](void *p) {
        delete static_cast<uint64_t *>(p);
        unreigst_auth(info, owner);
    });
}
void AuthCenter::unreigst_auth(const MediaInfo &info, uint64_t owner) {
    std::lock_guard<std::mutex> lck(_mtx);
    auto it = _auth_entry_map.find(generate_key(info));
    if (it != _auth_entry_map.end() && it->second._owner == owner) { _auth_entry_map.erase(it); }
}
SessionMeta::Ptr AuthCenter::sessionMeta(const MediaInfo &info) {
    std::lock_guard<std::mutex> lck(_mtx);
    auto it = _auth_entry_map.find(generate_key(info));
    return it == _auth_entry_map.end() ? nullptr : it->second._meta.lock();
}
auto AuthCenter::auth(const mediakit::MediaInfo &info, const mediakit::Broadcast::AuthInvoker &invoker) -> bool {
    auto key = generate_key(info);
    AuthEntry entry;
    {
        std::lock_guard<std::mutex> lck(_mtx);
        auto it = _auth_entry_map.find(key);
        if (it == _auth_entry_map.end()) {
            EventPollerPool::Instance().getPoller(false)->async([invoker]() {
                InfoL << "动态鉴权中心无人鉴权";
                invoker("");
            }, false);
            return false;
        }
        entry = it->second;
    }
    auto uri = entry._url;
    HttpRequester::Ptr requester(new HttpRequester);
    Json::Value value;
    value["type"] = "auth";
    value["session_id"] = entry._session_id;
    value["url"] = info._full_url;
    requester->setMethod("POST");
    requester->setBody(value.toStyledString());
    requester->addHeader("Content-Type", "application/json");
    requester->startRequester(
        uri,
        [invoker, info, requester](const toolkit::SockException &ex, const Parser &response) {
            InfoL << "app=" << info._app << ", stream_id=" << info._streamid
                  << ", auth response:" << response.Content();
            if (ex) {
                invoker(ex.what());
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
        10.0);
    return true;
}
auto AuthCenter::media_changed(const MediaInfo &info, int reader_count, uint64_t first_played)
    -> bool {
    auto key = generate_key(info);
    auto app = info._app;
    auto id = info._streamid;
    AuthEntry entry;
    {
        std::lock_guard<std::mutex> lck(_mtx);
        auto it = _auth_entry_map.find(key);
        if (it == _auth_entry_map.end()) {
            return false;
        }

        entry = it->second;
    }
    if (auto meta = entry._meta.lock()) {
        if (meta->isObserved()) { return true; } // v2 使用有效播放会话，避免内部 reader 重复通知。
    }
    auto uri = entry._url;
    HttpRequester::Ptr requester(new HttpRequester);
    Json::Value value;
    value["type"] = "media_changed";
    value["session_id"] = entry._session_id;
    value["total_reader"] = static_cast<uint64_t>(reader_count);
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
        10.0);
    return true;
}
std::string AuthCenter::generate_key(const mediakit::MediaInfo &info) {
    return info._vhost + "\n" + info._app + "\n" + info._streamid;
}
