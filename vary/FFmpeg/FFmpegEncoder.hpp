/*
* @file_name: CoderTransfer.hpp
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
#ifndef MEDIAKIT_FFMPEGENCODER_HPP
#define MEDIAKIT_FFMPEGENCODER_HPP
#include <functional>
#include <map>
#include <utility>
#include <type_traits>
#include "CoderStatus.hpp"
#include "CoderContext.hpp"
#include "CoderTransfer.hpp"
#include "Frame.hpp"
extern "C"{
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
};

template<typename T>
class is_video_encoder : public std::false_type{};
template<> class is_video_encoder<Video>:public std::true_type{};
template<> class is_video_encoder<CoderTransfer<Video>>:public std::true_type{};
template<typename T>
class is_audio_encoder : public std::false_type{};
template<> class is_audio_encoder<Audio>:public std::true_type{};
template<> class is_audio_encoder<CoderTransfer<Audio>>:public std::true_type{};

template<typename _EncoderType>
class FFmpegEncoder : public _EncoderType, public CoderStatus, public CoderContext{
public:
  using EncoderType = _EncoderType;
public:
  FFmpegEncoder(const AVCodecID&);
  FFmpegEncoder(FFmpegEncoder &&);
  ~FFmpegEncoder();

  FFmpegEncoder(const FFmpegEncoder &) = delete;
  FFmpegEncoder & operator = (const FFmpegEncoder &) = delete;
  //设置编码器工作的线程数
  void setWorkThreads(unsigned int);
  void readFrame(Rawpgm::Ptr);
  void setOnFrame(std::function<void(const CoderContext&, FFmpeg::Frame::Ptr)>);
public:
  virtual const AVCodecID& Id() const override{
       return codec_id;
  }
  virtual bool AudioCoder() const override{
        return is_audio_encoder<_EncoderType>::value;
  }
  virtual bool VideoCoder() const override{
        return is_video_encoder<_EncoderType>::value;
  }
  virtual uint64_t Width() const override;
  virtual uint64_t Height() const override;
  virtual const AVPixelFormat& PixFormat() const override;
  virtual const AVRational& TimeBase() const override;
  virtual const AVRational& SampleAspectRatio() const override;
private:
  void initialize();
  void open_encoder();
  void reset();
private:
  AVCodecID codec_id;
  const AVCodec* encoder;
  AVCodecContext* context;
  std::function<void(const CoderContext&, FFmpeg::Frame::Ptr)> on_frame;
};
#endif // MEDIAKIT_FFMPEGENCODER_HPP