/*
* @file_name: coder_CoderStatus.hpp
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
#include "CoderStatus.hpp"
CoderStatus::CoderStatus():encoder_is_open(false), is_initialize(false){}
bool CoderStatus::is_initial(){
  return is_initialize;
}
void CoderStatus::initial(){
  is_initialize = true;
}
void CoderStatus::destroy(){
  is_initialize = false;
}

bool CoderStatus::is_open(){
  return encoder_is_open;
}
void CoderStatus::open(){
  encoder_is_open = true;
}
void CoderStatus::close(){
  encoder_is_open = false;
}

