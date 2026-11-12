// Live MediaPackage mapping ported from streamproxy/NetWorkTool.
#include "RelayDemuxer.h"
#include "Extension/AAC.h"
#include "Extension/G711.h"
#include "Extension/H264.h"
#include "Extension/H265.h"
#include "RelayConfig.h"
#include "relay_message.pb.h"

namespace mediakit {
namespace relay {
namespace {

void videoFrames(CodecId codec, const char *data, size_t size, uint32_t stamp, const FrameConsumer &consume) {
    auto prefix = prefixSize(data, size);
    if (!prefix || size < prefix + (codec == CodecH265 ? 3 : 2)) {
        throw std::runtime_error("invalid relay Annex B packet");
    }
    splitH264(data, size, prefix, [&](const char *nal, size_t length, size_t offset) {
        if (length < offset + (codec == CodecH265 ? 3 : 2)) {
            throw std::runtime_error("short relay video NAL");
        }
        FrameImp::Ptr frame;
        if (codec == CodecH265) {
            frame = FrameImp::create<H265Frame>();
        } else {
            frame = FrameImp::create<H264Frame>();
        }
        frame->_buffer.assign(nal, length);
        frame->_prefix_size = offset;
        frame->_pts = frame->_dts = stamp;
        consume(frame);
    });
}

Track::Ptr videoTrack(CodecId codec) {
    if (codec == CodecH265) {
        return std::make_shared<H265Track>();
    }
    return std::make_shared<H264Track>();
}

} // namespace

Demuxer::Demuxer(FrameConsumer output)
    : _output(std::move(output))
    , _convert_video(setting("videoH265Transcode", "videoH265Transcode", "1") == "1")
    , _convert_audio(setting("audioTranscode", "audioTranscode", "1") == "1")
    , _max_height(
          setting("reduceResolution", "reduceResolution", "1") == "1"
              ? std::stoi(setting("transcodeHeight", "TranscodeHeigh", "720"))
              : 0) {
    if (_max_height < 0 || _max_height == 1) {
        throw std::invalid_argument("invalid relay transcode height");
    }
}

void Demuxer::videoConfig(const std::string &data, uint32_t stamp) {
    if (data.size() < 12) {
        throw std::runtime_error("short relay video configuration");
    }
    if (data == _video_config) {
        return;
    }
    if (_video) {
        throw std::runtime_error("relay video configuration changed");
    }
    if (data.compare(0, 4, "cveh") == 0) {
        _input_video = CodecH265;
    } else if (data.compare(0, 4, " 462") == 0) {
        _input_video = CodecH264;
    } else {
        throw std::runtime_error("unsupported relay video codec");
    }

    auto input = std::static_pointer_cast<VideoTrack>(videoTrack(_input_video));
    auto config = [&](const FrameConsumer &consumer) {
        videoFrames(_input_video, data.data() + 8, data.size() - 8, stamp, consumer);
    };
    config([&](const Frame::Ptr &f) { input->inputFrame(f); });
    if (!input->ready()) {
        throw std::runtime_error("incomplete relay video parameters");
    }
    int width = input->getVideoWidth(), height = input->getVideoHeight();
    bool resize = _max_height > 0 && height > _max_height;
    auto codec = _input_video == CodecH265 && _convert_video ? CodecH264 : _input_video;
    _video = videoTrack(codec);
    _video_config = data;
    if (resize || codec != _input_video) {
        if (resize) {
            int target = _max_height & ~1;
            width = std::max(2, static_cast<int>(static_cast<int64_t>(width) * target / height) & ~1);
            height = target;
        }
        if (width <= 0 || height <= 0) {
            throw std::runtime_error("unknown relay video dimensions");
        }
        _video->setBitRate(std::max(512000, input->getBitRate()));
        _video_input = makeFrameTranscoder(input, _video, width, height, _output);
    } else {
        _video_input = _output;
    }
    config(_video_input);
}

void Demuxer::audioConfig(const std::string &data) {
    if (data.size() < 8) {
        throw std::runtime_error("short relay audio configuration");
    }
    if (data == _audio_config) {
        return;
    }
    if (_audio) {
        throw std::runtime_error("relay audio configuration changed");
    }
    _audio_config = data;
    _audio_input = _output;
    if (data.compare(4, 4, " caa") == 0) {
        _input_audio = CodecAAC;
        _audio = data.size() >= 10 ? std::make_shared<AACTrack>(data.substr(8)) : std::make_shared<AACTrack>();
    } else if (data.compare(4, 4, "a117") == 0) {
        _input_audio = CodecG711A;
        int rate = 8000, channels = 1;
        if (data.size() >= 10) {
            const uint8_t first = data[8], second = data[9];
            static const int rates[]
                = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350, 0, 0, 0 };
            int parsed = rates[((first & 7) << 1) | (second >> 7)];
            if (parsed) {
                rate = parsed;
            }
            channels = std::max(1, (second >> 3) & 15);
        }
        _audio = std::make_shared<G711Track>(CodecG711A, rate, channels);
        if (_convert_audio) {
            auto input = _audio;
            _audio = std::make_shared<AACTrack>(8000, 1);
            _audio->setBitRate(24000);
            _audio_input = makeFrameTranscoder(input, _audio, 0, 0, _output);
        }
    } else {
        throw std::runtime_error("unsupported relay audio codec");
    }
}

void Demuxer::input(const MediaPackage &packet) {
    const auto &data = packet.data();
    if (packet.data_size() != data.size()) {
        throw std::runtime_error("relay media size mismatch");
    }
    switch (packet.package_type()) {
        case 4: videoConfig(data, packet.start_time()); break;
        case 3: audioConfig(data); break;
        case 2:
            if (_video_input) {
                videoFrames(_input_video, data.data(), data.size(), packet.start_time(), _video_input);
            }
            break;
        case 1: {
            if (!_audio_input || data.empty()) {
                break;
            }
            auto frame = FrameImp::create<>();
            frame->_buffer.assign(data.data(), data.size());
            frame->_pts = frame->_dts = packet.start_time();
            frame->_codec_id = _input_audio;
            if (_input_audio == CodecAAC) {
                if (data.size() < 7 || uint8_t(data[0]) != 0xff || (uint8_t(data[1]) & 0xf0) != 0xf0) {
                    throw std::runtime_error("invalid relay AAC header");
                }
                // The live relay AAC packet timestamp labels the following access unit.
                frame->_pts = frame->_dts = _has_aac_stamp ? _last_aac_stamp : packet.start_time();
                _has_aac_stamp = true;
                _last_aac_stamp = packet.start_time();
                frame->_prefix_size = (data[1] & 1) ? 7 : 9;
                if (frame->_prefix_size >= data.size()) {
                    throw std::runtime_error("short AAC frame");
                }
            }
            _audio_input(frame);
            break;
        }
        default: break;
    }
}

std::vector<Track::Ptr> Demuxer::tracks() const {
    std::vector<Track::Ptr> result;
    if (_video) {
        result.emplace_back(_video);
    }
    if (_audio) {
        result.emplace_back(_audio);
    }
    return result;
}

} // namespace relay
} // namespace mediakit
