/*
* @file_name: Frame.hpp
* @date: 2021/10/06
* @author: oaho
* Copyright @ hz oaho, All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
 */
#ifndef MEDIAKIT_FRAME_HPP
#define MEDIAKIT_FRAME_HPP
#include <memory>
extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
};

struct Rawpgm{
  using Ptr = std::shared_ptr<Rawpgm>;
  Rawpgm();
  ~Rawpgm();
  bool key_frame() const;
  AVFrame* raw_frame();
  AVFrame* frame;
};


namespace FFmpeg{
struct Frame{
    using Ptr = std::shared_ptr<Frame>;
    Frame();
    ~Frame();
    bool key_frame() const;
    int64_t dts() const;
    int64_t pts() const;
    const uint8_t* data() const;
    size_t  size() const;
    int     time_base() const;
    AVPacket* raw_packet();
    AVPacket* packet;
};
}

#endif // MEDIAKIT_FRAME_HPP
