# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## 项目概述

LiveQRHunter 是一个直播二维码实时监控工具，从抖音/B站直播间实时捕获二维码，重新生成干净的二维码图像并显示，方便手机扫码。

## 构建命令

### 环境要求
- Visual Studio 2022 (MSVC)
- CMake 3.26+
- Qt 6.8
- vcpkg

### 配置和构建
```powershell
# 配置 (使用 vcpkg 工具链)
cmake -B build -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md

# 构建
cmake --build build --config Release --parallel

# 复制模型文件到构建目录
Copy-Item -Recurse -Force ScanModel build\Release\ScanModel
```

### 单元测试
```powershell
# 启用测试构建
cmake -B build -DUNIT_TESTS=ON `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md

# 构建测试
cmake --build build --config Release --parallel

# 运行所有测试
cd build && ctest -C Release

# 运行单个测试
cd build && ctest -C Release -R test_qrscanner
```

## 架构设计

### 目录结构
- `src/Core/`: 核心业务逻辑
  - `LiveStreamLink`: 直播流链接获取（支持抖音/B站）
  - `QRScanner`: 基于 OpenCV WeChat QR Code 的二维码检测
  - `QRCodeInfo`: 二维码信息数据结构
  - `ApiDefs`: API 端点定义（编译期字符串拼接）
- `src/UI/`: Qt 6.8 图形界面
  - `StreamQRScanner`: FFmpeg 视频流处理 + 二维码扫描线程
  - `main.cpp`: 主窗口和 UI 逻辑
  - `UtilMat.hpp`: OpenCV Mat 与 QImage 互转工具
- `3rdparty/QR-Code-generator/`: 二维码生成库（用于重新生成干净二维码）
- `ScanModel/`: WeChat QR Code 检测模型文件（`.prototxt` + `.caffemodel`）
- `tests/`: Google Test 单元测试

### 核心流程
1. **直播流获取**: `LiveStreamLink` 通过平台 API 获取直播流 URL
2. **视频解码**: `StreamQRScanner` 使用 FFmpeg 解码视频流
3. **二维码检测**: `QRScanner` 使用 OpenCV WeChat QR Code 检测器识别二维码
4. **二维码生成**: 检测到二维码后，使用 QR-Code-generator 重新生成干净的白底黑码二维码
5. **UI 显示**: Qt 界面显示二维码图像和历史记录

### 关键技术栈
- **C++23**: 使用现代 C++ 特性
- **Qt 6.8**: 图形界面框架
- **OpenCV**: 二维码检测（WeChat QR Code 模型）
- **FFmpeg**: 视频流解码
- **cpr**: HTTP 客户端库
- **nlohmann/json**: JSON 解析
- **QR-Code-generator**: 二维码生成

### 依赖管理
- **vcpkg**: 管理 cpr 和 opencv
- **FetchContent**: 管理 nlohmann/json 和 FFmpeg（预编译二进制）
- **3rdparty/**: QR-Code-generator 源码

## 开发注意事项

### 编译选项
- 使用 MSVC 编译器，启用 AVX2 指令集
- Release 构建包含调试信息（`/DEBUG /OPT:REF,ICF,LBR`）
- 使用静态 CRT 链接（`x64-windows-static-md`）

### 测试
- 测试框架: Google Test v1.13.0
- 测试文件位于 `tests/` 目录
- 测试需要网络连接（直播流 API 测试）
- 测试工作目录设置为项目根目录（访问 `ScanModel/`）

### 模型文件
- `ScanModel/` 目录包含 WeChat QR Code 检测模型
- 模型文件必须与可执行文件一起部署
- 运行时从 `./ScanModel/` 相对路径加载

### 平台特定
- 仅支持 Windows（MSVC）
- 使用 Windows 特定的库：Ws2_32, wldap32, Normaliz
- Qt 插件需要部署到 `plugins/platforms/` 和 `plugins/styles/`

## 常用代码模式

### 信号槽连接
Qt 信号槽用于 UI 与后台线程通信：
```cpp
QObject::connect(&scanner, &StreamQRScanner::qrCodeDetected, [&](const QRCodeInfo& info) {
    // 处理检测到的二维码
});
```

### 编译期字符串
API 端点使用编译期字符串拼接：
```cpp
namespace api::live::bili {
    constexpr compile_string base{ "https://api.live.bilibili.com" };
    constexpr auto room_init = base + compile_string{ "/room/v1/Room/room_init" };
}
```

### OpenCV 与 Qt 互转
```cpp
// cv::Mat -> QImage
QImage qrImage = CV_8UC1_MatToQImage(qrMat);

// 生成二维码
cv::Mat qrMat = createQrCodeToCvMat(qrContent);
```

## CI/CD

GitHub Actions 工作流（`.github/workflows/build.yml`）：
- 触发条件: push 到 main 分支或推送 tag
- 构建环境: windows-latest
- 自动创建 Release（当推送 tag 时）
- 产物: ZIP 包含可执行文件、DLL 和模型文件
