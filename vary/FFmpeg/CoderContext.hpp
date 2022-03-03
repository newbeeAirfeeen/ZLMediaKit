/*
* @file_name: coder_context.hpp
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
#ifndef MEDIAKIT_CODERCONTEXT_HPP
#define MEDIAKIT_CODERCONTEXT_HPP
#include <iostream>
extern "C"{
#include <libavcodec/avcodec.h>
};
class CoderContext{
public:
  virtual const AVCodecID& Id()const = 0;
  virtual bool AudioCoder() const = 0;
  virtual bool VideoCoder() const = 0;
  virtual uint64_t Width() const = 0;
  virtual uint64_t Height() const = 0;
  virtual const AVPixelFormat& PixFormat() const = 0;
  virtual const AVRational& TimeBase() const = 0;
  virtual const AVRational& SampleAspectRatio() const = 0;
};
#endif // MEDIAKIT_CODERCONTEXT_HPP
