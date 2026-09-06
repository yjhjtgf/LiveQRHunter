#pragma once

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
};

#include <QThread>
#include <QThreadPool>

#include "QRCodeInfo.hpp"

class StreamQRScanner final :
    public QThread
{
    Q_OBJECT
public:
    explicit StreamQRScanner(QObject* parent = nullptr);
    ~StreamQRScanner();
    Q_DISABLE_COPY_MOVE(StreamQRScanner)

    void setUrl(const std::string& url, const std::map<std::string, std::string> heard = {});
    void setStreamContext(const std::string& platform, const std::string& roomID);
    auto init() -> bool;
    void run();
    void stop();

Q_SIGNALS:
    void qrCodeDetected(const QRCodeInfo info);
    void statusChanged(const QString status);

private:
    struct DecodeResult
    {
        std::string content;
        std::string fingerprint;
    };

    void processStream();
    void processDecodedResults();
    void setStreamHW();
    void cleanup();

    void onDecoded(std::string content, std::string fingerprint);

    std::string streamUrl{};
    AVDictionary* pAvdictionary{};
    AVFormatContext* pAVFormatContext{};
    AVCodecContext* pAVCodecContext{};
    SwsContext* pSwsContext{};
    AVFrame* pAVFrame{};
    AVPacket* pAVPacket{};
    int videoStreamIndex{ 0 };
    int videoStreamWidth{};
    int videoStreamHeight{};
    int videoStreamSrcWidth{};
    int videoStreamSrcHeight{};
    int m_consecutiveEmptyFrames{ 0 };
    static constexpr int threadNumber{ 2 };
    static constexpr int kMaxDetectWidth{ 768 };   // 检测用最大宽度
    static constexpr int kMaxDetectHeight{ 432 };  // 检测用最大高度
    static constexpr int kEmptyResultFrameLimit{ 15 }; // 连续 N 帧无码才判定消失
    QThreadPool threadPool;
    std::atomic<bool> m_stop;
    std::string m_streamPlatform;
    std::string m_roomID;
    std::string m_lastQRTicket;
    bool m_hasActiveQR{ false };
    int m_qrCount{ 0 };

    std::mutex m_decodedResultsMutex;
    std::deque<DecodeResult> m_decodedResults;
};
