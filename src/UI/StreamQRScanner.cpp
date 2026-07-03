#include "StreamQRScanner.h"

#include <string>

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
    for (const auto& it : heard)
    {
        av_dict_set(&pAvdictionary, it.first.c_str(), it.second.c_str(), 0);
    }
    av_dict_set(&pAvdictionary, "max_delay", "0", 0);
    av_dict_set(&pAvdictionary, "probesize", "1024", 0);
    av_dict_set(&pAvdictionary, "packetsize", "128", 0);
    av_dict_set(&pAvdictionary, "rtbufsize", "0", 0);
    av_dict_set(&pAvdictionary, "delay", "0", 0);
    av_dict_set(&pAvdictionary, "buffer_size", "1000", 0);
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
    avcodec_parameters_to_context(pAVCodecContext, videoStream->codecpar);
    if (avcodec_open2(pAVCodecContext, decoder, NULL) < 0)
    {
        Q_EMIT statusChanged(QString::fromUtf8("连接失败: 无法打开解码器"));
        cleanup();
        return false;
    }
    setStreamHW();
    pSwsContext = sws_getContext(
        pAVCodecContext->width, pAVCodecContext->height, pAVCodecContext->pix_fmt,
        videoStreamWidth, videoStreamHeight, AV_PIX_FMT_BGR24, SWS_BILINEAR, NULL, NULL, NULL);
    pAVPacket = av_packet_alloc();
    pAVFrame = av_frame_alloc();
    return true;
}

void StreamQRScanner::setStreamHW()
{
    if (pAVCodecContext->width < pAVCodecContext->height ||
        pAVCodecContext->height == 480 ||
        pAVCodecContext->height == 720)
    {
        videoStreamWidth = pAVCodecContext->width;
        videoStreamHeight = pAVCodecContext->height;
    }
    else
    {
        videoStreamWidth = pAVCodecContext->width / 1.5;
        videoStreamHeight = pAVCodecContext->height / 1.5;
    }
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
        avcodec_send_packet(pAVCodecContext, pAVPacket);
        if (!pAVFrame)
        {
            av_packet_unref(pAVPacket);
            break;
        }
        while (avcodec_receive_frame(pAVCodecContext, pAVFrame) == 0)
        {
            cv::Mat img(videoStreamHeight, videoStreamWidth, CV_8UC3);
            uint8_t* dstData[1] = { img.data };
            const int dstLinesize[1] = { static_cast<int>(img.step) };
            sws_scale(pSwsContext, pAVFrame->data, pAVFrame->linesize, 0, pAVFrame->height,
                      dstData, dstLinesize);

            threadPool.tryStart([img = std::move(img), platform = m_streamPlatform, room = m_roomID, this]() mutable {
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
                if (str.empty())
                {
                    QMetaObject::invokeMethod(this, [this]() {
                        onDecoded(QString(), QString());
                    }, Qt::QueuedConnection);
                    return;
                }
                std::string qrFingerprint = str.substr(0, (std::min)(str.size(), size_t(50)));
                // Post result back to scanner thread for safe member updates
                QMetaObject::invokeMethod(this, [this, str = std::move(str), qrFingerprint = std::move(qrFingerprint)]() mutable {
                    onDecoded(QString::fromStdString(str), QString::fromStdString(qrFingerprint));
                }, Qt::QueuedConnection);
            });

            frameCount++;
            if (frameCount % 30 == 0)
            {
                Q_EMIT statusChanged(QString::fromUtf8("已连接 | 帧数: %1 | 二维码: %2")
                                         .arg(frameCount)
                                         .arg(m_qrCount));
            }
        }
        av_frame_unref(pAVFrame);
        av_packet_unref(pAVPacket);
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

void StreamQRScanner::onDecoded(const QString& content, const QString& fingerprint)
{
    if (content.isEmpty())
    {
        if (m_hasActiveQR)
        {
            m_hasActiveQR = false;
            QRCodeInfo empty;
            empty.platform = m_streamPlatform;
            empty.roomID = m_roomID;
            empty.timestamp = QDateTime::currentMSecsSinceEpoch();
            Q_EMIT qrCodeDetected(empty);
        }
        return;
    }
    const std::string fp = fingerprint.toStdString();
    if (fp != m_lastQRTicket)
    {
        m_lastQRTicket = fp;
        m_hasActiveQR = true;
        m_qrCount++;
        Q_EMIT qrCodeDetected(buildInfo(content.toStdString(), m_streamPlatform, m_roomID));
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
