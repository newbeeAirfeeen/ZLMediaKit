#pragma once

#include "Extension/Track.h"

namespace mediakit {

// Converts one input track into frames for a muxer, independent of its transport.
// The callback runs synchronously on the caller's poller.
using FrameConsumer = std::function<void(const Frame::Ptr &)>;
FrameConsumer
makeFrameTranscoder(const Track::Ptr &input, const Track::Ptr &output, int width, int height, FrameConsumer sink);

} // namespace mediakit
