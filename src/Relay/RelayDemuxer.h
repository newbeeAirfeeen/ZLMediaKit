#pragma once

#include "Codec/FrameTranscoder.h"

class MediaPackage;

namespace mediakit {
namespace relay {

class Demuxer {
public:
    explicit Demuxer(FrameConsumer output);
    void input(const MediaPackage &packet);
    std::vector<Track::Ptr> tracks() const;

private:
    void videoConfig(const std::string &data, uint32_t stamp);
    void audioConfig(const std::string &data);
    Track::Ptr _video, _audio;
    FrameConsumer _video_input, _audio_input, _output;
    CodecId _input_video = CodecInvalid, _input_audio = CodecInvalid;
    std::string _video_config, _audio_config;
    bool _convert_video, _convert_audio;
    int _max_height;
    bool _has_aac_stamp = false;
    uint32_t _last_aac_stamp = 0;
};

} // namespace relay
} // namespace mediakit
