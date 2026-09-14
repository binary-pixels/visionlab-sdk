# Plugin SDK — Quick Start (in-process C ABI)

Write a custom algorithm as a DLL that the VisionLab runtime loads **in-process**. Your
algorithm then behaves like a built-in one: it appears in the algorithm tree, gets an
auto-generated parameter form, and can be used as a **recipe step** and consumed by the
between-step geometry/constraint engine.

> For the out-of-process model (your app drives the runtime over shared-memory IPC), see
> [`PLUGIN_INTEGRATION_GUIDE.md`](PLUGIN_INTEGRATION_GUIDE.md) Part B.

## Prerequisites

Building an in-process plugin needs only three things:

| Need | Provided by |
|---|---|
| `circle_qt_plugin.h` (the C ABI) | **this repo** — `shared/plugin_sdk/algo_plugin/` |
| `json.hpp` (nlohmann/json, single-header, MIT) | **this repo** — `shared/json.hpp` |
| **OpenCV** (core + imgproc) | **you install it** (vcpkg, system package, or prebuilt) — not bundled |

Nothing else (no Qt, no OpenSSL) is needed for a plugin; those are only required to build the
runtime itself. The `pin_hole` example under
`shared/plugin_sdk/algo_plugin/examples/pin_hole/` is a complete, compilable reference — copy
that project and replace the algorithm body.

---

## 1. The ABI in one screen

Header: [`../shared/plugin_sdk/algo_plugin/circle_qt_plugin.h`](../shared/plugin_sdk/algo_plugin/circle_qt_plugin.h)

```c
#define CIRCLE_QT_PLUGIN_API_VERSION 1

typedef struct {
    unsigned char* data;   // input: host memory (read-only); output: host-owned writable buffer
    int rows, cols;
    int channels;          // 1 (gray) or 3 (BGR)
    int step;              // bytes per row (stride may exceed cols*channels)
} CQImage;

CQ_EXPORT int circle_qt_plugin_api_version(void);

CQ_EXPORT int circle_qt_plugin_register(const char* appVersion,
                                        char* outJson, int outBufSize);

CQ_EXPORT int circle_qt_plugin_run(const char* algo,
                                   const CQImage* img,
                                   const char* paramsJson,
                                   char* outResultJson, int outResultBufSize,
                                   CQImage* outDisplayBgr,   // nullable
                                   char* errBuf, int errBufSize);
```

All strings are **UTF-8**. The host allocates every buffer; the plugin only fills it.
**Never allocate/free across the DLL boundary**, and never pass a C++ object (no `cv::Mat`,
STL, or Qt types in the ABI).

## 2. Project layout

```
my_plugin/
├── CMakeLists.txt
└── src/my_plugin.cpp
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_plugin LANGUAGES CXX)
add_library(my_plugin SHARED src/my_plugin.cpp)
target_compile_definitions(my_plugin PRIVATE CIRCLE_QT_PLUGIN_BUILD)
target_include_directories(my_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/plugin_sdk/algo_plugin   # circle_qt_plugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared                          # json.hpp
    ${OpenCV_INCLUDE_DIRS})
find_package(OpenCV REQUIRED)                # external prereq (core + imgproc)
target_link_libraries(my_plugin PRIVATE ${OpenCV_LIBS})
set_target_properties(my_plugin PROPERTIES PREFIX "")
```

## 3. Implement the three exports

```cpp
#include "circle_qt_plugin.h"
#include <opencv2/opencv.hpp>
#include <json.hpp>   // nlohmann/json single header, shipped as shared/json.hpp
using json = nlohmann::json;

static int writeJson(const json& j, char* out, int cap) {
    const std::string s = j.dump();
    if (int(s.size()) + 1 > cap) return -1;     // buffer too small
    std::memcpy(out, s.c_str(), s.size() + 1);
    return 0;
}

extern "C" {

CQ_EXPORT int circle_qt_plugin_api_version(void) { return CIRCLE_QT_PLUGIN_API_VERSION; }

CQ_EXPORT int circle_qt_plugin_register(const char* /*appVersion*/, char* out, int cap) {
    json schema = json::array();
    schema.push_back(json{{"name","thresh_value"}, {"label","Threshold"}, {"type","int"},
                          {"min",1}, {"max",255}, {"default",100}, {"slider",true}});
    schema.push_back(json{{"name","min_radius"}, {"label","Min radius"}, {"type","double"},
                          {"min",1}, {"max",2000}, {"step",1}, {"default",5}, {"slider",true}});

    json algo;
    algo["name"]     = "customer_pin_hole";   // the algorithm key used in recipes
    algo["label"]    = "Pin hole (customer)";
    algo["category"] = "measure";
    algo["roi_type"] = "none";                // none | circle | ellipse | line | rect | tm
    algo["schema"]   = schema;                // host builds the parameter form from this
    algo["defaults"] = json{{"thresh_value",100}, {"min_radius",5}};

    json root;
    root["api_version"] = CIRCLE_QT_PLUGIN_API_VERSION;
    root["algorithms"]  = json::array({algo});
    return writeJson(root, out, cap);
}

CQ_EXPORT int circle_qt_plugin_run(const char* algo, const CQImage* img,
                                   const char* paramsJson,
                                   char* out, int cap, CQImage* outDisplay,
                                   char* err, int errCap) {
    if (std::strcmp(algo, "customer_pin_hole") != 0) {
        std::snprintf(err, errCap, "unknown algorithm: %s", algo);
        return -1;                            // <0 = hard error
    }

    json p = json::object();
    try { p = json::parse(paramsJson ? paramsJson : ""); } catch (...) {}
    const int thresh = p.value("thresh_value", 100);

    // Copy the host buffer into an owned cv::Mat (do not retain host memory).
    cv::Mat gray(img->rows, img->cols, img->channels == 3 ? CV_8UC3 : CV_8UC1,
                 img->data, img->step);
    if (img->channels == 3) cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);
    else gray = gray.clone();

    cv::Mat bin;
    cv::threshold(gray, bin, thresh, 255, cv::THRESH_BINARY_INV);

    double cx = 0, cy = 0, r = 0;
    std::vector<std::vector<cv::Point>> cs;
    cv::findContours(bin, cs, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (!cs.empty()) {
        cv::Point2f c; float rr = 0;
        cv::minEnclosingCircle(cs[0], c, rr);
        cx = c.x; cy = c.y; r = rr;
    }
    const bool ok = r > 0;

    // Result contract:
    //   measurements -> merged into the step's measurements (constraints/aggregates read these)
    //   overlay      -> drawn by the host (circles / lines / rects)
    json result;
    result["ok"]     = ok;
    result["status"] = ok ? "OK" : "NG (none found)";
    result["measurements"] = json{{"radius_px", r}, {"diameter_px", 2*r}};
    result["overlay"]      = json{{"circles", json::array({json{{"cx",cx},{"cy",cy},{"radius",r}}})}};

    // Optional: paint a BGR display image into the host's writable buffer.
    if (outDisplay && outDisplay->data) {
        cv::Mat bgr; cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
        if (r > 0) cv::circle(bgr, {(int)cx,(int)cy}, (int)r, {0,255,0}, 2);
        std::memcpy(outDisplay->data, bgr.data, (size_t)bgr.rows * bgr.cols * 3);
        outDisplay->rows = bgr.rows; outDisplay->cols = bgr.cols;
        outDisplay->channels = 3;    outDisplay->step = bgr.cols * 3;
    }
    return writeJson(result, out, cap);       // 0 = success (NG is data, not a hard error)
}

} // extern "C"
```

Return codes: `0` = ran (the algorithm verdict is in the result's `ok` field);
`< 0` = hard error (bad input / exception) with a reason in `errBuf`.
`writeJson` returns `-1` when the host buffer is too small.

## 4. Deploy & verify

1. Build the DLL (`my_plugin.dll`).
2. Drop it where the runtime loads plugins, then start the runtime.
3. The algorithm appears in the tree as **Pin hole (customer)** with the generated form.
4. Run it on an image; confirm `radius_px` / `diameter_px` and the green overlay.
5. Add it as a **recipe step** — its `measurements` are consumable by constraints
   (`measure_range`, distances, etc.) exactly like built-in steps.

## Rules & gotchas

- **Version handshake**: the host calls `circle_qt_plugin_api_version()` first and refuses a
  mismatch. Bump `CIRCLE_QT_PLUGIN_API_VERSION` (and your return value) on any ABI change.
- **Buffer ownership**: input image is host-owned and read-only; copy what you need.
  The display buffer is pre-sized by the host (≈ `rows*cols*3`); never assume it is larger.
- **No cross-boundary allocation**: fill host buffers, don't return pointers to your memory.
- **UTF-8 everywhere**; `snprintf` a human-readable reason into `errBuf` on any `-1`.
- **Keep `measurements` keys stable** — recipes and constraints reference them by name.
