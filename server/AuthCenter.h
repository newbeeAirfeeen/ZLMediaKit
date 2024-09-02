//
// Created by shenhao on 2024/9/2.
//

#ifndef ZLMEDIAKIT_AUTHCENTER_H
#define ZLMEDIAKIT_AUTHCENTER_H
#include "Common/MediaSource.h"
#include "Common/config.h"
#include <string>

struct AuthEntry {
    std::string _url;
    std::string _session_id;
};
class AuthCenter {
public:
    static AuthCenter &instance();

public:
    void regist_auth(const mediakit::MediaInfo &, const std::string &url, const std::string &session_id);
    void unreigst_auth(const mediakit::MediaInfo &);
    auto auth(const mediakit::MediaInfo &info, const mediakit::Broadcast::AuthInvoker &invoker) -> bool;
    auto media_changed(const std::string& app, const std::string& id, int reader_count, uint64_t first_played) -> bool;

private:
    std::string generate_key(const mediakit::MediaInfo &info);

private:
    std::mutex _mtx;
    std::unordered_map<std::string, AuthEntry> _auth_entry_map;
};

#endif // ZLMEDIAKIT_AUTHCENTER_H
