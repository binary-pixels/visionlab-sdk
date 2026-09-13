# Plugin SDK - Qt 集成完整示例

> **SDK 版本**: V3 多相机槽 API  
> 参考实现: `examples/qt_host_minimal/main.cpp`

## 📋 项目结构

```
MyQtHost/
├── CMakeLists.txt
└── src/
    └── main.cpp
```

## 🛠️ CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyQtHost)

set(CMAKE_AUTOMOC ON)
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)
find_package(OpenCV REQUIRED)

if(NOT TARGET PluginSDK)
    add_subdirectory(path/to/shared/plugin_sdk ${CMAKE_BINARY_DIR}/plugin_sdk)
endif()

add_executable(MyQtHost src/main.cpp)
target_link_libraries(MyQtHost PRIVATE Qt6::Widgets ${OpenCV_LIBS} PluginSDK)
```

## 📄 main.cpp — 单相机最小示例

```cpp
#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QThread>
#include <opencv2/opencv.hpp>

#include "PluginHostLauncher.h"
#include "PluginHostInterface.h"
#include "PluginHostInterfaceExt.h"

// ── 结果轮询线程 ───────────────────────────────────────────────────────────────
class ResultPoller : public QThread {
    Q_OBJECT
public:
    ResultPoller(PluginHostHandle* host, int camCount, QObject* parent = nullptr)
        : QThread(parent), m_host(host), m_camCount(camCount) {}

    void requestStop() { m_stop = true; }

signals:
    void resultReady(int slot, QString json);

protected:
    void run() override {
        int ticks = 0;
        while (!m_stop) {
            for (int slot = 0; slot < m_camCount && !m_stop; ++slot) {
                PluginHostResult res{};
                if (PluginHost_PollResultSlot(m_host, slot, &res, 20))
                    emit resultReady(slot, QString::fromUtf8(res.data));
            }
            if (++ticks % 5 == 0)
                PluginHost_UpdateHostHeartbeat(m_host);
        }
    }

private:
    PluginHostHandle* m_host;
    int               m_camCount;
    std::atomic_bool  m_stop{false};
};

// ── 宿主窗口 ──────────────────────────────────────────────────────────────────
class HostWidget : public QWidget {
    Q_OBJECT
public:
    explicit HostWidget(QWidget* parent = nullptr) : QWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        m_status = new QLabel("Ready", this);
        auto* btnLaunch = new QPushButton("Launch Plugin", this);
        auto* btnSearch = new QPushButton("Search", this);
        layout->addWidget(m_status);
        layout->addWidget(btnLaunch);
        layout->addWidget(btnSearch);

        connect(btnLaunch, &QPushButton::clicked, this, &HostWidget::onLaunch);
        connect(btnSearch, &QPushButton::clicked, this, &HostWidget::onSearch);
    }

    ~HostWidget() override { stopPlugin(); }

private slots:
    void onLaunch() {
        m_launcher = PluginHostLauncher_Create("CircleFitTester.exe");
        if (!m_launcher) { m_status->setText("Create failed"); return; }

        // 配置相机数（必须在 Launch 之前）
        PluginHostHandle* host = PluginHostLauncher_GetHost(m_launcher);
        PluginHost_SetCamCount(host, 1);

        if (!PluginHostLauncher_Launch(m_launcher, 5000)) {
            m_status->setText("Launch failed");
            return;
        }
        m_host = PluginHostLauncher_GetHost(m_launcher);

        m_poller = new ResultPoller(m_host, 1, this);
        connect(m_poller, &ResultPoller::resultReady, this, [this](int slot, QString json) {
            m_status->setText(QString("[slot %1] %2").arg(slot).arg(json.left(120)));
        });
        m_poller->start();
        m_status->setText("Plugin running");
    }

    void onSearch() {
        if (!m_host) return;

        cv::Mat gray = cv::imread("test.png", cv::IMREAD_GRAYSCALE);
        if (gray.empty()) { m_status->setText("Image not found"); return; }

        PluginHostSlotRequest req;
        req.slot_index  = 0;
        req.request_id  = ++m_reqId;
        req.image       = { gray.data, gray.cols, gray.rows, 1 };
        req.params_json = R"({
            "algorithm": "circle_fit",
            "parameters": {
                "roi": { "center": {"x":800,"y":600},
                         "inner_radius":100, "outer_radius":300 },
                "radius_range": {"min":80,"max":320}
            }
        })";

        if (!PluginHost_SendCommandSlot(m_host, &req))
            m_status->setText("Send failed: " + QString(PluginHost_GetLastError(m_host)));
    }

private:
    void stopPlugin() {
        if (m_poller) {
            m_poller->requestStop();
            PluginHost_SendShutdown(m_host);
            if (!m_poller->wait(2000))
                m_poller->terminate();
            delete m_poller; m_poller = nullptr;
        }
        if (m_launcher) {
            PluginHostLauncher_Destroy(m_launcher);
            m_launcher = nullptr;
        }
        m_host = nullptr;
    }

    QLabel*               m_status  = nullptr;
    PluginHostLauncher*   m_launcher = nullptr;
    PluginHostHandle*     m_host    = nullptr;
    ResultPoller*         m_poller  = nullptr;
    uint32_t              m_reqId   = 0;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    HostWidget w;
    w.show();
    return app.exec();
}

#include "main.moc"
```

## 🎯 关键点

| 要素 | 说明 |
|------|------|
| `PluginHost_SetCamCount()` | **必须在 `Launch` 之前调用**，写入共享内存 `MultiCamHeader.cam_count` |
| `PluginHostSlotRequest` | 每帧请求指定 `slot_index`（0~3）和 `request_id` |
| `PluginHost_SendCommandSlot()` | 非阻塞，写入 `CamSlot[i]` 并触发 `Event_H2P_<name>_i` |
| `PluginHost_PollResultSlot()` | 在后台线程轮询，带超时（20~100 ms），主线程不阻塞 |
| `PluginHost_UpdateHostHeartbeat()` | 每 500 ms 调用一次，防止插件端超时断开 |
| `PluginHost_SendShutdown()` | 优雅关闭，插件收到后自行退出 |

## 📄 多相机并发示例（4 路）

```cpp
const int CAM_COUNT = 4;
PluginHost_SetCamCount(host, CAM_COUNT);
PluginHostLauncher_Launch(launcher, 5000);

// 同时向 4 个槽发送请求
for (int i = 0; i < CAM_COUNT; i++) {
    PluginHostSlotRequest req;
    req.slot_index  = i;
    req.request_id  = baseId + i;
    req.image       = images[i];
    req.params_json = paramsJson[i];
    PluginHost_SendCommandSlot(host, &req);
}

// ResultPoller 线程 round-robin 轮询所有槽
for (int i = 0; i < CAM_COUNT; i++) {
    PluginHostResult res{};
    if (PluginHost_PollResultSlot(host, i, &res, 20))
        handleResult(i, res.data);
}
```

## 📚 相关文档

- [宿主开发指南](PLUGIN_HOST_DEV_GUIDE.md)
- [API 参考](PLUGIN_SDK_API_REFERENCE.md)
- [快速开始](PLUGIN_SDK_QUICK_START.md)


## 📋 项目结构

```
MyImageProcessor/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── ImageProcessorWidget.h
│   ├── ImageProcessorWidget.cpp
│   └── ImageProcessor.pro (或 CMakeLists.txt)
└── resources/
    └── test_images/
        └── sample.jpg
```

## 🛠️ 第一步：CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(ImageProcessorQtExample)

# Qt 配置
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS
    Core
    Gui
    Widgets
    REQUIRED
)

find_package(OpenCV REQUIRED)

# 添加 Plugin SDK 路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/plugin_sdk)
include_directories(${OpenCV_INCLUDE_DIRS})

# 构建可执行文件
add_executable(ImageProcessor
    src/main.cpp
    src/ImageProcessorWidget.cpp
)

# 链接库
target_link_libraries(ImageProcessor
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${OpenCV_LIBS}
)

# 设置输出目录
set_target_properties(ImageProcessor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
```

## 📄 第二步：ImageProcessorWidget.h

```cpp
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QProgressBar>
#include <memory>

// Plugin SDK 头文件
#include "PluginHostInterface.h"
#include "PluginHostLauncher.h"

class ImageProcessorWidget : public QWidget {
    Q_OBJECT

public:
    explicit ImageProcessorWidget(QWidget* parent = nullptr);
    ~ImageProcessorWidget() override;

private slots:
    void onLoadImage();
    void onProcessImage();
    void onProcessBatch();
    void onPluginModeChanged(int mode);

private:
    // 初始化插件
    bool initializePlugin();
    void shutdownPlugin();

    // 处理图像
    void processImage(const cv::Mat& image);
    void processBatchAsync(const std::vector<cv::Mat>& images);

    // UI 组件
    QLabel* imageLabel;
    QLabel* originalImageLabel;
    QPushButton* loadButton;
    QPushButton* processButton;
    QPushButton* batchButton;
    QSpinBox* thresholdSpinBox;
    QLineEdit* modeComboBox;
    QProgressBar* progressBar;
    QLabel* statusLabel;

    // Plugin 相关
    PluginHostLauncher* launcher = nullptr;
    PluginHostHandle* pluginHost = nullptr;
    QString pluginExePath;

    // 当前图像
    cv::Mat currentImage;
};
```

## 📄 第三步：ImageProcessorWidget.cpp

```cpp
#include "ImageProcessorWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QComboBox>
#include <opencv2/opencv.hpp>
#include <cstdio>

ImageProcessorWidget::ImageProcessorWidget(QWidget* parent)
    : QWidget(parent) {
    
    setWindowTitle("Image Processor with Plugin");
    setGeometry(100, 100, 1200, 800);

    // 主布局
    auto mainLayout = new QVBoxLayout(this);

    // 上部分：图像显示
    auto imageLayout = new QHBoxLayout();
    
    originalImageLabel = new QLabel();
    originalImageLabel->setMinimumSize(400, 400);
    originalImageLabel->setStyleSheet("border: 1px solid black;");
    originalImageLabel->setText("Original Image");
    
    imageLabel = new QLabel();
    imageLabel->setMinimumSize(400, 400);
    imageLabel->setStyleSheet("border: 1px solid black;");
    imageLabel->setText("Processing Result");
    
    imageLayout->addWidget(originalImageLabel);
    imageLayout->addWidget(imageLabel);
    mainLayout->addLayout(imageLayout);

    // 中部分：控制面板
    auto controlGroup = new QGroupBox("Controls");
    auto controlLayout = new QHBoxLayout();

    loadButton = new QPushButton("Load Image");
    connect(loadButton, &QPushButton::clicked, this, &ImageProcessorWidget::onLoadImage);

    processButton = new QPushButton("Process Single");
    connect(processButton, &QPushButton::clicked, this, &ImageProcessorWidget::onProcessImage);

    batchButton = new QPushButton("Process Batch (10 images)");
    connect(batchButton, &QPushButton::clicked, this, &ImageProcessorWidget::onProcessBatch);

    auto thresholdLabel = new QLabel("Threshold:");
    thresholdSpinBox = new QSpinBox();
    thresholdSpinBox->setRange(0, 255);
    thresholdSpinBox->setValue(50);

    controlLayout->addWidget(loadButton);
    controlLayout->addWidget(processButton);
    controlLayout->addWidget(batchButton);
    controlLayout->addWidget(thresholdLabel);
    controlLayout->addWidget(thresholdSpinBox);
    controlLayout->addStretch();

    controlGroup->setLayout(controlLayout);
    mainLayout->addWidget(controlGroup);

    // 下部分：进度和状态
    progressBar = new QProgressBar();
    progressBar->setMaximum(100);
    progressBar->setValue(0);

    statusLabel = new QLabel("Ready. Please load an image first.");
    statusLabel->setStyleSheet("color: blue;");

    mainLayout->addWidget(progressBar);
    mainLayout->addWidget(statusLabel);

    // 设置布局
    setLayout(mainLayout);

    // 初始化插件
    if (!initializePlugin()) {
        statusLabel->setText("Failed to initialize plugin!");
        statusLabel->setStyleSheet("color: red;");
        loadButton->setEnabled(false);
        processButton->setEnabled(false);
    }
}

ImageProcessorWidget::~ImageProcessorWidget() {
    shutdownPlugin();
}

bool ImageProcessorWidget::initializePlugin() {
    // 创建启动器（假设插件在同目录下）
    pluginExePath = "circle_detect.exe";
    
    launcher = PluginHostLauncher_Create(pluginExePath.toStdString().c_str());
    if (!launcher) {
        statusLabel->setText(QString("Failed to create launcher for %1")
                           .arg(pluginExePath));
        return false;
    }

    // 配置：嵌入到父窗口
    PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);
    
    // 获得这个窗口的句柄
    void* parentHwnd = (void*)winId();
    
    // 在窗口右侧嵌入插件窗口
    if (!PluginHostLauncher_EmbedInto(launcher, parentHwnd, 820, 20, 360, 360)) {
        statusLabel->setText("Failed to configure plugin embedding");
        return false;
    }

    // 启动插件（等待5秒）
    if (!PluginHostLauncher_Launch(launcher, 5000)) {
        statusLabel->setText(
            QString("Failed to launch plugin: %1")
            .arg(PluginHostLauncher_GetLastError(launcher))
        );
        return false;
    }

    // 获得通信句柄
    pluginHost = PluginHostLauncher_GetHost(launcher);
    if (!pluginHost) {
        statusLabel->setText("Failed to get plugin host handle");
        return false;
    }

    statusLabel->setText(
        QString("✓ Plugin initialized (PID: %1)")
        .arg(PluginHostLauncher_GetPID(launcher))
    );
    statusLabel->setStyleSheet("color: green;");

    return true;
}

void ImageProcessorWidget::shutdownPlugin() {
    if (launcher) {
        // 优雅关闭
        if (PluginHostLauncher_IsRunning(launcher)) {
            PluginHostLauncher_Stop(launcher, 3000);
        }
        PluginHostLauncher_Destroy(launcher);
        launcher = nullptr;
        pluginHost = nullptr;
    }
}

void ImageProcessorWidget::onLoadImage() {
    QString fileName = QFileDialog::getOpenFileName(this,
        "Open Image", "", "Image Files (*.jpg *.png *.bmp)");
    
    if (fileName.isEmpty()) return;

    currentImage = cv::imread(fileName.toStdString());
    if (currentImage.empty()) {
        QMessageBox::warning(this, "Error", "Failed to load image");
        return;
    }

    // 缩放到400x300显示
    cv::Mat displayImage;
    cv::resize(currentImage, displayImage, cv::Size(400, 300));
    
    // 转换为RGB
    cv::Mat rgb;
    cv::cvtColor(displayImage, rgb, cv::COLOR_BGR2RGB);
    
    // 显示在UI上
    QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step,
               QImage::Format_RGB888);
    originalImageLabel->setPixmap(QPixmap::fromImage(qimg));

    statusLabel->setText(
        QString("✓ Image loaded: %1x%2")
        .arg(currentImage.cols)
        .arg(currentImage.rows)
    );
    statusLabel->setStyleSheet("color: green;");
}

void ImageProcessorWidget::onProcessImage() {
    if (currentImage.empty()) {
        QMessageBox::warning(this, "Error", "Please load an image first");
        return;
    }

    if (!pluginHost) {
        QMessageBox::warning(this, "Error", "Plugin not initialized");
        return;
    }

    statusLabel->setText("Processing...");
    progressBar->setValue(0);
    qApp->processEvents();  // 让UI更新

    processImage(currentImage);
}

void ImageProcessorWidget::processImage(const cv::Mat& image) {
    // 转换为灰度
    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = image.clone();
    }

    // 构建请求
    PluginHostImage img;
    img.pixels = gray.data;
    img.width = gray.cols;
    img.height = gray.rows;
    img.channels = 1;

    // 构建参数JSON
    char paramsJson[256];
    sprintf(paramsJson, R"({"threshold": %d})",
           thresholdSpinBox->value());

    // 发送 + 等待
    PluginHostResult result;
    statusLabel->setText("Waiting for plugin...");
    progressBar->setValue(50);
    qApp->processEvents();

    if (!PluginHost_SendAndWait(pluginHost, &img, paramsJson, &result, 10000)) {
        statusLabel->setText("Plugin timed out!");
        statusLabel->setStyleSheet("color: red;");
        progressBar->setValue(0);
        return;
    }

    // 检查结果
    if (strcmp(result.status, "ok") != 0) {
        statusLabel->setText(
            QString("Error: %1").arg(result.error_message)
        );
        statusLabel->setStyleSheet("color: red;");
        progressBar->setValue(0);
        return;
    }

    // 结果成功
    statusLabel->setText(
        QString("✓ Result: %1").arg(result.data)
    );
    statusLabel->setStyleSheet("color: green;");
    progressBar->setValue(100);

    // 显示结果
    imageLabel->setText(
        QString("Result:\n%1").arg(result.data)
    );
}

void ImageProcessorWidget::onProcessBatch() {
    if (currentImage.empty()) {
        QMessageBox::warning(this, "Error", "Please load an image first");
        return;
    }

    if (!pluginHost) {
        QMessageBox::warning(this, "Error", "Plugin not initialized");
        return;
    }

    // 创建10个测试图像（复制当前图像）
    std::vector<cv::Mat> images;
    for (int i = 0; i < 10; i++) {
        images.push_back(currentImage.clone());
    }

    processBatchAsync(images);
}

void ImageProcessorWidget::processBatchAsync(const std::vector<cv::Mat>& images) {
    statusLabel->setText("Processing 10 images in batch...");
    progressBar->setMaximum(images.size());
    progressBar->setValue(0);
    qApp->processEvents();

    // 阶段1：快速发送所有请求
    std::cout << "Submitting 10 requests...\n";
    
    for (size_t i = 0; i < images.size(); i++) {
        // 转换为灰度
        cv::Mat gray;
        if (images[i].channels() == 3) {
            cv::cvtColor(images[i], gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = images[i].clone();
        }

        // 构建异步请求
        PluginHostAsyncRequest req;
        req.request_id = i;
        req.image.pixels = gray.data;
        req.image.width = gray.cols;
        req.image.height = gray.rows;
        req.image.channels = 1;
        
        // 不同请求用不同阈值
        static char paramsJson[256];
        sprintf(paramsJson, R"({"threshold": %d})",
               20 + (i % 3) * 15);  // 20, 35, 50...
        req.params_json = paramsJson;

        if (!PluginHost_SendAsyncCommand(pluginHost, &req)) {
            std::cerr << "Failed to send request " << i << "\n";
        }
    }

    std::cout << "All requests submitted. Collecting results...\n";

    // 阶段2：轮询结果
    int completed = 0;
    while (completed < (int)images.size()) {
        PluginHostResult result;

        if (PluginHost_PollResult(pluginHost, &result, 100)) {
            // 收到一个结果
            printf("Request %d: %s\n", result.request_id, result.data);
            completed++;
            progressBar->setValue(completed);
            qApp->processEvents();
        } else {
            // 超时，但还有待处理的请求，继续等待
            if (completed < (int)images.size()) {
                qApp->processEvents();  // 保持UI响应
            }
        }
    }

    statusLabel->setText(
        QString("✓ Batch processing complete: %1 images")
        .arg(images.size())
    );
    statusLabel->setStyleSheet("color: green;");
}
```

## 📄 第四步：main.cpp

```cpp
#include <QApplication>
#include "ImageProcessorWidget.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    ImageProcessorWidget widget;
    widget.show();

    return app.exec();
}
```

## 🚀 编译和运行

```bash
# 创建构建目录
mkdir build
cd build

# CMake 配置
cmake ..

# 编译
cmake --build . --config Release

# 运行
./bin/ImageProcessor
```

## 🎯 关键点

1. **自动化启动**：`PluginHostLauncher_Create` + `Launch` 一键搞定
2. **UI嵌入**：`EmbedInto` 自动处理 SetParent 等底层操作
3. **同步模式**：`SendAndWait` 简单易懂，适合单个请求
4. **异步模式**：`SendAsyncCommand` + `PollResult` 用于批处理
5. **错误处理**：所有函数返回 bool，`GetLastError` 获取详细错误

## 💡 扩展想法

- 添加拖放加载图像
- 支持视频处理（逐帧发送）
- 添加参数滑条实时调整
- 显示性能统计（处理时间）
- 支持多个插件并行处理

## 📚 完整参考

详见：
- [快速开始](PLUGIN_SDK_QUICK_START.md)
- [API参考](PLUGIN_SDK_API_REFERENCE.md)
- [常见问题](PLUGIN_SDK_FAQ.md)
