/*
* @file_name: EncoderStatus.hpp
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
#ifndef MEDIAKIT_ENCODERSTATUS_HPP
#define MEDIAKIT_ENCODERSTATUS_HPP
class CoderStatus {
public:
  CoderStatus();
public:
  //是否初始化
  virtual bool is_initial();
  //初始化
  virtual void initial();
  //销毁
  virtual void destroy();
  //是否打开
  virtual bool is_open();
  //打开
  virtual void open();
  //关闭
  virtual void close();
private:
  bool is_initialize;
  bool encoder_is_open;
};
#endif // MEDIAKIT_ENCODERSTATUS_HPP
