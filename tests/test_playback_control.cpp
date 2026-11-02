#include "Common/SessionMeta.h"
#include "Rtmp/RtmpMediaSourceImp.h"
#include "../server/AuthCenter.h"
#include "Poller/EventPoller.h"
#include <iostream>
#include <stdexcept>

using namespace mediakit;
using namespace toolkit;
using namespace std;

static void require(bool ok, const char *message) {
    if (!ok) { throw runtime_error(message); }
}
static Json::Value response(uint64_t seq, uint64_t version, bool paused, int code = 0) {
    Json::Value r;
    r["code"] = code;
    r["result"] = code ? "unsupported" : "accepted";
    r["session_id"] = "task";
    r["publish_epoch"] = "1";
    r["request_seq"] = to_string(seq);
    auto &current = r["current"];
    current["publish_epoch"] = "1";
    current["state_version"] = to_string(version);
    current["last_accepted_seq"] = to_string(seq);
    current["target_state"] = paused ? "paused" : "playing";
    return r;
}
int main() {
    try {
        EventPollerPool::setPoolSize(1);
        auto poller = EventPollerPool::Instance().getPoller();
        uint64_t number;
        require(SessionMeta::parseUint64("18446744073709551615", number) && number == UINT64_MAX, "uint64 max");
        require(!SessionMeta::parseUint64("18446744073709551616", number), "overflow rejected");
        require(!SessionMeta::parseUint64("-1", number) && !SessionMeta::parseUint64("1x", number), "malformed seq");
        AMFValue params(AMF_OBJECT), control(AMF_OBJECT), caps(AMF_OBJECT);
        params.set("sessionId", "task"); params.set("hookUrl", "http://127.0.0.1/control");
        control.set("version", 1.0); control.set("mode", "playback"); control.set("source_type", "cloud");
        control.set("publish_epoch", "9007199254740993"); control.set("state_version", "0");
        control.set("target_state", "paused"); caps.set("pause", true); caps.set("resume", true);
        control.set("capabilities", caps); params.set("playbackControl", control);
        SessionMeta::Snapshot s;
        require(SessionMeta::parseConnect(params, s) && s.epoch == 9007199254740993ULL && s.paused, "AMF exact uint64");
        const_cast<AMFValue &>(control["publish_epoch"]) = AMFValue(9007199254740992.0);
        const_cast<AMFValue &>(params["playbackControl"]) = control;
        require(!SessionMeta::parseConnect(params, s), "numeric epoch rejected");
        s.session_id = "task"; s.epoch = 1; s.version = 0; s.paused = false;
        MediaInfo stream("rtmp://127.0.0.1/live/test");
        auto meta = make_shared<SessionMeta>(stream, s, poller);
        require(meta->mergeResponse(response(2, 2, false), 1, 2).accepted(), "newer response accepted");
        require(meta->mergeResponse(response(1, 1, true), 1, 1).code == 40901, "old success reported as stale");
        require(meta->snapshot().version == 2 && !meta->isPaused(), "old response cannot undo resume");
        require(!meta->mergeResponse(response(3, 2, true), 1, 3).accepted(), "same-version conflict rejected");
        Json::Value legacy; legacy["code"] = 0;
        require(!meta->mergeResponse(legacy, 1, 4).accepted(), "legacy unknown hook success rejected");
        auto rejected = response(5, 2, false, 50101);
        rejected["current"]["last_accepted_seq"] = "2";
        require(!meta->mergeResponse(rejected, 1, 5).accepted(), "newer rejected request");
        require(meta->mergeResponse(response(4, 3, true), 1, 4).accepted() && meta->isPaused(), "older accepted request still updates target");
        auto next = s; next.epoch = 2; next.version = 3;
        require(!meta->bind(next), "paused task cannot reconnect");
        meta->disconnected();
        require(meta->snapshot().stopped, "paused disconnect is terminal");
        require(!meta->mergeResponse(response(6, 4, false), 1, 6).accepted(), "late resume cannot revive task");

        auto playing = make_shared<SessionMeta>(stream, s, poller);
        playing->disconnected();
        require(playing->bind(next), "playing stream retains existing reconnect policy");
        require(playing->mergeResponse(response(9, 9, true), 1, 9).code == 40903, "old epoch callback ignored");
        AMFValue snapshot(AMF_OBJECT);
        snapshot.set("session_id", "task"); snapshot.set("publish_epoch", "2");
        snapshot.set("state_version", "4"); snapshot.set("target_state", "paused");
        playing->update(snapshot);
        poller->sync([]() {});
        require(playing->isPaused() && playing->snapshot().version == 4, "publisher snapshot updates state");
        const_cast<AMFValue &>(snapshot["state_version"]) = AMFValue("3");
        const_cast<AMFValue &>(snapshot["target_state"]) = AMFValue("playing");
        playing->update(snapshot);
        poller->sync([]() {});
        require(playing->isPaused(), "old publisher snapshot ignored");
        auto &auth = AuthCenter::instance();
        auto lease1 = auth.regist_auth(stream, s.hook_url, "task", playing);
        auto replacement = make_shared<SessionMeta>(stream, s, poller);
        auto lease2 = auth.regist_auth(stream, s.hook_url, "task", replacement);
        lease1.reset();
        require(SessionMeta::find(stream) == replacement, "old owner cleanup must not remove new binding");
        auto other = stream; other._vhost = "different-vhost";
        require(!SessionMeta::find(other), "vhost route isolation");
        lease2.reset();
        require(!SessionMeta::find(stream), "source lease cleanup");
        auto source = make_shared<RtmpMediaSourceImp>("test", "live", "retained");
        weak_ptr<RtmpMediaSourceImp> weak_source = source;
        source->retainForReconnect(poller, 15000);
        source->cancelReconnectRetention();
        source.reset();
        require(weak_source.expired(), "old reconnect timer must not retain a republished source");
        cout << "playback control tests passed" << endl;
        return 0;
    } catch (const exception &ex) {
        cerr << "playback control test failed: " << ex.what() << endl;
        return 1;
    }
}
