#include "QRScanner.h"

#include <windows.h>
#include <filesystem>
#include <array>

namespace
{
constexpr const char* kModelFiles[] = {
    "ScanModel/detect.prototxt",
    "ScanModel/detect.caffemodel",
    "ScanModel/sr.prototxt",
    "ScanModel/sr.caffemodel",
};

std::filesystem::path scanModelDir()
{
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
    {
        std::filesystem::path exePath(buf);
        auto candidate = exePath.parent_path() / "ScanModel";
        if (std::filesystem::exists(candidate / kModelFiles[0]))
        {
            return candidate;
        }
    }
    return std::filesystem::current_path() / "ScanModel";
}
} // namespace

QRScanner::QRScanner()
{
    const auto modelDir = scanModelDir();
    const auto detectProto = (modelDir / "detect.prototxt").string();
    const auto detectModel = (modelDir / "detect.caffemodel").string();
    const auto srProto = (modelDir / "sr.prototxt").string();
    const auto srModel = (modelDir / "sr.caffemodel").string();
    detector = cv::makePtr<cv::wechat_qrcode::WeChatQRCode>(
        detectProto, detectModel, srProto, srModel);
    detector->setScaleFactor(0.4);
}

QRScanner::~QRScanner()
{
}

void QRScanner::decodeSingle(const cv::Mat& img, std::string& qrCode)
{
#ifdef _DEBUG
    auto startTime = std::chrono::high_resolution_clock::now();
#endif
    const std::vector<std::string>& strDecoded = detector->detectAndDecode(img);
    if (strDecoded.size() > 0)
    {
        qrCode = strDecoded[0];
    }
#ifdef _DEBUG
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
    std::cout << static_cast<float>(duration) / 1000000 << " decode: " << qrCode << std::endl;
#endif
}

void QRScanner::decodeMultiple(const cv::Mat& img, std::string& qrCode)
{
    const std::vector<std::string>& strDecoded = detector->detectAndDecode(img);
    for (int i = 0; i < strDecoded.size(); i++)
    {
        qrCode = strDecoded[i];
#ifdef _DEBUG
        std::cout << "decode:" << qrCode << std::endl;
#endif // DEBUG
    }
}