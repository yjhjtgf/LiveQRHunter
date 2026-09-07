// 用真实直播间截图验证 QR 检测:
//  - 原始分辨率图片能识别出二维码
//  - 模拟 StreamQRScanner 的降采样(最长边上限 768x432, 偶数对齐)后仍能识别
// 这组图片来自各游戏直播间的登录二维码截图(1080p/1440p)。

#include <gtest/gtest.h>

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>

#include "QRScanner.h"

namespace
{
std::vector<std::string> imageFiles()
{
    const std::filesystem::path dir = std::filesystem::path("tests") / "images";
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.is_regular_file())
        {
            const auto ext = entry.path().extension().string();
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp")
            {
                files.push_back(entry.path().string());
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// 与 StreamQRScanner::setStreamHW 相同的降采样策略(长边<=768 且短边>=432, 偶数对齐)
// 注意: 生产用 sws_scale(BILINEAR), 这里用 cv::resize(INTER_AREA) 近似, 尺寸算法保持一致。
void downscaleLikeScanner(const cv::Mat& src, cv::Mat& dst)
{
    constexpr int kMaxDetectLongSide = 768;
    constexpr int kMinDetectShortSide = 432;
    const int srcW = src.cols;
    const int srcH = src.rows;
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
    outW -= outW % 2;
    outH -= outH % 2;
    if (outW < 2) outW = 2;
    if (outH < 2) outH = 2;
    cv::resize(src, dst, cv::Size(outW, outH), 0, 0, cv::INTER_AREA);
}
} // namespace

// 每张真实图都应能在原始分辨率下检出二维码
TEST(RealLiveImages, DetectAtOriginalResolution)
{
    const auto files = imageFiles();
    ASSERT_FALSE(files.empty()) << "tests/images 目录没有图片, 请检查测试资源";
    QRScanner scanner;
    for (const auto& file : files)
    {
        const cv::Mat img = cv::imread(file, cv::IMREAD_COLOR);
        ASSERT_FALSE(img.empty()) << "无法读取: " << file;
        std::string result;
        scanner.decodeSingle(img, result);
        EXPECT_FALSE(result.empty()) << "原图未检出二维码: " << file
                                     << " (" << img.cols << "x" << img.rows << ")";
        if (!result.empty())
        {
            std::cout << "[info] " << file << " -> " << result.substr(0, 90) << "\n";
        }
    }
}

// 关键: 降采样到检测上限后仍能检出(否则 StreamQRScanner 的提速缩放会漏码)
TEST(RealLiveImages, DetectAfterDownscale)
{
    const auto files = imageFiles();
    ASSERT_FALSE(files.empty()) << "tests/images 目录没有图片, 请检查测试资源";
    QRScanner scanner;
    for (const auto& file : files)
    {
        const cv::Mat img = cv::imread(file, cv::IMREAD_COLOR);
        ASSERT_FALSE(img.empty()) << "无法读取: " << file;
        cv::Mat small;
        downscaleLikeScanner(img, small);
        ASSERT_LE((std::max)(small.cols, small.rows), 768);
        ASSERT_EQ(small.cols % 2, 0);
        ASSERT_EQ(small.rows % 2, 0);
        std::string result;
        scanner.decodeSingle(small, result);
        EXPECT_FALSE(result.empty()) << "降采样后未检出二维码: " << file
                                     << " (原 " << img.cols << "x" << img.rows
                                     << " -> " << small.cols << "x" << small.rows << ")";
    }
}
