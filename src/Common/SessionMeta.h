#pragma once

#include <mutex>
#include <map>
#include "Common/MediaSource.h"
#include "Rtmp/amf.h"
#include "json/json.h"

namespace mediakit {

// 一条 SP 任务的播放观测与可选回放控制镜像。观测不赋予控制能力。
// 请求分配、HTTP 完成和状态同步固定在 owner poller。发布绑定/断开可来自其他
// poller，使用同一把锁校验并更新快照，不跨线程读写裸状态字段。
class SessionMeta : public std::enable_shared_from_this<SessionMeta> {
public:
    using Ptr = std::shared_ptr<SessionMeta>;
    struct Snapshot {
        std::string session_id, source_type, hook_url;
        std::string task_instance, mode;
        bool observed = false, controlled = false;
        bool can_pause = false, can_resume = false, paused = false;
        bool ready = false, connected = true, stopped = false;
        uint64_t epoch = 0, version = 0, last_seq = 0;
    };
    struct Result {
        int code;
        std::string result;
        Result(int code = 0, std::string result = "accepted") : code(code), result(std::move(result)) {}
        bool accepted() const { return code == 0; }
    };
    using Completion = std::function<void(const Result &)>;

    SessionMeta(const MediaInfo &stream, const Snapshot &snapshot, const toolkit::EventPoller::Ptr &owner);
    static bool parseConnect(const AMFValue &params, Snapshot &snapshot);
    static bool parseLifecycle(const AMFValue &params, Snapshot &snapshot);
    static bool parseUint64(const std::string &value, uint64_t &number);
    static Ptr find(const MediaInfo &stream);
    Snapshot snapshot() const;
    bool isPaused() const;
    bool isControlled() const;
    bool isObserved() const;
    // 重推只适用于非暂停任务；同一源复用上下文，播放器无需更换引用。
    bool bind(const Snapshot &snapshot);
    void setReady(bool ready);
    void disconnected();
    void stop();
    void update(const AMFValue &message);
    void playerJoined(const std::string &player, const std::string &protocol);
    void playerLeft(const std::string &player);
    void control(bool paused, const std::string &player, const std::string &protocol,
                 const Json::Value &request_context, const toolkit::EventPoller::Ptr &reply_poller,
                 Completion completion);
    // 单独暴露协议结果合并，便于验证乱序响应；调用者须在 owner poller。
    Result mergeResponse(const Json::Value &response, uint64_t epoch, uint64_t seq);

private:
    void notifyPlayers(const char *event, const std::string &player, const std::string &protocol);
    void log(const char *event, const std::string &player = "", uint64_t seq = 0,
             const std::string &result = "") const;
    MediaInfo _stream;
    toolkit::EventPoller::Ptr _owner;
    mutable std::mutex _mutex;
    Snapshot _snapshot;
    uint64_t _next_seq = 0;
    std::map<std::string, std::string> _players;
    uint64_t _event_seq = 0, _disconnect_count = 0, _all_left_count = 0;
    bool _ever_played = false;
};
} // namespace mediakit
