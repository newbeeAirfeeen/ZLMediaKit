include_directories(${CMAKE_SOURCE_DIR}/vary)

aux_source_directory(FFmpeg ffmpeg_src)
include_directories("/Users/shenhao/git/ffmpeg_study/include")
link_directories("/Users/shenhao/git/ffmpeg_study/lib/mac_apple_silicon/debug/static")

set(LIB avfilter
        avformat
        avdevice
        avcodec
        swscale
        postproc
        avutil
        swresample
        x264
        x265
        m
        z)



add_executable(vary vary.cpp
        VaryFrameWriter.cpp
        VarySession.cpp
        RtspMuxerMediaSource.cpp
        RtspPusher.cpp
        RtspPlayer.hpp
        VaryManager.cpp
        VaryTask.cpp
                    ${ffmpeg_src})
target_link_libraries(vary ${LINK_LIB_LIST} ${LIB} jsoncpp)