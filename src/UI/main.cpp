#include <QApplication>
#include <QFuture>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QDateTime>
#include <QGroupBox>
#include <QMenu>
#include <QPointer>

#include <atomic>
#include <fstream>
#include <nlohmann/json.hpp>

#include "StreamQRScanner.h"
#include "UtilMat.hpp"
#include "LiveStreamLink.h"

// === 历史文件 (history.json) ===
// { "qr_history": [...], "room_history": [...] }

static nlohmann::json loadHistoryFile()
{
    std::ifstream f("history.json");
    if (f.is_open())
    {
        auto data = nlohmann::json::parse(f, nullptr, false);
        if (!data.is_discarded() && data.is_object())
            return data;
    }
    return { { "qr_history", nlohmann::json::array() }, { "room_history", nlohmann::json::array() } };
}

static void saveHistoryFile(const nlohmann::json& data)
{
    std::ofstream f("history.json");
    f << data.dump(2);
}

static void saveQRHistory(QListWidget* list, nlohmann::json& historyData)
{
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < list->count(); i++)
    {
        arr.push_back({
            { "text", list->item(i)->text().toStdString() },
            { "content", list->item(i)->data(Qt::UserRole).toString().toStdString() }
        });
    }
    historyData["qr_history"] = arr;
}

static void loadQRHistory(QListWidget* list, const nlohmann::json& historyData)
{
    if (!historyData.contains("qr_history") || !historyData["qr_history"].is_array())
        return;
    for (const auto& item : historyData["qr_history"])
    {
        auto* wi = new QListWidgetItem(QString::fromStdString(item.value("text", "")));
        wi->setData(Qt::UserRole, QString::fromStdString(item.value("content", "")));
        list->addItem(wi);
    }
}

static void saveRoomHistory(QComboBox* combo, nlohmann::json& historyData)
{
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < combo->count(); i++)
        arr.push_back(combo->itemText(i).toStdString());
    historyData["room_history"] = arr;
}

static void loadRoomHistory(QComboBox* combo, const nlohmann::json& historyData)
{
    if (!historyData.contains("room_history") || !historyData["room_history"].is_array())
        return;
    for (const auto& item : historyData["room_history"])
        combo->addItem(QString::fromStdString(item.get<std::string>()));
}

// === 配置文件 (config.json) ===

static void saveConfig(QWidget* w, int platformIdx, bool pinned)
{
    nlohmann::json cfg;
    cfg["platform"] = platformIdx;
    cfg["always_on_top"] = pinned;
    cfg["window_width"] = w->width();
    cfg["window_height"] = w->height();
    std::ofstream f("config.json");
    f << cfg.dump(2);
}

static nlohmann::json loadConfigFile()
{
    std::ifstream f("config.json");
    if (f.is_open())
    {
        auto data = nlohmann::json::parse(f, nullptr, false);
        if (!data.is_discarded() && data.is_object())
            return data;
    }
    return nlohmann::json::object();
}

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName("LiveQRHunter");

    QWidget w;
    w.setWindowTitle("LiveQRHunter");
    w.setMinimumSize(380, 500);

    QVBoxLayout* root = new QVBoxLayout(&w);

    // -- 二维码显示区 (自适应窗口, 白底, 内边距) --
    QLabel* qrLabel = new QLabel();
    qrLabel->setAlignment(Qt::AlignCenter);
    qrLabel->setMinimumSize(100, 100);
    qrLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    qrLabel->setStyleSheet(
        "background-color: white;"
        "border: 2px solid palette(mid);"
        "padding: 10px;");
    qrLabel->setText(QString::fromUtf8("等待二维码..."));
    root->addWidget(qrLabel, 1);

    // -- 信息区 --
    QLabel* infoLabel = new QLabel();
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("font-size: 11px; padding: 4px;");
    infoLabel->setText(QString::fromUtf8("未检测到二维码"));
    root->addWidget(infoLabel);

    // -- 控制栏 --
    QHBoxLayout* ctrlLayout = new QHBoxLayout();
    QComboBox* platformCombo = new QComboBox();
    platformCombo->addItem(QString::fromUtf8("抖音"));
    platformCombo->addItem(QString::fromUtf8("B站"));
    platformCombo->setMaximumWidth(80);
    QComboBox* roomCombo = new QComboBox();
    roomCombo->setEditable(true);
    roomCombo->lineEdit()->setPlaceholderText(QString::fromUtf8("房间号"));
    roomCombo->setContextMenuPolicy(Qt::CustomContextMenu);
    QPushButton* startBtn = new QPushButton(QString::fromUtf8("▶"));
    startBtn->setFixedSize(40, 30);
    QPushButton* pinBtn = new QPushButton(QString::fromUtf8("📌 置顶"));
    pinBtn->setCheckable(true);
    pinBtn->setMaximumWidth(80);
    ctrlLayout->addWidget(platformCombo);
    ctrlLayout->addWidget(roomCombo, 1);
    ctrlLayout->addWidget(startBtn);
    ctrlLayout->addWidget(pinBtn);
    root->addLayout(ctrlLayout);

    // -- 状态栏 --
    QLabel* statusLabel = new QLabel(QString::fromUtf8("空闲"));
    statusLabel->setStyleSheet("font-size: 10px; padding: 2px;");
    root->addWidget(statusLabel);

    // -- 二维码历史 --
    QGroupBox* historyGroup = new QGroupBox(QString::fromUtf8("▼ 二维码历史"));
    historyGroup->setCheckable(true);
    historyGroup->setChecked(true);
    QVBoxLayout* historyLayout = new QVBoxLayout(historyGroup);
    QListWidget* historyList = new QListWidget();
    historyList->setMaximumHeight(150);
    historyList->setContextMenuPolicy(Qt::CustomContextMenu);
    historyLayout->addWidget(historyList);
    QHBoxLayout* historyBtnLayout = new QHBoxLayout();
    QPushButton* clearHistoryBtn = new QPushButton(QString::fromUtf8("清空历史"));
    clearHistoryBtn->setMaximumWidth(80);
    historyBtnLayout->addStretch();
    historyBtnLayout->addWidget(clearHistoryBtn);
    historyLayout->addLayout(historyBtnLayout);
    root->addWidget(historyGroup);

    // === 扫描器 ===
    StreamQRScanner scanner;

    // === 加载历史和配置 ===
    nlohmann::json historyData = loadHistoryFile();
    loadQRHistory(historyList, historyData);
    loadRoomHistory(roomCombo, historyData);

    nlohmann::json configData = loadConfigFile();
    platformCombo->setCurrentIndex(configData.value("platform", 0));
    w.resize(configData.value("window_width", 450), configData.value("window_height", 650));

    // === 信号连接 ===

    // 检测到二维码
    QObject::connect(&scanner, &StreamQRScanner::qrCodeDetected, [&](const QRCodeInfo& info) {
        if (info.rawContent.empty())
        {
            qrLabel->clear();
            qrLabel->setText(QString::fromUtf8("等待二维码..."));
            infoLabel->setText(QString::fromUtf8("未检测到二维码"));
            return;
        }
        cv::Mat qrMat = createQrCodeToCvMat(info.rawContent);
        QImage qrImage = CV_8UC1_MatToQImage(qrMat);
        int maxW = qrLabel->width() - qrLabel->style()->pixelMetric(QStyle::PM_LayoutLeftMargin)
                    - qrLabel->style()->pixelMetric(QStyle::PM_LayoutRightMargin) - 24;
        int maxH = qrLabel->height() - qrLabel->style()->pixelMetric(QStyle::PM_LayoutTopMargin)
                    - qrLabel->style()->pixelMetric(QStyle::PM_LayoutBottomMargin) - 24;
        qrLabel->setPixmap(QPixmap::fromImage(qrImage).scaled(
            maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation));

        QDateTime dt = QDateTime::fromMSecsSinceEpoch(info.timestamp);
        QString text;
        text += QString::fromUtf8("来源: ") + QString::fromStdString(info.platform) + "    ";
        text += QString::fromUtf8("房间: ") + QString::fromStdString(info.roomID) + "\n";
        text += QString::fromUtf8("时间: ") + dt.toString("HH:mm:ss") + "\n";
        text += QString::fromUtf8("内容: ") + QString::fromStdString(info.rawContent).left(80) + "...";
        infoLabel->setText(text);

        // 历史记录 (最多100条, 按内容去重)
        const QString content = QString::fromStdString(info.rawContent);
        const QString entry = dt.toString("HH:mm:ss") + "  " + content.left(50);
        for (int i = 0; i < historyList->count(); ++i)
        {
            if (historyList->item(i)->data(Qt::UserRole).toString() == content)
            {
                // 已存在: 移到最前并更新时间戳前缀
                auto* item = historyList->takeItem(i);
                delete item;
                break;
            }
        }
        historyList->insertItem(0, entry);
        historyList->item(0)->setData(Qt::UserRole, content);
        while (historyList->count() > 100)
        {
            delete historyList->takeItem(historyList->count() - 1);
        }
        // 不再每帧全量写盘: 由程序退出时统一保存(见 main 末尾 saveHistory)
    });

    // 点击历史条目 → 显示二维码
    QObject::connect(historyList, &QListWidget::itemClicked, [&](QListWidgetItem* item) {
        QString content = item->data(Qt::UserRole).toString();
        if (!content.isEmpty())
        {
            cv::Mat qrMat = createQrCodeToCvMat(content.toStdString());
            QImage qrImage = CV_8UC1_MatToQImage(qrMat);
            int maxW = qrLabel->width() - 24;
            int maxH = qrLabel->height() - 24;
            qrLabel->setPixmap(QPixmap::fromImage(qrImage).scaled(
                maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            infoLabel->setText(QString::fromUtf8("历史记录: ") + content.left(80) + "...");
        }
    });

    // 右键历史条目 → 删除
    QObject::connect(historyList, &QListWidget::customContextMenuRequested, [&](const QPoint& pos) {
        QListWidgetItem* item = historyList->itemAt(pos);
        if (item)
        {
            QMenu menu;
            QAction* deleteAction = menu.addAction(QString::fromUtf8("删除"));
            if (menu.exec(historyList->mapToGlobal(pos)) == deleteAction)
            {
                delete historyList->takeItem(historyList->row(item));
                saveQRHistory(historyList, historyData);
            }
        }
    });

    // 清空历史
    QObject::connect(clearHistoryBtn, &QPushButton::clicked, [&]() {
        historyList->clear();
        saveQRHistory(historyList, historyData);
    });

    // 房间号右键 → 删除单条
    QObject::connect(roomCombo, &QComboBox::customContextMenuRequested, [&](const QPoint& pos) {
        QMenu menu;
        QAction* deleteAction = menu.addAction(QString::fromUtf8("删除"));
        QAction* clearAllAction = menu.addAction(QString::fromUtf8("清空全部"));
        QAction* selected = menu.exec(roomCombo->mapToGlobal(pos));
        if (selected == deleteAction)
        {
            int idx = roomCombo->currentIndex();
            if (idx >= 0)
            {
                roomCombo->removeItem(idx);
                saveRoomHistory(roomCombo, historyData);
            }
        }
        else if (selected == clearAllAction)
        {
            roomCombo->clear();
            saveRoomHistory(roomCombo, historyData);
        }
    });

    // 状态更新
    QObject::connect(&scanner, &StreamQRScanner::statusChanged, [&](const QString& s) {
        statusLabel->setText(s);
    });

    // 开始/停止
    std::atomic<bool> monitoring{ false };
    std::atomic<bool> requestPending{ false };
    QFuture<void> liveInfoFuture;
    QObject::connect(startBtn, &QPushButton::clicked, [&]() {
        try
        {
            if (requestPending.load())
            {
                return;
            }
            if (monitoring.load())
            {
                scanner.stop();
                scanner.wait();
                startBtn->setText(QString::fromUtf8("▶"));
                statusLabel->setText(QString::fromUtf8("已停止"));
                monitoring.store(false);
                return;
            }
            QString roomId = roomCombo->currentText().trimmed();
            if (roomId.isEmpty())
            {
                statusLabel->setText(QString::fromUtf8("请输入房间号"));
                return;
            }
            // 保存房间号到历史 (去重)
            if (roomCombo->findText(roomId) < 0)
            {
                roomCombo->insertItem(0, roomId);
                saveRoomHistory(roomCombo, historyData);
            }
            int idx = platformCombo->currentIndex();
            statusLabel->setText(QString::fromUtf8("获取直播流..."));
            requestPending.store(true);
            startBtn->setEnabled(false);

            QString roomIdCopy = roomId;
            int platformIdx = idx;
            QPointer<QLabel> statusPtr = statusLabel;
            QPointer<QPushButton> startBtnPtr = startBtn;
            QPointer<StreamQRScanner> scannerPtr = &scanner;
            auto monitoringPtr = &monitoring;
            auto requestPendingPtr = &requestPending;
            liveInfoFuture = QtConcurrent::run([platformIdx, roomIdCopy, statusPtr, startBtnPtr, scannerPtr, monitoringPtr, requestPendingPtr]() {
                auto info = GetLiveInfo(static_cast<LivePlatform>(platformIdx), roomIdCopy.toStdString());
                if (!qApp)
                {
                    requestPendingPtr->store(false);
                    return;
                }
                QMetaObject::invokeMethod(qApp, [info, platformIdx, roomIdCopy, statusPtr, startBtnPtr, scannerPtr, monitoringPtr, requestPendingPtr]() mutable {
                    if (!statusPtr || !startBtnPtr)
                    {
                        requestPendingPtr->store(false);
                        return;
                    }
                    if (info.status != LiveStreamStatus::Normal)
                    {
                        statusPtr->setText(QString::fromUtf8("获取直播流失败"));
                        startBtnPtr->setEnabled(true);
                        requestPendingPtr->store(false);
                        return;
                    }
                    if (!scannerPtr)
                    {
                        statusPtr->setText(QString::fromUtf8("获取直播流失败"));
                        startBtnPtr->setEnabled(true);
                        requestPendingPtr->store(false);
                        return;
                    }
                    statusPtr->setText(QString::fromUtf8("连接中..."));
                    scannerPtr->setUrl(info.link);
                    scannerPtr->setStreamContext(platformIdx == 0 ? "Douyin" : "Bilibili", roomIdCopy.toStdString());
                    scannerPtr->start();
                    startBtnPtr->setText(QString::fromUtf8("■"));
                    startBtnPtr->setEnabled(true);
                    monitoringPtr->store(true);
                    requestPendingPtr->store(false);
                }, Qt::QueuedConnection);
            });
        }
        catch (const std::exception& e)
        {
            statusLabel->setText(QString::fromUtf8("错误: ") + QString::fromUtf8(e.what()));
        }
        catch (...)
        {
            statusLabel->setText(QString::fromUtf8("未知错误"));
        }
    });

    // 置顶切换
    QObject::connect(pinBtn, &QPushButton::clicked, [&](bool checked) {
        w.setWindowFlag(Qt::WindowStaysOnTopHint, checked);
        w.show();
        pinBtn->setText(checked ? QString::fromUtf8("📌 取消") : QString::fromUtf8("📌 置顶"));
    });

    w.show();

    int ret = a.exec();

    if (scanner.isRunning())
    {
        scanner.stop();
        scanner.wait();
    }
    if (liveInfoFuture.isRunning())
    {
        liveInfoFuture.waitForFinished();
    }

    // 退出前统一落盘历史与配置(此前只更新内存)
    saveQRHistory(historyList, historyData);
    saveRoomHistory(roomCombo, historyData);
    saveHistoryFile(historyData);
    saveConfig(&w, platformCombo->currentIndex(), pinBtn->isChecked());

    return ret;
}
