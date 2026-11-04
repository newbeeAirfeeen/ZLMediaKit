#include "SessionMeta.h"
#include "server/AuthCenter.h"
#include "Http/HttpRequester.h"
#include <limits>
#include <sstream>

using namespace toolkit;
using namespace std;
namespace mediakit {

bool SessionMeta::parseUint64(const string &value, uint64_t &number) {
    if (value.empty()) { return false; }
    uint64_t result = 0;
    for (auto c : value) {
        if (c < '0' || c > '9' || result > (numeric_limits<uint64_t>::max() - (c - '0')) / 10) {
            return false;
        }
        result = result * 10 + c - '0';
    }
    number = result;
    return true;
}

bool SessionMeta::parseConnect(const AMFValue &params, Snapshot &out) {
    try {
        const auto &control = params["playbackControl"];
        if (control.type() != AMF_OBJECT || control["version"].as_number() != 1 ||
            control["mode"].as_string() != "playback") { return false; }
        Snapshot s;
        s.controlled = true;
        s.mode = "playback";
        s.session_id = params["sessionId"].as_string();
        s.hook_url = params["hookUrl"].as_string();
        s.source_type = control["source_type"].as_string();
        auto state = control["target_state"].as_string();
        if (s.session_id.empty() || s.hook_url.empty() ||
            (s.source_type != "cloud" && s.source_type != "sdcard") ||
            (state != "paused" && state != "playing")) { return false; }
        if (!parseUint64(control["publish_epoch"].as_string(), s.epoch) || !s.epoch ||
            !parseUint64(control["state_version"].as_string(), s.version)) { return false; }
        s.can_pause = control["capabilities"]["pause"].as_boolean();
        s.can_resume = control["capabilities"]["resume"].as_boolean();
        s.paused = state == "paused";
        out = std::move(s);
        return true;
    } catch (const exception &) { return false; }
}

// 生命周期元信息独立于 playbackControl：直播及禁用暂停控制的回放也要上报。
bool SessionMeta::parseLifecycle(const AMFValue &params, Snapshot &out) {
    try {
        const auto &lifecycle = params["streamLifecycle"];
        if (lifecycle.type() != AMF_OBJECT || lifecycle["version"].as_number() != 1) { return false; }
        Snapshot s;
        s.observed = true;
        s.session_id = params["sessionId"].as_string();
        s.hook_url = params["hookUrl"].as_string();
        s.task_instance = lifecycle["task_instance"].as_string();
        s.mode = lifecycle["mode"].as_string();
        if (s.session_id.empty() || s.hook_url.empty() || s.task_instance.empty() ||
            (s.mode != "live" && s.mode != "playback") ||
            !parseUint64(lifecycle["publish_epoch"].as_string(), s.epoch) || !s.epoch) { return false; }
        out = std::move(s);
        return true;
    } catch (const exception &) { return false; }
}

SessionMeta::SessionMeta(const MediaInfo &stream, const Snapshot &s, const EventPoller::Ptr &owner)
    : _stream(stream), _owner(owner), _snapshot(s) {}
SessionMeta::Ptr SessionMeta::find(const MediaInfo &stream) {
    return AuthCenter::instance().sessionMeta(stream);
}
SessionMeta::Snapshot SessionMeta::snapshot() const {
    lock_guard<mutex> lock(_mutex);
    return _snapshot;
}
bool SessionMeta::isPaused() const { auto s = snapshot(); return s.controlled && s.paused; }
bool SessionMeta::isControlled() const { return snapshot().controlled; }
bool SessionMeta::isObserved() const { return snapshot().observed; }
bool SessionMeta::bind(const Snapshot &s) {
    lock_guard<mutex> lock(_mutex);
    if (_snapshot.stopped || _snapshot.session_id != s.session_id || s.epoch <= _snapshot.epoch ||
        s.version < _snapshot.version || _snapshot.paused ||
        s.controlled != _snapshot.controlled || s.observed != _snapshot.observed ||
        s.task_instance != _snapshot.task_instance || s.mode != _snapshot.mode) { return false; }
    auto ready = _snapshot.ready;
    _snapshot = s;
    _snapshot.ready = ready;
    _next_seq = 0;
    return true;
}
void SessionMeta::setReady(bool ready) {
    auto self = shared_from_this();
    _owner->async([self, ready]() {
        {
            lock_guard<mutex> lock(self->_mutex);
            if (self->_snapshot.stopped) { return; }
            self->_snapshot.ready = ready;
        }
        self->log("ready");
    });
}
void SessionMeta::disconnected() {
    lock_guard<mutex> lock(_mutex);
    _snapshot.connected = false;
    if (_snapshot.paused) { _snapshot.stopped = true; _snapshot.ready = false; }
}
void SessionMeta::stop() {
    lock_guard<mutex> lock(_mutex);
    _snapshot.connected = false;
    _snapshot.stopped = true;
    _snapshot.ready = false;
}
void SessionMeta::playerJoined(const string &player, const string &protocol) {
    auto self = shared_from_this();
    _owner->async([self, player, protocol]() {
        if (self->_players.emplace(player, protocol).second) {
            self->_ever_played = true;
            self->log(self->_players.size() > 1 ? "second_player_join" : "player_join", player);
            self->notifyPlayers("player_join", player, protocol);
        }
    });
}
void SessionMeta::playerLeft(const string &player) {
    auto self = shared_from_this();
    _owner->async([self, player]() {
        auto it = self->_players.find(player);
        if (it == self->_players.end()) { return; }
        auto protocol = it->second;
        self->_players.erase(it);
        ++self->_disconnect_count;
        if (self->_players.empty()) { ++self->_all_left_count; }
        self->log("player_leave", player);
        self->notifyPlayers("player_leave", player, protocol);
    });
}
void SessionMeta::notifyPlayers(const char *event, const string &player, const string &protocol) {
    auto s = snapshot();
    if (!s.observed) { return; }
    Json::Value body;
    body["type"] = "media_changed";
    body["version"] = 2;
    body["session_id"] = s.session_id;
    body["task_instance"] = s.task_instance;
    body["publish_epoch"] = to_string(s.epoch);
    body["event_seq"] = to_string(++_event_seq);
    body["event"] = event;
    body["player_id"] = player;
    body["protocol"] = protocol;
    body["player_count"] = static_cast<Json::UInt64>(_players.size());
    body["ever_played"] = _ever_played;
    // 当前人数可能先收到 1→0→1 的最后一个快照，累计事实不能随人数重置。
    body["disconnect_count"] = to_string(_disconnect_count);
    body["all_left_count"] = to_string(_all_left_count);
    InfoL << "STREAM_POLICY event=" << event << " session_id=" << s.session_id
          << " task_instance=" << s.task_instance << " publish_epoch=" << s.epoch
          << " event_seq=" << _event_seq << " protocol=" << protocol << " player_id=" << player
          << " player_count=" << _players.size() << " disconnect_count=" << _disconnect_count
          << " all_left_count=" << _all_left_count;
    auto requester = make_shared<HttpRequester>();
    requester->setMethod("POST");
    requester->addHeader("Content-Type", "application/json");
    requester->setBody(body.toStyledString());
    auto seq = _event_seq;
    try {
        requester->startRequester(s.hook_url, [requester, s, seq](const SockException &ex, const Parser &response) {
            // 只上报事实，hook 响应不在 ZLM 执行释放策略；不新增重试/持久化机制。
            if (ex || response.Url() != "200") {
                WarnL << "STREAM_POLICY event=hook_failed session_id=" << s.session_id
                      << " task_instance=" << s.task_instance << " event_seq=" << seq
                      << " error=" << ex.what() << " http_status=" << response.Url();
            }
        }, 10.0f);
    } catch (const exception &ex) {
        requester->clear();
        WarnL << "STREAM_POLICY event=hook_failed session_id=" << s.session_id
              << " task_instance=" << s.task_instance << " event_seq=" << seq << " error=" << ex.what();
    }
}
void SessionMeta::log(const char *event, const string &player, uint64_t seq, const string &result) const {
    auto s = snapshot();
    InfoL << "PLAYBACK_STATE event=" << event << " session_id=" << s.session_id
          << " stream=" << _stream.shortUrl() << " publish_epoch=" << s.epoch
          << " request_seq=" << seq << " state_version=" << s.version
          << " player_id=" << player << " readers=" << _players.size()
          << " ready=" << s.ready << " target_state=" << (s.paused ? "paused" : "playing")
          << " result=" << result;
}

SessionMeta::Result SessionMeta::mergeResponse(const Json::Value &r, uint64_t epoch, uint64_t seq) {
    lock_guard<mutex> lock(_mutex);
    auto &s = _snapshot;
    if (s.stopped) { return {41001, "task_stopped"}; }
    if (!s.connected) { return {40904, "not_ready"}; }
    if (s.epoch != epoch) { return {40903, "epoch_mismatch"}; }
    uint64_t reply_epoch, reply_seq;
    if (!r.isObject() || !r["code"].isInt() || !r["result"].isString() || !r["session_id"].isString() ||
        r["session_id"].asString() != s.session_id ||
        !r["publish_epoch"].isString() || !parseUint64(r["publish_epoch"].asString(), reply_epoch) ||
        !r["request_seq"].isString() || !parseUint64(r["request_seq"].asString(), reply_seq) ||
        reply_epoch != epoch || reply_seq != seq) { return {500, "invalid_hook_response"}; }
    int code = r["code"].asInt();
    auto result = r["result"].asString();
    if (code == 0 && result != "accepted" && result != "unchanged" && result != "duplicate") {
        return {500, "invalid_hook_response"};
    }
    const auto &current = r["current"];
    if (!current.isObject()) { return code ? Result(code, result) : Result(500, "missing_snapshot"); }
    uint64_t current_epoch, version, last_seq;
    if (!current["publish_epoch"].isString() || !parseUint64(current["publish_epoch"].asString(), current_epoch) ||
        !current["state_version"].isString() || !parseUint64(current["state_version"].asString(), version) ||
        !current["last_accepted_seq"].isString() || !parseUint64(current["last_accepted_seq"].asString(), last_seq) ||
        !current["target_state"].isString()) { return {500, "invalid_snapshot"}; }
    auto target = current["target_state"].asString();
    if (current_epoch != s.epoch) { return {40903, "epoch_mismatch"}; }
    if (target != "playing" && target != "paused") { return {500, "invalid_snapshot"}; }
    if (!code && last_seq < seq) { return {500, "invalid_snapshot"}; }
    // 按权威版本合并，而非最后发送序号；较新的请求可能被 SP 拒绝。
    if (version > s.version) {
        s.version = version;
        s.paused = target == "paused";
    } else if (version == s.version && s.paused != (target == "paused")) {
        return {500, "conflicting_snapshot"};
    }
    s.last_seq = max(s.last_seq, last_seq);
    _next_seq = max(_next_seq, last_seq);
    if (!code && seq < s.last_seq) { return {40901, "stale"}; }
    return {code, result};
}

void SessionMeta::control(bool paused, const string &player, const string &protocol,
                          const Json::Value &context, const EventPoller::Ptr &reply_poller, Completion completion) {
    auto self = shared_from_this();
    _owner->async([self, paused, player, protocol, context, reply_poller, completion]() {
        auto done = [reply_poller, completion](const Result &result) {
            reply_poller->async([completion, result]() { completion(result); });
        };
        Snapshot s;
        uint64_t seq = 0;
        Result validation;
        {
            lock_guard<mutex> lock(self->_mutex);
            s = self->_snapshot;
            if (!s.controlled) { validation = {50101, "unsupported"}; }
            else if (s.stopped) { validation = {41001, "task_stopped"}; }
            else if (!s.connected || !s.ready) { validation = {40904, "not_ready"}; }
            else if (!(paused ? s.can_pause : s.can_resume)) { validation = {50101, "unsupported"}; }
            else if (self->_next_seq == numeric_limits<uint64_t>::max()) { validation = {500, "sequence_exhausted"}; }
            else { seq = ++self->_next_seq; }
        }
        if (!validation.accepted()) {
            self->log("control_rejected", player, seq, validation.result);
            done(validation); return;
        }
        Json::Value body;
        body["type"] = "control";
        body["session_id"] = s.session_id;
        body["publish_epoch"] = to_string(s.epoch);
        body["request_seq"] = to_string(seq);
        body["action"] = paused ? "pause" : "resume";
        body["player_id"] = player;
        body["protocol"] = protocol;
        body["params"] = Json::Value(Json::objectValue);
        body["request_context"] = context;
        self->log("control_send", player, seq);
        auto requester = make_shared<HttpRequester>();
        requester->setMethod("POST");
        requester->addHeader("Content-Type", "application/json");
        requester->setBody(body.toStyledString());
        auto epoch = s.epoch;
        try {
            requester->startRequester(s.hook_url, [self, requester, done, epoch, seq, player](const SockException &ex, const Parser &response) {
                // HttpRequester 不一定运行在 owner poller，解析和合并统一切回。
                auto content = response.Content();
                auto ok = !ex && response.Url() == "200";
                self->_owner->async([self, done, epoch, seq, player, content, ok]() {
                    Result result(500, "hook_transport_error");
                    if (ok) {
                        try {
                            Json::Value response;
                            istringstream input(content);
                            input >> response;
                            result = self->mergeResponse(response, epoch, seq);
                        } catch (const exception &) { result = {500, "invalid_hook_response"}; }
                    }
                    self->log("control_result", player, seq, result.result);
                    done(result);
                });
            }, 10.0f);
        } catch (const exception &) {
            // 构造/发起 HTTP 也可能同步失败，必须结束原协议请求，不能只留下待回复状态。
            requester->clear();
            self->log("control_result", player, seq, "hook_transport_error");
            done({500, "hook_transport_error"});
        }
    });
}

void SessionMeta::update(const AMFValue &message) {
    // 发布者主动同步目标状态，不会再次产生 control hook。
    Json::Value value;
    try {
        for (auto key : {"session_id", "publish_epoch", "state_version", "target_state"}) {
            value[key] = message[key].as_string();
        }
    } catch (const exception &) { return; }
    auto self = shared_from_this();
    _owner->async([self, value]() {
        {
            lock_guard<mutex> lock(self->_mutex);
            auto &s = self->_snapshot;
            uint64_t epoch, version;
            auto target = value["target_state"].asString();
            if (!s.controlled || s.stopped || !s.connected || value["session_id"].asString() != s.session_id ||
                !parseUint64(value["publish_epoch"].asString(), epoch) || epoch != s.epoch ||
                !parseUint64(value["state_version"].asString(), version) || version <= s.version ||
                (target != "paused" && target != "playing")) { return; }
            s.version = version;
            s.paused = target == "paused";
        }
        self->log("publisher_snapshot");
    });
}
} // namespace mediakit
