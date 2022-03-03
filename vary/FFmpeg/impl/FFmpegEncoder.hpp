/*
* @file_name: FFmpegEncoder.hpp
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
#ifndef MEDIAKIT_FFMPEG_ENCODER_IMPL_HPP
#define MEDIAKIT_FFMPEG_ENCODER_IMPL_HPP
#include "FFmpeg/FFmpegEncoder.hpp"
#include "FFmpeg/CoderException.hpp"

template<typename T>
FFmpegEncoder<T>::FFmpegEncoder(const AVCodecID& id):codec_id(id), encoder(nullptr), context(nullptr) {
  //分配好内存并初始化
  on_frame = [](const CoderContext&, FFmpeg::Frame::Ptr) {};
  auto codec_type = avcodec_get_type(id);

  if(is_video_encoder<T>::value && codec_type != AVMEDIA_TYPE_VIDEO)
    throw std::logic_error("FFmpegEncoder error: not is video encoder");

  if(is_audio_encoder<T>::value && codec_type != AVMEDIA_TYPE_AUDIO)
    throw std::logic_error("FFmpegEncoder error: not is video encoder");

  //初始化编码器
  initialize();
}

template<typename T>
FFmpegEncoder<T>::FFmpegEncoder(FFmpegEncoder<T>&& other){
  codec_id = other.codec_id;
  encoder = other.encoder;
  context = other.context;
  on_frame = std::move(other.on_frame);
  other.codec_id = AV_CODEC_ID_NONE;
  other.encoder = nullptr;
  other.context = nullptr;
}

template<typename T> FFmpegEncoder<T>::~FFmpegEncoder(){
  reset();
}

template<typename T>
void FFmpegEncoder<T>::initialize(){

  this->encoder = avcodec_find_encoder(codec_id);
  if(encoder == nullptr)
    throw FFmpegCoderNotFound(codec_id, "couldn't find encoder");
  context = avcodec_alloc_context3(this->encoder);
  if(context == nullptr)
    throw std::logic_error("couldn't alloc codec context");

  //上下文
  T::setContext(context);
  CoderStatus::initial();
}

template<typename T>
void FFmpegEncoder<T>::open_encoder(){
  if(CoderStatus::is_open())
    return;
    //未初始化完成，先初始化
  auto ret = avcodec_open2(context, encoder, nullptr);
  if( ret < 0)throw std::logic_error("couldn't open encoder");
  CoderStatus::open();
}

template<typename T>
void FFmpegEncoder<T>::setWorkThreads(unsigned int thread_counts){
  this->context->thread_count = (int)thread_counts;
}

template<typename T>
void FFmpegEncoder<T>::readFrame(Rawpgm::Ptr rawpgm){
  //如果未打开编码器，先打开编码器
  this->open_encoder();
  //放入一帧
  T::inputFrame(rawpgm, [this](FFmpeg::Frame::Ptr frame_ptr){
    on_frame(*this, frame_ptr);
  });
}

template<typename T>
void FFmpegEncoder<T>::setOnFrame(std::function<void(const CoderContext&, FFmpeg::Frame::Ptr)> on_frame){
  if(on_frame)this->on_frame = std::move(on_frame);
}

template<typename T>
uint64_t FFmpegEncoder<T>::Width() const{
  return context->width;
}

template<typename T>
uint64_t FFmpegEncoder<T>::Height() const{
  return context->height;
}

template<typename T>
const AVPixelFormat& FFmpegEncoder<T>::PixFormat() const{
  return context->pix_fmt;
}

template<typename T>
const AVRational& FFmpegEncoder<T>::TimeBase() const{
  return context->time_base;
}

template<typename T>
const AVRational& FFmpegEncoder<T>::SampleAspectRatio() const{
  return context->sample_aspect_ratio;
}


template<typename T>
void FFmpegEncoder<T>::reset(){
  codec_id = AV_CODEC_ID_NONE;
  if(encoder){
    encoder = nullptr;
  }
  if(context){
    avcodec_free_context(&context);
    context = nullptr;
  }
}
#endif