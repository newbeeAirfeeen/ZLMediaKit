/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <ctime>
#include <sys/stat.h>
#include "HlsMakerImp.h"
#include "Util/util.h"
#include "Util/uv_errno.h"
#include "Util/File.h"
#include "Common/config.h"

using namespace std;
using namespace toolkit;

namespace mediakit {
namespace {
// A delayed cleanup may outlive its player and a new pull may reuse the URL.
mutex writer_mutex;
map<string, uint64_t> writers;
uint64_t next_writer = 0;

bool isSegmentDirectory(const string &name, const string &stream) {
    const string prefix = stream + "_";
    if (name.size() != prefix.size() + 10 || name.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    for (size_t i = 0; i < 10; ++i) {
        char ch = name[prefix.size() + i];
        if (i == 4 || i == 7) {
            if (ch != '-') { return false; }
        } else if (ch < '0' || ch > '9') { return false; }
    }
    return true;
}
} // namespace

HlsMakerImp::HlsMakerImp(const string &m3u8_file,
                         const string &params,
                         uint32_t bufSize,
                         float seg_duration,
                         uint32_t seg_number,
                         bool seg_keep):HlsMaker(seg_duration, seg_number, seg_keep) {
    _poller = EventPollerPool::Instance().getPoller();
    _path_prefix = m3u8_file.substr(0, m3u8_file.rfind('/'));
    _path_hls = m3u8_file;
    // _path_hls = "/streamid.m3u8";
    size_t pos1 = m3u8_file.rfind('/');
    size_t pos2 = m3u8_file.rfind('.');
    _stream_id = m3u8_file.substr(pos1 + 1, pos2 - pos1 - 1);
    _params = params;
    _buf_size = bufSize;
    _file_buf.reset(new char[bufSize], [](char *ptr) {
        delete[] ptr;
    });

    _info.folder = _path_prefix;
    lock_guard<mutex> lock(writer_mutex);
    _writer_id = ++next_writer;
    writers[_path_hls] = _writer_id;
}

HlsMakerImp::~HlsMakerImp() {
    clearCache(false, true);
}

void HlsMakerImp::clearCache() {
    clearCache(true, false);
}

void HlsMakerImp::clearCache(bool immediately, bool eof) {
    if (eof) {
        lock_guard<mutex> lock(writer_mutex);
        auto current = writers.find(_path_hls);
        if (current == writers.end() || current->second != _writer_id) { return; }
    }
    //录制完了
    flushLastSegment(eof);
    if (!isLive() || isKeep()) {
        if (eof) {
            lock_guard<mutex> lock(writer_mutex);
            auto it = writers.find(_path_hls);
            if (it != writers.end() && it->second == _writer_id) { writers.erase(it); }
        }
        return;
    }

    clear();
    _file = nullptr;
    _segment_file_paths.clear();

    auto index = _path_hls;
    auto stream = _stream_id;
    auto parent = _path_prefix;
    auto nested = _info.stream.rfind('/');
    if (nested != string::npos) { parent += "/" + _info.stream.substr(0, nested); }
    auto writer = _writer_id;
    auto cleanup = [index, stream, parent, writer, eof]() -> uint64_t {
        lock_guard<mutex> lock(writer_mutex);
        auto current = writers.find(index);
        if (current == writers.end() || current->second != writer) { return 0; }
        if (!File::is_dir(index.c_str())) { File::delete_file(index.c_str()); }
        File::scanDir(parent, [&](const string &path, bool directory) {
            auto name = path.substr(path.find_last_of('/') + 1);
            if ((!directory && name == stream + "_init.mp4") || (directory && isSegmentDirectory(name, stream))) {
                File::delete_file(path.c_str());
            }
            return true;
        });
        if (eof) { writers.erase(current); }
        return 0;
    };
    GET_CONFIG(uint32_t, delay, Hls::kDeleteDelaySec);
    if (!delay || immediately) { cleanup(); }
    else { _poller->doDelayTask(delay * 1000, std::move(cleanup)); }
}

string HlsMakerImp::onOpenSegment(uint64_t index, bool init_mp4 /*= false*/, bool use_fmp4 /* = false*/) {
    string segment_name, segment_path;
    {
        auto strDate = getTimeStr("%Y-%m-%d");
        auto strHour = getTimeStr("%H");
        auto strTime = getTimeStr("%M-%S");
        if (init_mp4) {
            segment_name = StrPrinter << _info.stream << "_init.mp4"; // init segment存储文件名
        } else {
            segment_name = StrPrinter << (_info.stream + "_" + strDate + "/" + strHour + "/" + strTime) << "_" << index << (use_fmp4 ? ".m4s" : ".ts");
        }
        segment_path = _path_prefix + "/" + segment_name;
        if (isLive() && !init_mp4) {
            _segment_file_paths.emplace(index, segment_path);
        }
    }
    _file = makeFile(segment_path, true);

    //保存本切片的元数据
    _info.start_time = ::time(NULL);
    _info.file_name = segment_name;
    _info.file_path = segment_path;
    _info.url = _info.app + "/" + _info.stream + "/" + segment_name;

    if (!_file) {
        WarnL << "create file failed," << segment_path << " " << get_uv_errmsg();
    }
    if (_params.empty()) {
        return segment_name;
    }
    return segment_name + "?" + _params;
}

bool HlsMakerImp::fileExist(const std::string &file_name)
{
    std::string file_name_full = _path_prefix + "/" + file_name.substr(0, file_name.find('?'));
    return File::fileExist(file_name_full.c_str());
}


void HlsMakerImp::onDelSegment(uint64_t index) {
    auto it = _segment_file_paths.find(index);
    if (it == _segment_file_paths.end()) {
        return;
    }
    File::delete_file(it->second.data());
    _segment_file_paths.erase(it);
}

void HlsMakerImp::onWriteSegment(const char *data, size_t len) {
    if (_file) {
        fwrite(data, len, 1, _file.get());
    }
    if (_media_src) {
        _media_src->onSegmentSize(len);
    }
}

void HlsMakerImp::onWriteHls(const std::string &data) {
    auto hls = makeFile(_path_hls);
    if (hls) {
        fwrite(data.data(), data.size(), 1, hls.get());
        hls.reset();
        if (_media_src) {
            _media_src->setIndexFile(data);
        }
    } else {
        WarnL << "create hls file failed," << _path_hls << " " << get_uv_errmsg();
    }
    //DebugL << "\r\n"  << string(data,len);
}

void HlsMakerImp::onFlushLastSegment(uint64_t duration_ms) {
    //关闭并flush文件到磁盘
    _file = nullptr;

    GET_CONFIG(bool, broadcastRecordTs, Hls::kBroadcastRecordTs);
    if (broadcastRecordTs) {
        _info.time_len = duration_ms / 1000.0f;
        _info.file_size = File::fileSize(_info.file_path.data());
        NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastRecordTs, _info);
    }
}

std::shared_ptr<FILE> HlsMakerImp::makeFile(const string &file, bool setbuf) {
    auto file_buf = _file_buf;
    auto ret = shared_ptr<FILE>(File::create_file(file.data(), "wb"), [file_buf](FILE *fp) {
        if (fp) {
            fclose(fp);
        }
    });
    if (ret && setbuf) {
        setvbuf(ret.get(), _file_buf.get(), _IOFBF, _buf_size);
    }
    return ret;
}

void HlsMakerImp::setMediaSource(const string &vhost, const string &app, const string &stream_id) {
    _media_src = std::make_shared<HlsMediaSource>(vhost, app, stream_id);
    _info.app = app;
    _info.stream = stream_id;
    _info.vhost = vhost;
}

HlsMediaSource::Ptr HlsMakerImp::getMediaSource() const {
    return _media_src;
}

}//namespace mediakit