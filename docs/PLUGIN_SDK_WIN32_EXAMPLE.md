# Plugin SDK — Win32 Integration Example

A headless Win32 (no Qt) host that launches the runtime and processes images over IPC, in
both sync and async (batch) modes.

```
ImageProcessorWin32/
├── CMakeLists.txt
└── src/
    ├── main.cpp
    ├── ImageProcessor.h
    └── ImageProcessor.cpp
```

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(ImageProcessorWin32Example)
find_package(OpenCV REQUIRED)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/plugin_sdk)
include_directories(${OpenCV_INCLUDE_DIRS})
if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")
endif()
add_executable(ImageProcessorWin32 src/main.cpp src/ImageProcessor.cpp)
target_link_libraries(ImageProcessorWin32 user32 gdi32 kernel32 advapi32 ${OpenCV_LIBS})
if(MSVC)
    set_property(TARGET ImageProcessorWin32 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()
set_target_properties(ImageProcessorWin32 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
```

## ImageProcessor.h

```cpp
#pragma once
#include <windows.h>
#include <vector>
#include <opencv2/core.hpp>
#include "PluginHostLauncher.h"
#include "PluginHostInterface.h"

class ImageProcessor {
public:
    ImageProcessor();
    ~ImageProcessor();
    bool Initialize(const char* pluginExePath);   // launch + connect
    void Shutdown();
    bool ProcessImage(const cv::Mat& image, int threshold);            // sync
    bool ProcessBatchAsync(const std::vector<cv::Mat>& images);        // async
    bool IsPluginRunning() const;
    const char* GetLastError() const;
private:
    PluginHostLauncher* launcher   = nullptr;
    PluginHostHandle*   pluginHost = nullptr;
    char errorBuffer[512] = {};
    void LogError(const char* message);
};
```

## ImageProcessor.cpp

```cpp
#include "ImageProcessor.h"
#include <cstdio>
#include <cstring>
#include <opencv2/imgproc.hpp>

ImageProcessor::ImageProcessor() { strcpy_s(errorBuffer, sizeof(errorBuffer), ""); }
ImageProcessor::~ImageProcessor() { Shutdown(); }

bool ImageProcessor::Initialize(const char* pluginExePath) {
    launcher = PluginHostLauncher_Create(pluginExePath);
    if (!launcher) { LogError("Failed to create launcher"); return false; }

    // Headless: no runtime window (pure background processing)
    if (!PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_HEADLESS)) {
        LogError("Failed to set UI mode"); return false;
    }

    if (!PluginHostLauncher_Launch(launcher, 5000)) {
        LogError(PluginHostLauncher_GetLastError(launcher)); return false;
    }

    pluginHost = PluginHostLauncher_GetHost(launcher);
    if (!pluginHost) { LogError("Failed to get plugin host"); return false; }

    printf("[OK] Plugin initialized (PID: %d)\n", PluginHostLauncher_GetPID(launcher));
    return true;
}

void ImageProcessor::Shutdown() {
    if (launcher) {
        if (IsPluginRunning()) PluginHostLauncher_Stop(launcher, 3000);
        PluginHostLauncher_Destroy(launcher);
        launcher = nullptr; pluginHost = nullptr;
    }
}

bool ImageProcessor::IsPluginRunning() const {
    return launcher && PluginHostLauncher_IsRunning(launcher);
}
const char* ImageProcessor::GetLastError() const { return errorBuffer; }
void ImageProcessor::LogError(const char* message) {
    strcpy_s(errorBuffer, sizeof(errorBuffer), message);
    fprintf(stderr, "[ERR] %s\n", message);
}

bool ImageProcessor::ProcessImage(const cv::Mat& image, int threshold) {
    if (!pluginHost) { LogError("Plugin not initialized"); return false; }
    if (image.empty()) { LogError("Image is empty"); return false; }

    cv::Mat gray;
    if (image.channels() == 3) cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    else                       gray = image.clone();

    PluginHostImage img = { gray.data, gray.cols, gray.rows, gray.channels };

    char paramsJson[256];
    sprintf_s(paramsJson, sizeof(paramsJson), R"({"threshold": %d})", threshold);

    PluginHostResult result;
    if (!PluginHost_SendAndWait(pluginHost, &img, paramsJson, &result, 10000)) {
        LogError("Plugin timeout"); return false;
    }
    if (strcmp(result.status, "ok") != 0) {
        sprintf_s(errorBuffer, sizeof(errorBuffer), "Plugin error: %s", result.error_message);
        return false;
    }
    printf("[OK] Result: %s\n", result.data);
    return true;
}

bool ImageProcessor::ProcessBatchAsync(const std::vector<cv::Mat>& images) {
    if (!pluginHost) { LogError("Plugin not initialized"); return false; }
    if (images.empty()) { LogError("No images"); return false; }

    // Phase 1: submit all requests (fast)
    for (size_t i = 0; i < images.size(); i++) {
        cv::Mat gray;
        if (images[i].channels() == 3) cv::cvtColor(images[i], gray, cv::COLOR_BGR2GRAY);
        else                           gray = images[i].clone();

        PluginHostAsyncRequest req;
        req.request_id = (int)i;
        req.image = { gray.data, gray.cols, gray.rows, 1 };
        static char paramsBuf[256];
        sprintf_s(paramsBuf, sizeof(paramsBuf), R"({"threshold": %d})", 20 + (int)(i % 3) * 15);
        req.params_json = paramsBuf;
        PluginHost_SendAsyncCommand(pluginHost, &req);
    }

    // Phase 2: collect results
    int completed = 0, timeouts = 0;
    while (completed < (int)images.size()) {
        PluginHostResult result;
        if (PluginHost_PollResult(pluginHost, &result, 100)) {
            printf("  <- Result ID=%d: %s\n", result.request_id, result.data);
            completed++; timeouts = 0;
        } else if (++timeouts > 100) {
            fprintf(stderr, "[ERR] too many timeouts\n"); break;
        }
    }
    return completed == (int)images.size();
}
```

## main.cpp

```cpp
#include <windows.h>
#include <vector>
#include <opencv2/opencv.hpp>
#include "ImageProcessor.h"

void PrintUsage() {
    printf("Usage: ImageProcessorWin32.exe <command>\n");
    printf("  single <image>         process one image\n");
    printf("  batch  <image> <count> process the image N times\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2 || strcmp(argv[1], "help") == 0) { PrintUsage(); return 0; }

    ImageProcessor processor;
    if (!processor.Initialize("VisionLab.exe")) {
        printf("[ERR] init failed: %s\n", processor.GetLastError());
        return 1;
    }

    bool success = false;
    if (strcmp(argv[1], "single") == 0 && argc >= 3) {
        cv::Mat image = cv::imread(argv[2]);
        if (!image.empty()) success = processor.ProcessImage(image, 50);
    } else if (strcmp(argv[1], "batch") == 0 && argc >= 4) {
        int count = atoi(argv[3]);
        cv::Mat image = cv::imread(argv[2]);
        if (!image.empty() && count > 0) {
            std::vector<cv::Mat> images(count, image.clone());
            auto t0 = GetTickCount();
            success = processor.ProcessBatchAsync(images);
            auto ms = GetTickCount() - t0;
            printf("total %.2fs, %.2f ms/image\n", ms / 1000.0, (float)ms / count);
        }
    }

    processor.Shutdown();
    return success ? 0 : 1;
}
```

## Build & run

```bash
mkdir build && cd build
cmake .. && cmake --build . --config Release
./bin/ImageProcessorWin32.exe single test_image.jpg
./bin/ImageProcessorWin32.exe batch  test_image.jpg 10
```

## Key points

**Three processing modes**

- **Sync** — `PluginHost_SendAndWait(host, &img, params, &result, timeout)`: one line, simple,
  but the wait scales with the number of requests.
- **Async batch** — `SendAsyncCommand` for all, then `PollResult` in a loop: ~8× faster for
  many requests; the basis of high-throughput.
- **Hybrid** — interleave submits and polls to keep the queue full.

**Performance**

| Operation | Time |
|---|---|
| Plugin launch | ~500 ms |
| One request | 50–200 ms |
| 10 async requests | ~550 ms (~8× vs sync) |
| Poll | <1 ms |

**FAQ**

- **Plugin path**: pass a name (current dir), an absolute path, or a relative path to
  `Initialize()`.
- **Plugin crash**: check `IsPluginRunning()`, then `Shutdown()` + `Initialize()` to restart.
- **Timeout**: the last arg of `SendAndWait` (sync) or `PollResult` (async).

## See also

- **Compilable host projects** ship in this repository: `Master/QtHost/sample/` (C++/Qt) and
  `Master/WpfHost/sample/` (C#). This Win32 example uses the same C++ SDK.
- [Quick Start](PLUGIN_SDK_QUICK_START.md) · [Qt example](PLUGIN_SDK_QT_EXAMPLE.md)
- [API reference](PLUGIN_SDK_API_REFERENCE.md)
