/*
* @file_name: FFmpegDecoder.cpp
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
#include "FFmpegDecoder.hpp"
#include <string>


FFmpegDecoder::FFmpegDecoder(const AVCodecID& av_code_id):codec_id(av_code_id),context(nullptr),parse_context(nullptr), decoder(nullptr){
  on_decoder = [](const CoderContext&, typename Rawpgm::Ptr){};
  findOpenDecoder();
  //申请一个缓存帧
  rawpgm_ptr = std::make_shared<Rawpgm>();
}


FFmpegDecoder::~FFmpegDecoder(){
    reset();
}

FFmpegDecoder::FFmpegDecoder(FFmpegDecoder && other){

  codec_id = other.codec_id;
  decoder = other.decoder;
  context = other.context;
  parse_context = other.parse_context;
  rawpgm_ptr = std::move(other.rawpgm_ptr);
  on_decoder = std::move(other.on_decoder);

  other.codec_id = AV_CODEC_ID_NONE;
  other.decoder = nullptr;
  other.context = nullptr;
  other.parse_context = nullptr;

}

void FFmpegDecoder::reset(){

  if(context){
    avcodec_free_context(&context);
    context = nullptr;
  }

  if(parse_context){
    av_parser_close(parse_context);
    parse_context = nullptr;
  }
  CoderStatus::close();
  CoderStatus::destroy();
}

void FFmpegDecoder::findOpenDecoder(){

  decoder = avcodec_find_decoder(codec_id);
  if( decoder == nullptr ){
    throw FFmpegCoderNotFound(codec_id, "FFmpegDecoder not found");
  }
  parse_context = av_parser_init(codec_id);
  if( parse_context == nullptr){
    throw std::logic_error("FFmpegDecoder parse context not init");
  }
  //分配解码器上下文
  context = avcodec_alloc_context3(decoder);
  if(context == nullptr){
    throw std::logic_error("could not allocate codec context");
  }
  //缺少一些解码器的信息
  //open decoder
  if(avcodec_open2(context, decoder, nullptr) < 0){
    throw std::logic_error("could not open codec");
  }
  CoderStatus::initial();
  CoderStatus::open();
}

void FFmpegDecoder::setOnDecoder(std::function<void(const CoderContext&, typename Rawpgm::Ptr)>&& func_){
  if(func_)on_decoder = std::move(func_);
}

void FFmpegDecoder::inputFrame(const void* data, size_t length){

    int ret = 0;
    //一共有多少数据
    auto data_size = length;
    if(!data_size)return;
    auto frame_ = std::make_shared<FFmpeg::Frame>();
    auto raw_packet = frame_->raw_packet();
    const uint8_t* pointer = (const uint8_t*)data;
    while(data_size > 0){
      ret = av_parser_parse2(parse_context, context,
                             &raw_packet->data,
                             &raw_packet->size,
                             pointer, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
      if( ret < 0){
        throw std::logic_error("frame parse error");
      }
      pointer += ret;
      data_size -= ret;
      if(frame_->size()) decodeFrameInternal(frame_);
    }

}

void FFmpegDecoder::setWorkThreads(unsigned int thread_counts){
  this->context->thread_count = (int)thread_counts;
}

bool FFmpegDecoder::AudioCoder() const {
  return avcodec_get_type(codec_id) == AVMEDIA_TYPE_AUDIO;
}

bool FFmpegDecoder::VideoCoder() const {
  return avcodec_get_type(codec_id) == AVMEDIA_TYPE_VIDEO;
}

uint64_t FFmpegDecoder::Width() const{
  return context->width;
}

uint64_t FFmpegDecoder::Height() const{
  return context->height;
}

const AVPixelFormat& FFmpegDecoder::PixFormat() const{
  return context->pix_fmt;
}
const AVRational& FFmpegDecoder::TimeBase() const{
  return context->time_base;
}
const AVRational& FFmpegDecoder::SampleAspectRatio() const{
  return context->sample_aspect_ratio;
}
// pkt
void FFmpegDecoder::decodeFrameInternal(FFmpeg::Frame::Ptr frame_ptr){
  auto self_packet = frame_ptr;
  int ret = avcodec_send_packet(context, self_packet->raw_packet());
  if( ret < 0 ){
    throw std::logic_error("occur the error while decoding");
  }
  while( ret >= 0){
    ret = avcodec_receive_frame(context, rawpgm_ptr->raw_frame());
    if( ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)return;
    else if ( ret < 0){
      throw std::logic_error("occur error while decoding on receive frame");
    }
    on_decoder(*this, rawpgm_ptr);
  }
}