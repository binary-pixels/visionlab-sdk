# Plugin SDK — Qt Integration Example

A complete Qt host that launches the runtime, embeds its window, and exchanges images over
IPC. SDK version: **V3 multi-camera slot API**.

```
MyQtHost/
├── CMakeLists.txt
└── src/main.cpp
```

## CMakeLists.txt

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

## Minimal host (single camera)

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

// Result polling thread — keeps the UI thread free.
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
            if (++ticks % 5 == 0)                 // heartbeat every ~500 ms
                PluginHost_UpdateHostHeartbeat(m_host);
        }
    }
private:
    PluginHostHandle* m_host;
    int               m_camCount;
    std::atomic_bool  m_stop{false};
};

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
        m_launcher = PluginHostLauncher_Create("VisionLab.exe");
        if (!m_launcher) { m_status->setText("Create failed"); return; }

        // Camera count MUST be set before Launch.
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
            if (!m_poller->wait(2000)) m_poller->terminate();
            delete m_poller; m_poller = nullptr;
        }
        if (m_launcher) { PluginHostLauncher_Destroy(m_launcher); m_launcher = nullptr; }
        m_host = nullptr;
    }

    QLabel*             m_status   = nullptr;
    PluginHostLauncher* m_launcher = nullptr;
    PluginHostHandle*   m_host     = nullptr;
    ResultPoller*       m_poller   = nullptr;
    uint32_t            m_reqId    = 0;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    HostWidget w; w.show();
    return app.exec();
}
#include "main.moc"
```

## Key points

| Item | Note |
|---|---|
| `PluginHost_SetCamCount()` | **must be called before `Launch`** (writes `MultiCamHeader.cam_count`) |
| `PluginHostSlotRequest` | per-frame `slot_index` (0..3) + `request_id` |
| `PluginHost_SendCommandSlot()` | non-blocking; writes `CamSlot[i]` + signals `Event_H2P_<name>_i` |
| `PluginHost_PollResultSlot()` | call from a background thread (20–100 ms timeout) so the UI never blocks |
| `PluginHost_UpdateHostHeartbeat()` | every ~500 ms, or the plugin times out the host |
| `PluginHost_SendShutdown()` | graceful shutdown; the plugin exits itself |

## Multi-camera (4 slots)

```cpp
const int CAM_COUNT = 4;
PluginHost_SetCamCount(host, CAM_COUNT);
PluginHostLauncher_Launch(launcher, 5000);

for (int i = 0; i < CAM_COUNT; i++) {          // send all four
    PluginHostSlotRequest req;
    req.slot_index  = i;
    req.request_id  = baseId + i;
    req.image       = images[i];
    req.params_json = paramsJson[i];
    PluginHost_SendCommandSlot(host, &req);
}

for (int i = 0; i < CAM_COUNT; i++) {          // round-robin poll
    PluginHostResult res{};
    if (PluginHost_PollResultSlot(host, i, &res, 20))
        handleResult(i, res.data);
}
```

## Embedding the runtime window

```cpp
// Before Launch:
PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);
PluginHostLauncher_EmbedInto(launcher, (void*)winId(), 820, 20, 360, 360);
```

## Single request (sync) vs batch (async)

```cpp
// Sync — simplest, good for one image
PluginHostImage img { gray.data, gray.cols, gray.rows, 1 };
PluginHostResult result;
if (PluginHost_SendAndWait(pluginHost, &img, paramsJson, &result, 10000)
    && strcmp(result.status, "ok") == 0)
    show(result.data);

// Async — submit many quickly, then collect
for (size_t i = 0; i < images.size(); i++) {
    PluginHostAsyncRequest req{ int(i), { gray.data, gray.cols, gray.rows, 1 }, params };
    PluginHost_SendAsyncCommand(pluginHost, &req);
}
int done = 0;
while (done < (int)images.size()) {
    PluginHostResult result;
    if (PluginHost_PollResult(pluginHost, &result, 100)) { use(result); done++; }
    qApp->processEvents();
}
```

## Build & run

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
./bin/MyQtHost
```

## See also

- [Quick Start](PLUGIN_SDK_QUICK_START.md) · [API reference](PLUGIN_SDK_API_REFERENCE.md)
- [Host developer guide](PLUGIN_HOST_DEV_GUIDE.md) · [Win32 example](PLUGIN_SDK_WIN32_EXAMPLE.md)
