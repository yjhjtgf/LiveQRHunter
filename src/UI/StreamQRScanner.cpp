#include "StreamQRScanner.h"

#include <string>
#include <cstdio>
#include <algorithm>

#include <QDateTime>

#include "QRScanner.h"

StreamQRScanner::StreamQRScanner(QObject* parent) :
    QThread(parent),
    m_stop(false)
{
    av_log_set_level(AV_LOG_FATAL);
}

StreamQRScanner::~StreamQRScanner()
{
    if (!this->isInterruptionRequested())
    {
        m_stop.store(false);
    }
    this->requestInterruption();
    this->wait();
}

void StreamQRScanner::setUrl(const std::string& url, const std::map<std::string, std::string> heard)
{
    streamUrl = url;
    if (pAvdictionary)
    {
        av_dict_free(&pAvdictionary);
    }
    for (const auto& it : heard)
    {
        av_dict_set(&pAvdictionary, it.first.c_str(), it.second.c_str(), 0);
    }
    // 低延迟直播: 减小缓冲, 尽快吐帧
    av_dict_set(&pAvdictionary, "fflags", "nobuffer", 0);
    av_dict_set(&pAvdictionary, "max_delay", "0", 0);
    av_dict_set(&pAvdictionary, "probesize", "1024", 0);
    av_dict_set(&pAvdictionary, "packetsize", "128", 0);
    av_dict_set(&pAvdictionary, "rtbufsize", "0", 0);
    av_dict_set(&pAvdictionary, "delay", "0", 0);
    av_dict_set(&pAvdictionary, "buffer_size", "1000", 0);
    av_dict_set(&pAvdictionary, "analyzeduration", "0", 0);
    av_dict_set(&pAvdictionary, "flags2", "+fast", 0);
}

void StreamQRScanner::setStreamContext(const std::string& platform, const std::string& roomID)
{
    m_streamPlatform = platform;
    m_roomID = roomID;
}

auto StreamQRScanner::init() -> bool
{
    pAVFormatContext = avformat_alloc_context();
    if (avformat_open_input(&pAVFormatContext, streamUrl.c_str(), NULL, &pAvdictionary) != 0)
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败: 无法打开直播流"));
        cleanup();
        return false;
    }
    if (avformat_find_stream_info(pAVFormatContext, NULL) < 0)
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败: 无法获取流信息"));
        cleanup();
        return false;
    }
    AVStream* videoStream = nullptr;
    for (unsigned i = 0; i < pAVFormatContext->nb_streams; i++)
    {
        if (pAVFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = pAVFormatContext->streams[i];
            break;
        }
    }
    if (!videoStream)
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败: 无视频流"));
        cleanup();
        return false;
    }
    videoStreamIndex = videoStream->index;
    const AVCodec* decoder = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!decoder)
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败: 找不到解码器"));
        cleanup();
        return false;
    }
    pAVCodecContext = avcodec_alloc_context3(decoder);
    if (!pAVCodecContext)
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败: 无法分配解码上下文"));
        cleanup();
        return false;
    }
    if (avcodec_parameters_to_context(pAVCodecContext, videoStream->codecpar) < 0)
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败: 无法拷贝流参数"));
        cleanup();
        return false;
    }
    if (pAVCodecContext->codec && (pAVCodecContext->codec->capabilities & AV_CODEC_CAP_DELAY))
    {
        pAVCodecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    }
    pAVCodecContext->thread_count = 1; // 低延迟: 减少解码器内部缓冲
    if (avcodec_open2(pAVCodecContext, decoder, NULL) < 0)
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败: 无法打开解码器"));
        cleanup();
        return false;
    }
    setStreamHW();
    if (videoStreamWidth <= 0 || videoStreamHeight <= 0)
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败: 无效的视频尺寸"));
        cleanup();
        return false;
    }
    pSwsContext = sws_getContext(
        videoStreamSrcWidth, videoStreamSrcHeight,
        pAVCodecContext->pix_fmt != AV_PIX_FMT_NONE ? pAVCodecContext->pix_fmt : AV_PIX_FMT_YUV420P,
        videoStreamWidth, videoStreamHeight, AV_PIX_FMT_BGR24, SWS_BILINEAR | SWS_ACCURATE_RND,
        NULL, NULL, NULL);
    if (!pSwsContext)
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败: 无法创建像素格式转换上下文"));
        cleanup();
        return false;
    }
    pAVPacket = av_packet_alloc();
    if (!pAVPacket)
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败: 无法分配数据包"));
        cleanup();
        return false;
    }
    pAVFrame = av_frame_alloc();
    if (!pAVFrame)
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败: 无法分配帧缓冲"));
        cleanup();
        return false;
    }
    return true;
}

void StreamQRScanner::setStreamHW()
{
    videoStreamSrcWidth = pAVCodecContext->width;
    videoStreamSrcHeight = pAVCodecContext->height;

    // 降低检测面积以提速: 二维码在画面中通常占比大, 缩到上限内几乎不影响检出
    int outW = pAVCodecContext->width;
    int outH = pAVCodecContext->height;
    if (outW > kMaxDetectWidth || outH > kMaxDetectHeight)
    {
        const double scale = (std::min)((double)kMaxDetectWidth / outW, (double)kMaxDetectHeight / outH);
        outW = static_cast<int>(outW * scale);
        outH = static_cast<int>(outH * scale);
    }
    if (outW % 2 != 0) outW--;
    if (outH % 2 != 0) outH--;
    if (outW < 2) outW = 2;
    if (outH < 2) outH = 2;
    videoStreamWidth = outW;
    videoStreamHeight = outH;
}

static QRCodeInfo buildInfo(const std::string& content,
                            const std::string& platform,
                            const std::string& roomID)
{
    QRCodeInfo info;
    info.rawContent = content;
    info.platform = platform;
    info.roomID = roomID;
    info.timestamp = QDateTime::currentMSecsSinceEpoch();
    return info;
}

// 内容指纹: 整个 URL 的定长摘要。避免用"前 N 字符"导致登录码前缀相同被误判为相同。
static std::string makeQRFingerprint(const std::string& content)
{
    if (content.empty())
    {
        return {};
    }
    std::size_t h = 14695981039346656037ull; // FNV-1a
    for (unsigned char ch : content)
    {
        h ^= ch;
        h *= 1099511628211ull;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016zx", h);
    return std::string(buf, 16);
}

void StreamQRScanner::processStream()
{
    int frameCount = 0;
    while (m_stop.load())
    {
        if (av_read_frame(pAVFormatContext, pAVPacket) < 0)
        {
            Q_EMIT statusChanged(QString::fromUtf8("直播中断"));
            break;
        }
        if (pAVPacket->stream_index != videoStreamIndex)
        {
            av_packet_unref(pAVPacket);
            continue;
        }
        const int sendRet = avcodec_send_packet(pAVCodecContext, pAVPacket);
        av_packet_unref(pAVPacket);
        if (sendRet < 0 && sendRet != AVERROR(EAGAIN))
        {
            Q_EMIT statusChanged(QString::fromUtf8("直播中断(解码错误)"));
            break;
        }
        while (true)
        {
            const int recvRet = avcodec_receive_frame(pAVCodecContext, pAVFrame);
            if (recvRet == AVERROR(EAGAIN) || recvRet == AVERROR_EOF)
            {
                break;
            }
            if (recvRet < 0)
            {
                Q_EMIT statusChanged(QString::fromUtf8("直播中断(解码错误)"));
                break;
            }
            frameCount++;
            // 检测线程全忙时直接丢帧: 直播场景丢帧远好于排队增加延迟
            const bool poolBusy = threadPool.activeThreadCount() >= threadNumber;
            if (!poolBusy)
            {
                cv::Mat img(videoStreamHeight, videoStreamWidth, CV_8UC3);
                uint8_t* dstData[1] = { img.data };
                const int dstLinesize[1] = { static_cast<int>(img.step) };
                sws_scale(pSwsContext, pAVFrame->data, pAVFrame->linesize, 0, pAVFrame->height,
                          dstData, dstLinesize);

                threadPool.start([img = std::move(img), this]() mutable {
                    thread_local QRScanner qrScanners;
                    std::string str;
                    try
                    {
                        qrScanners.decodeSingle(img, str);
                    }
                    catch (...)
                    {
                        return;
                    }
                    const std::string fingerprint = makeQRFingerprint(str);
                    std::lock_guard<std::mutex> lock(m_decodedResultsMutex);
                    m_decodedResults.push_back({ std::move(str), fingerprint });
                });
            }
            processDecodedResults();

            if (frameCount % 30 == 0)
            {
                Q_EMIT statusChanged(QString::fromUtf8("已连接 | 帧数: %1 | 二维码: %2")
                                         .arg(frameCount)
                                         .arg(m_qrCount));
            }
            av_frame_unref(pAVFrame);
        }
    }
    threadPool.waitForDone();
    processDecodedResults();
}

void StreamQRScanner::processDecodedResults()
{
    std::deque<DecodeResult> pending;
    {
        std::lock_guard<std::mutex> lock(m_decodedResultsMutex);
        pending.swap(m_decodedResults);
    }
    for (auto& result : pending)
    {
        onDecoded(std::move(result.content), std::move(result.fingerprint));
    }
}

void StreamQRScanner::cleanup()
{
    avformat_close_input(&pAVFormatContext);
    avcodec_free_context(&pAVCodecContext);
    sws_freeContext(pSwsContext);
    av_dict_free(&pAvdictionary);
    av_frame_free(&pAVFrame);
    av_packet_free(&pAVPacket);
    pAVFormatContext = nullptr;
    pAVCodecContext = nullptr;
    pSwsContext = nullptr;
    pAvdictionary = nullptr;
    pAVFrame = nullptr;
    pAVPacket = nullptr;
}

void StreamQRScanner::onDecoded(std::string content, std::string fingerprint)
{
    if (content.empty())
    {
        // 单帧没检测到不立刻判定消失(检测抖动/瞬时遮挡很常见)。
        // 连续多帧都为空, 才认为二维码真的从画面消失。
        m_consecutiveEmptyFrames++;
        if (m_hasActiveQR && m_consecutiveEmptyFrames >= kEmptyResultFrameLimit)
        {
            m_hasActiveQR = false;
            m_lastQRTicket.clear();
            QRCodeInfo empty;
            empty.platform = m_streamPlatform;
            empty.roomID = m_roomID;
            empty.timestamp = QDateTime::currentMSecsSinceEpoch();
            Q_EMIT qrCodeDetected(empty);
        }
        return;
    }
    // 出现内容: 重置空帧计数
    m_consecutiveEmptyFrames = 0;
    if (fingerprint != m_lastQRTicket)
    {
        m_lastQRTicket = std::move(fingerprint);
        m_hasActiveQR = true;
        m_qrCount++;
        Q_EMIT qrCodeDetected(buildInfo(std::move(content), m_streamPlatform, m_roomID));
    }
}

void StreamQRScanner::stop()
{
    m_stop.store(false);
}

void StreamQRScanner::run()
{
    threadPool.setMaxThreadCount(threadNumber);
    m_stop.store(true);
    m_qrCount = 0;
    m_hasActiveQR = false;
    m_lastQRTicket.clear();
    m_consecutiveEmptyFrames = 0;

    Q_EMIT statusChanged(QString::fromUtf8("连接中..."));
    if (init())
    {
        Q_EMIT statusChanged(QString::fromUtf8("已连接"));
        processStream();
    }
    else
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败"));
    }
    cleanup();
}
