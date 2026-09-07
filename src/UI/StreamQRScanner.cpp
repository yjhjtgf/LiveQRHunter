#include "StreamQRScanner.h"

#include <string>
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
    m_flushing.store(true);
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

    // 降低检测面积以提速: 二维码在画面中通常占比大, 缩到上限内几乎不影响检出。
    // 策略: 长边不超过 kMaxDetectLongSide, 同时短边不低于 kMinDetectShortSide
    // (避免竖屏/4:3 源被单一高度上限压得过小, 导致二维码过小难以检出)。
    const int srcW = pAVCodecContext->width;
    const int srcH = pAVCodecContext->height;
    const double longSide = (std::max)(srcW, srcH);
    const double shortSide = (std::min)(srcW, srcH);

    double scale = 1.0;
    if (longSide > kMaxDetectLongSide)
    {
        scale = (std::min)(scale, kMaxDetectLongSide / longSide);
    }
    if (shortSide * scale < kMinDetectShortSide && shortSide >= kMinDetectShortSide)
    {
        scale = (std::max)(scale, kMinDetectShortSide / shortSide);
    }
    int outW = static_cast<int>(srcW * scale);
    int outH = static_cast<int>(srcH * scale);
    // 保持偶数(部分编码/缩放要求)
    outW -= outW % 2;
    outH -= outH % 2;
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
                // 单帧解码错误在直播流中常见(损坏包等), 连续多帧失败才判定中断
                if (++m_consecutiveDecodeErrors >= 10)
                {
                    Q_EMIT statusChanged(QString::fromUtf8("直播中断(连续解码错误)"));
                    break;
                }
                av_frame_unref(pAVFrame);
                continue;
            }
            m_consecutiveDecodeErrors = 0;
            frameCount++;
            // 送检: 2 个检测线程都在忙时, 先取回已完成结果腾出线程再试一次,
            // 尽力保证"新二维码出现的那一帧"不被系统性丢弃。
            auto submitForDecode = [&]() -> bool {
                if (m_inFlightDecodes.load() >= threadNumber)
                {
                    return false;
                }
                m_inFlightDecodes.fetch_add(1);
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
                        // 单帧解码异常: 按"该帧无码"处理, 保持消失判定状态机推进
                        str.clear();
                    }
                    {
                        std::lock_guard<std::mutex> lock(m_decodedResultsMutex);
                        m_decodedResults.push_back({ std::move(str) });
                    }
                    m_inFlightDecodes.fetch_sub(1);
                });
                return true;
            };

            bool submitted = submitForDecode();
            if (!submitted)
            {
                // 线程全忙: 先消费已完成结果, 腾出线程再试当前帧
                processDecodedResults();
                submitted = submitForDecode();
            }
            if (!submitted)
            {
                // 极端情况仍忙, 只能丢这一帧(有界, 不堆积)
                ++m_droppedFrames;
            }
            processDecodedResults();

            if (frameCount % 30 == 0)
            {
                Q_EMIT statusChanged(QString::fromUtf8("已连接 | 帧数: %1 | 二维码: %2 | 丢帧: %3")
                                         .arg(frameCount)
                                         .arg(m_qrCount)
                                         .arg(m_droppedFrames));
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
    // 停止后只清空队列, 不再把最后一批结果上报 UI(避免停止后仍弹新码)
    if (m_flushing.load())
    {
        return;
    }
    for (auto& result : pending)
    {
        onDecoded(std::move(result.content));
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

void StreamQRScanner::onDecoded(std::string content)
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
    // 直接整串比较去重(不再用哈希指纹, 彻底消除碰撞漏码)
    if (content != m_lastQRTicket)
    {
        m_lastQRTicket = std::move(content);
        m_hasActiveQR = true;
        m_qrCount++;
        Q_EMIT qrCodeDetected(buildInfo(m_lastQRTicket, m_streamPlatform, m_roomID));
    }
}

void StreamQRScanner::stop()
{
    // 置 flushing: 阻止退出路径把排队结果再上报 UI
    m_flushing.store(true);
    m_stop.store(false);
}

void StreamQRScanner::run()
{
    threadPool.setMaxThreadCount(threadNumber);
    m_stop.store(true);
    m_flushing.store(false);
    m_inFlightDecodes.store(0);
    m_qrCount = 0;
    m_droppedFrames = 0;
    m_consecutiveDecodeErrors = 0;
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
