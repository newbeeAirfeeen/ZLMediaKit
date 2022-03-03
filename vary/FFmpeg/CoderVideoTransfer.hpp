/*
* @file_name: CoderVideoTransfer.hpp
* @date: 2022/02/09
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
#ifndef MEDIAKIT_CODERVIDEOTRANSFER_HPP
#define MEDIAKIT_CODERVIDEOTRANSFER_HPP
#include "CoderTransfer.hpp"
template<> class CoderTransfer<Video> : protected Video, public BasicTransfer{
public:
  CoderTransfer(): BasicTransfer("buffer", "buffersink"){
    source_height = source_width = 0;
    source_pix_format = AV_PIX_FMT_NONE;
    source_sample_aspect_ratio = {0, 0};
    source_time_base = {0, 0};
  }

  void CreateTransfer(uint32_t width, uint32_t height,
                      const AVPixelFormat& pix_format,
                      AVRational time_base,
                      AVRational sample_aspect_ratio){
    std::call_once(flag, [&](){

      this->source_width = width;
      this->source_height = height;
      this->source_pix_format = pix_format;
      this->source_time_base = time_base;
      this->source_sample_aspect_ratio = sample_aspect_ratio;

      char args[512] = {0};
      snprintf(args, sizeof(args),
               "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
               width, height, pix_format,
               time_base.num, time_base.den,
               sample_aspect_ratio.num,
               sample_aspect_ratio.den);
      //这里需要原来视频或者音频的参数
      int ret = avfilter_graph_create_filter(&buffer_src_context, buffer_src, "in",
                                             args, nullptr,filter_graph);
      if(ret < 0){
        throw std::runtime_error("avfilter_graph_create_filter: create buffer_src_context error");
      }
      ret = avfilter_graph_create_filter(&buffer_sink_context, buffer_sink, "out",
                                         nullptr, nullptr,filter_graph);
      if( ret < 0){
        throw std::runtime_error("avfilter_graph_create_filter: create buffer_sink_context error");
      }
      //初始化滤镜
      BasicTransfer::CreateTransfer();
    });
  }

  void inputFrame(Rawpgm::Ptr rawpgm, std::function<void(FFmpeg::Frame::Ptr)> on_frame_func){
    BasicTransfer::inputFrame(context, rawpgm, std::ref(on_frame_func));
  }

  void setTimeBase(const AVRational& rational){
    context->time_base = rational;
    format_filter_string("settb=%d/%d", "timebase", rational.num, rational.den);
  }

  void PixFmt(const AVPixelFormat& pixel_format){
    context->pix_fmt = pixel_format;
    const char* name = av_pix_fmt_desc_get(pixel_format)->name;
    format_filter_string("format=pix_fmts=%s", "format", name);
  }

  void Scale(uint64_t width, uint64_t height){
    context->width = (int)width;
    context->height = (int)height;
    format_filter_string("scale=w=%d:h=%d", "scale", (int)width, (int)height);
  }

  void Vflip(){
    desciptor["vflip"] = "vflip";
  }

  void Hflip(){
    desciptor["hflip"] = "hflip";
  }

  void BitRate(uint64_t bit_rate){
    context->bit_rate = (int64_t)bit_rate;
  }

  void DrawBox(uint64_t x, uint64_t y, uint64_t width, uint64_t height){
    format_filter_string("drawbox=x=%d:y=%d:w=%d:h=%d:color=red", "drawbox",(int)x, (int)y, (int)width,(int)height);
  }

private:
  int source_width;
  int source_height;
  AVPixelFormat source_pix_format;
  AVRational source_time_base;
  AVRational source_sample_aspect_ratio;
};
#endif // MEDIAKIT_CODERVIDEOTRANSFER_HPP
