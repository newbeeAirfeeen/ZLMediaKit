/*
* @file_name: Frame.cpp
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
#include "Frame.hpp"
#include <string.h>

Rawpgm::Rawpgm():frame(nullptr){
  frame = av_frame_alloc();
}
Rawpgm::~Rawpgm(){
  if(frame)
    av_frame_free(&frame);
}

bool Rawpgm::key_frame() const{
  return frame->key_frame;
}

AVFrame* Rawpgm::raw_frame(){
  return frame;
}


namespace FFmpeg{
Frame::Frame():packet(nullptr){
    packet = av_packet_alloc();
    memset(packet, 0, sizeof(*packet));
}

Frame::~Frame(){
    if(packet) av_packet_free(&packet);
}

bool Frame::key_frame() const{
    return (bool)((AV_PKT_FLAG_KEY)|(packet->flags));
}

int64_t Frame::dts() const{
    return packet->dts;
}

int64_t Frame::pts() const{
    return packet->pts;
}
const uint8_t * Frame::data() const{
    return packet->data;
}
size_t Frame::size() const{
    return (size_t)packet->size;
}

int Frame::time_base() const{
    return packet->time_base.den;
}

AVPacket* Frame::raw_packet(){
    return packet;
}
}