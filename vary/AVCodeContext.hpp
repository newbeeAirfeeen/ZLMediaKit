//
// Created by 沈昊 on 2022/2/24.
//

#ifndef MEDIAKIT_AVCODECONTEXT_HPP
#define MEDIAKIT_AVCODECONTEXT_HPP
extern "C"{
#include <libavformat/avformat.h>
};

struct AVCodeContext{
    //原始编码ID
    AVCodecID src_id;
    //目标编码id
    AVCodecID dst_id;
    //转换的视频宽高
    size_t dst_width = 1024;
    size_t dst_height = 768;

};




#endif // MEDIAKIT_AVCODECONTEXT_HPP
