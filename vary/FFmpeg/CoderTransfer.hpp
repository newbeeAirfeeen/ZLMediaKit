/*
* @file_name: FFmpegVideoTransfer.hpp
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
#ifndef MEDIAKIT_FFMPEGVIDEOTRANSFER_HPP
#define MEDIAKIT_FFMPEGVIDEOTRANSFER_HPP
#include "Frame.hpp"
#include <mutex>
#include <map>
extern "C"{
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/channel_layout.h>
};
using namespace FFmpeg;
class Video{
  template<typename T> friend class CoderTransfer;
public:
  Video(){}
  void setContext(AVCodecContext* context){
    this->context = context;
  }
public:
  void AverageBitRate(uint64_t bit_rate){
    context->bit_rate = (int64_t )bit_rate;
  }
  void Scale(uint64_t width, uint64_t height){
    context->width = (int64_t)width;
    context->height = (int64_t)height;
  }

  void Fps(unsigned int fps){
    context->framerate.num = (int)fps;
    context->framerate.den = (int)1;
    context->time_base = {1, (int)fps};
  }

  void PixFmt(const AVPixelFormat& pixel_format){
    context->pix_fmt = pixel_format;
  }
  void Gop(uint64_t gop_size){
    context->gop_size = (int)gop_size;
  }
  void MaxBFrame(uint64_t bframes){
    context->max_b_frames = (int)bframes;
  }
  uint64_t AverageBitRate() const{
    return (uint64_t)(context->bit_rate);
  }
  uint64_t Width() const{
    return (uint64_t)context->width;
  }
  uint64_t Height() const{
    return (uint64_t )context->height;
  }
  const AVPixelFormat& PixFmt() const{
    return context->pix_fmt;
  }
  uint64_t Gop() const{
    return (uint64_t)context->gop_size;
  }
  uint64_t MaxBFrame() const{
    return (uint64_t)context->max_b_frames;
  }
protected:
  void readFrame(Rawpgm::Ptr raw_pgm, std::function<void(FFmpeg::Frame::Ptr)> _invoke){
    raw_pgm->raw_frame()->width = context->width;
    raw_pgm->raw_frame()->height = context->height;
    raw_pgm->raw_frame()->format = context->pix_fmt;
    int ret = avcodec_send_frame(context, raw_pgm->raw_frame());
    if( ret < 0)
      throw std::logic_error("avcodec_send_frame error");

    while(ret >= 0){
      if(!frame_ptr) {
        frame_ptr = std::make_shared<FFmpeg::Frame>();
      }
      ret = avcodec_receive_packet(context, frame_ptr->raw_packet());
      if( ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)return;
      else if( ret < 0)
        throw std::logic_error("avcodec_receive_packet error");
      _invoke(std::move(frame_ptr));
    }
  }
private:
  AVCodecContext* context;
  //临时存放数据
  FFmpeg::Frame::Ptr frame_ptr;
};


class Audio{
  template<typename T> friend class CoderTransfer;
};



class BasicTransfer{
protected:
  BasicTransfer(const char* src, const char* sink):buffer_src(nullptr),
                    buffer_sink(nullptr),
                    outputs(nullptr),
                    buffer_src_context(nullptr),
                    buffer_sink_context(nullptr){

    buffer_src = avfilter_get_by_name(src);
    buffer_sink = avfilter_get_by_name(sink);
    if(!buffer_src || !buffer_sink ){
      throw std::runtime_error("buffersrc or buffersink is empty");
    }
    outputs = avfilter_inout_alloc();
    inputs = avfilter_inout_alloc();

    filter_graph = avfilter_graph_alloc();
    pkt = std::make_shared<FFmpeg::Frame>();
  }
  ~BasicTransfer(){
    if(inputs)
      avfilter_inout_free(&inputs);
    if(outputs)
      avfilter_inout_free(&outputs);
    if(filter_graph)
      avfilter_graph_free(&filter_graph);
  }

  void CreateTransfer(){
    /*
   * Set the endpoints for the filter graph. The filter_graph will
   * be linked to the graph described by filters_descr.
   *
   * The buffer source output must be connected to the input pad of
   * the first filter described by filters_descr; since the first
   * filter input label is not specified, it is set to "in" by
   * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffer_src_context;
    outputs->pad_idx    = 0;
    outputs->next       = nullptr;
    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffer_sink_context;
    inputs->pad_idx    = 0;
    inputs->next       = nullptr;


    //构造filters
    std::string desi;
    size_t index = 0;
    for(auto iter = desciptor.begin(); iter != desciptor.end(); ++iter){
      if(iter != desciptor.begin()){
        desi += "[";
        desi += std::to_string(index - 1);
        desi += "]";
      }
      //descriptor
      desi += iter->second;
      desi += "[";
      desi += std::to_string(index);
      desi += "];";
      ++index;
    }
    //删除多余的;
    desi.erase(desi.rfind("["));
    int ret = 0;
    if ((ret = avfilter_graph_parse_ptr(filter_graph, desi.empty() ? "null" : desi.c_str(),
                                        &inputs, &outputs, nullptr)) < 0)
      throw std::runtime_error("avfilter_graph_parse_ptr error");
    if ((ret = avfilter_graph_config(filter_graph, nullptr)) < 0)
      throw std::runtime_error("avfilter_graph_config error");
  }
protected:
  template<typename...Args>
  void format_filter_string(const char* format, const char* key, Args&&...args){
    char buf[1024] = {0};
    int ret = snprintf(buf, sizeof(buf), format, args...);
    std::string destination(buf, ret);
    desciptor[key] = std::move(destination);
  }

  void inputFrame(AVCodecContext* context, Rawpgm::Ptr rawpgm, std::function<void(FFmpeg::Frame::Ptr)>& on_frame_func){
    //拿到原始数据帧
    /* push the decoded frame into the filtergraph */
    int ret = av_buffersrc_add_frame_flags(buffer_src_context, rawpgm -> raw_frame(),0);
    //error
    if( ret < 0 ){
      throw std::runtime_error("av_buffersrc_add_frame_flags error");
    }

    /* pull filtered frames from the filtergraph */
    while(true){
      auto filter_frame = std::make_shared<Rawpgm>();
      ret = av_buffersink_get_frame(buffer_sink_context, filter_frame->raw_frame());
      if( ret == AVERROR(EAGAIN) || ret == AVERROR(EOF)){
        /*
         *  if no more frames for output - returns AVERROR(EAGAIN)
         *  if flushed and no more frames for output - returns AVERROR_EOF
         *  rewrite retcode to 0 to show it as normal procedure completion
         */
        break;
      }

      //error
      if( ret < 0 ){
        throw std::runtime_error("av_buffersink_get_frame error");
      }
      //这里需要把frame重新打包成AVpacket
      while(true){
        //frame放入缓冲区中，发送frame的时候注意生命周期
        ret = avcodec_send_frame(context, filter_frame->raw_frame());
        if( ret < 0)
          throw std::logic_error("avcodec_send_frame error");
        while(ret >= 0){
          if(!pkt)
            pkt = std::make_shared<FFmpeg::Frame>();
          ret = avcodec_receive_packet(context, pkt->raw_packet());
          if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
          else if(ret < 0){
            throw std::logic_error("avcodec_receive_packet error, when transcoding");
          }
          //这里可能需要转换时间戳
          on_frame_func(std::move(pkt));
        }
      }
    }
  }
protected:
  const AVFilter* buffer_src;
  const AVFilter* buffer_sink;
  AVFilterInOut* outputs;
  AVFilterInOut* inputs;
  AVFilterGraph* filter_graph;
  AVFilterContext* buffer_src_context;
  AVFilterContext* buffer_sink_context;
  std::once_flag flag;
  std::shared_ptr<FFmpeg::Frame> pkt;
  std::map<std::string, std::string> desciptor;
};

template<typename T> class CoderTransfer;

#endif // MEDIAKIT_FFMPEGVIDEOTRANSFER_HPP
