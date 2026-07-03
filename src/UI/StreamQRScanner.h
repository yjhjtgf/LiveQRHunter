#pragma once

#include <atomic>
#include <string>
#include <map>

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
    void processStream();
    void setStreamHW();
    void cleanup();

    private slots:
    void onDecoded(const QString& content, const QString& fingerprint);

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
    const int threadNumber{ 2 };
    QThreadPool threadPool;
    std::atomic<bool> m_stop;
    std::string m_streamPlatform;
    std::string m_roomID;
    std::string m_lastQRTicket;
    bool m_hasActiveQR{ false };
    int m_qrCount{ 0 };
};
