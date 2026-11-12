//
// Created by shenhao on 2024/9/2.
//

#ifndef ZLMEDIAKIT_AUTHCENTER_H
#define ZLMEDIAKIT_AUTHCENTER_H
#include "Common/MediaSource.h"
#include "Common/config.h"
#include "Common/SessionMeta.h"
#include <string>

struct AuthEntry {
    std::string _url;
    std::string _session_id;
    uint64_t _owner = 0;
    std::weak_ptr<mediakit::SessionMeta> _meta;
};
class AuthCenter {
public:
    static AuthCenter &instance();

public:
    // lease 由媒体源持有，覆盖断连续推窗口；旧 lease 不能删除新发布者路由。
    std::shared_ptr<void> regist_auth(const mediakit::MediaInfo &, const std::string &url,
                                    const std::string &session_id, const mediakit::SessionMeta::Ptr &meta = nullptr);
    void unreigst_auth(const mediakit::MediaInfo &, uint64_t owner);
    mediakit::SessionMeta::Ptr sessionMeta(const mediakit::MediaInfo &info);
    auto auth(const mediakit::MediaInfo &info, const mediakit::Broadcast::AuthInvoker &invoker) -> bool;
    auto media_changed(const mediakit::MediaInfo &info, int reader_count, uint64_t first_played) -> bool;

private:
    std::string generate_key(const mediakit::MediaInfo &info);

private:
    std::mutex _mtx;
    uint64_t _next_owner = 0;
    std::unordered_map<std::string, AuthEntry> _auth_entry_map;
};

#endif // ZLMEDIAKIT_AUTHCENTER_H
