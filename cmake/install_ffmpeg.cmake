# FFmpeg 预编译二进制(BtbN/FFmpeg-Builds)
#
# 默认使用 latest master 共享构建; 如需固定版本可覆盖 FFMPEG_DOWNLOAD_URL:
#   cmake -DFFMPEG_DOWNLOAD_URL=https://.../ffmpeg-master-latest-win64-gpl-shared.zip ...
#
# 注意: latest 会随上游漂移(可能换 major 版本)。若编译/链接出错,
# 或运行时提示找不到 avcodec-XX.dll, 请固定 URL 到一个具体 autobuild 资产。

if(NOT DEFINED FFMPEG_DOWNLOAD_URL)
    set(FFMPEG_DOWNLOAD_URL "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip")
endif()

set(FFMPEG_ARCHIVE "ffmpeg.zip")
set(FFMPEG_DIR "${CMAKE_BINARY_DIR}/ffmpeg-master-latest-win64-gpl-shared")
set(FFMPEG_ARCHIVE "${CMAKE_BINARY_DIR}/${FFMPEG_ARCHIVE}")

if(NOT EXISTS ${FFMPEG_DIR})
    message(STATUS "Downloading FFmpeg from ${FFMPEG_DOWNLOAD_URL} ...")
    file(DOWNLOAD ${FFMPEG_DOWNLOAD_URL} ${FFMPEG_ARCHIVE} SHOW_PROGRESS STATUS ffmpeg_dl_status)
    list(GET ffmpeg_dl_status 0 ffmpeg_dl_code)
    list(GET ffmpeg_dl_status 1 ffmpeg_dl_msg)
    if(NOT ffmpeg_dl_code EQUAL 0)
        message(FATAL_ERROR "FFmpeg 下载失败 (${ffmpeg_dl_msg}): ${FFMPEG_DOWNLOAD_URL}")
    endif()
    file(ARCHIVE_EXTRACT INPUT ${FFMPEG_ARCHIVE} DESTINATION "${CMAKE_BINARY_DIR}")
endif()

if(NOT EXISTS "${FFMPEG_DIR}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "FFmpeg 解压后缺少头文件, 预期目录: ${FFMPEG_DIR}")
endif()

set(FFMPEG_LIBS
    avcodec
    avdevice
    avfilter
    avformat
    avutil
    swscale
    swresample
)

set(FFMPEG_BIN_DIR ${FFMPEG_DIR}/bin)
set(FFMPEG_LIB_DIR ${FFMPEG_DIR}/lib)
set(FFMPEG_INCLUDE_DIR ${FFMPEG_DIR}/include)

link_directories(${FFMPEG_LIB_DIR})
include_directories(${FFMPEG_INCLUDE_DIR})

