/*
* @file_name: endian.hpp
* @date: 2021/09/12
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
#ifndef OAHO_ENDIAN_HPP
#define OAHO_ENDIAN_HPP

#include <iostream>
#include <cstdlib>
#include <cstring>

template<typename T>
static void reverse(T& val){
    auto typesize = sizeof(T);
    char* dst = reinterpret_cast<char*>(std::addressof(val));
    int loopc = (int)typesize / 2;
    for (int i = 0; i < loopc; i++)
        std::swap(dst[i], dst[typesize - i - 1]);
}

class endian {
public:
    static inline uint32_t load_be32(const void* p) {
      const uint8_t* data = (const uint8_t*)p;
      return data[0] | ((uint32_t)data[1] << 8) |
             ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    }
    static inline uint16_t load_be16(const void* p) {
        const uint8_t * data = (const uint8_t*)p;
        return ((uint16_t)data[0] >> 8) | ((uint16_t)data[1]);
    }
    static inline uint32_t load_le32(const void* p) {
        const uint8_t* data = (const uint8_t*)p;
        return data[0] | ((uint32_t)data[1] << 8) |
            ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    }
    static inline uint32_t load_be24(const void* p) {
        const uint8_t* data = (const uint8_t*)p;
        return data[2] | ((uint32_t)data[1] << 8) | ((uint32_t)data[0] << 16);
    }
    static inline void set_be16(void* p) {
        uint8_t* data = (uint8_t*)p;
        std::swap(data[0], data[1]);
    }
    static inline void set_be24(void* p, uint32_t val) {
        uint8_t* data = (uint8_t*)p;
        data[0] = val >> 16;
        data[1] = val >> 8;
        data[2] = val;
    }
    static inline void set_le32(void* p, uint32_t val) {
        uint8_t* data = (uint8_t*)p;
        data[0] = val;
        data[1] = val >> 8;
        data[2] = val >> 16;
        data[3] = val >> 24;
    }
    static inline void set_be32(void* p, uint32_t val) {
        uint8_t* data = (uint8_t*)p;
        data[3] = val;
        data[2] = val >> 8;
        data[1] = val >> 16;
        data[0] = val >> 24;
    }

    template<typename R = uint64_t>
    auto load_be64(void* p) -> typename std::decay<R>::type
    {
        typename std::decay<R>::type val;
        val = *(typename std::decay<R>::type*)&p;
        reverse(*val);
        return val;
    }

    static void set_be64(void* ptr)
    {
        uint64_t* val = (uint64_t*)ptr;
        reverse(*val);
    }
};



#endif