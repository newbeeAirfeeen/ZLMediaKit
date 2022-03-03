/*
* @file_name: FFmpegDecoder.hpp
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
#ifndef MEDIAKIT_FFMPEGDECODER_HPP
#define MEDIAKIT_FFMPEGDECODER_HPP
#include "CoderStatus.hpp"
#include "CoderContext.hpp"
#include "CoderException.hpp"
#include "Frame.hpp"
#include <functional>
#include <string>
extern "C"{
#include <libavcodec/avcodec.h>
};

class FFmpegDecoder : public CoderStatus , public CoderContext{
public:
  FFmpegDecoder(const AVCodecID&);
  ~FFmpegDecoder();
  FFmpegDecoder(FFmpegDecoder &&);
  FFmpegDecoder(const FFmpegDecoder &) = delete;
  FFmpegDecoder & operator = (const FFmpegDecoder &) = delete;
  void setOnDecoder(std::function<void(const CoderContext&, typename Rawpgm::Ptr)>&&);
  void inputFrame(const void* data, size_t length);
public:
  //设置编码器工作的线程数
  void setWorkThreads(unsigned int);
  virtual const AVCodecID& Id()const override{
      return codec_id;
  }
  //是否是音频解码器
  virtual bool AudioCoder() const override;
  //是否是视频解码器
  virtual bool VideoCoder() const override;
  //视频的宽
  virtual uint64_t Width() const override;
  //视频的高
  virtual uint64_t Height() const override;
  //编码类型
  virtual const AVPixelFormat& PixFormat() const override;
  //时间基
  virtual const AVRational& TimeBase() const override;

  virtual const AVRational& SampleAspectRatio() const override;

private:
  void reset();
  void findOpenDecoder();
  void decodeFrameInternal(FFmpeg::Frame::Ptr);
private:
  AVCodecID codec_id;
  const AVCodec* decoder;
  AVCodecContext* context;
  AVCodecParserContext* parse_context;
  //缓存一帧
  Rawpgm::Ptr rawpgm_ptr;
  std::function<void(const CoderContext&, typename Rawpgm::Ptr)> on_decoder;
};


#endif // MEDIAKIT_FFMPEGDECODER_HPP
