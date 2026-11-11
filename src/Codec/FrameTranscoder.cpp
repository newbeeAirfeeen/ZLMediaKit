#include "FrameTranscoder.h"
#include "Transcode.h"

namespace mediakit {
FrameConsumer
makeFrameTranscoder(const Track::Ptr &input, const Track::Ptr &output, int width, int height, FrameConsumer sink) {
#ifdef ENABLE_FFMPEG
    auto encoder = std::make_shared<FFmpegEncoder>(output, 2, width, height);
    encoder->setOnEncode(std::move(sink));
    auto decoder = std::make_shared<FFmpegDecoder>(input);
    FFmpegSws::Ptr scaler;
    if (input->getTrackType() == TrackVideo) {
        scaler = std::make_shared<FFmpegSws>(AV_PIX_FMT_YUV420P, width, height);
    }
    decoder->setOnDecode([encoder, scaler](const FFmpegFrame::Ptr &frame) {
        auto scaled = scaler ? scaler->inputFrame(frame) : frame;
        if (scaled) {
            encoder->inputFrame(scaled, false);
        }
    });
    return [decoder](const Frame::Ptr &frame) { decoder->inputFrame(frame, true, false); };
#else
    throw std::runtime_error("transcoding requires the FFmpeg codec backend");
#endif
}
} // namespace mediakit
