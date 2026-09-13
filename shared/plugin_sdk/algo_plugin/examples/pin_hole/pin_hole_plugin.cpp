// pin_hole_plugin.cpp — 进程内算法插件示例。
//
// 算法：customer_pin_hole —— 在灰度图中检测暗色圆孔。
// 演示完整 ABI 契约：
//   register：返回算法 schema（ParamFormWidget 字段格式）→ 宿主自动生成参数表单；
//   run     ：图像 + 参数 JSON → 结果 JSON（measurements + overlay）+ 可选烤图。
//
// 依赖：OpenCV（core/imgproc）+ nlohmann/json（本仓库 shared/json.hpp 单头文件）。
// 构建：见本目录 CMakeLists.txt。客户可直接把本工程拷走替换算法主体。
#include "circle_qt_plugin.h"

#include <opencv2/opencv.hpp>
#include <json.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

// 把宿主 CQImage 拷成插件自有 cv::Mat（拷贝，不引用宿主内存，宿主可复用缓冲）。
cv::Mat toCvOwn(const CQImage* img)
{
    const int type = (img->channels == 3) ? CV_8UC3 : CV_8UC1;
    cv::Mat m(img->rows, img->cols, type, img->data, img->step);
    return m.clone();
}

// JSON → 缓冲。缓冲不足返回 -1。
int writeJson(const json& j, char* out, int outBufSize)
{
    const std::string s = j.dump();
    if (static_cast<int>(s.size()) + 1 > outBufSize) return -1;
    std::memcpy(out, s.c_str(), s.size() + 1);
    return 0;
}

void writeError(const char* msg, char* errBuf, int errBufSize)
{
    if (errBuf && errBufSize > 0) std::snprintf(errBuf, errBufSize, "%s", msg);
}

} // namespace

extern "C" {

CQ_EXPORT int circle_qt_plugin_api_version(void)
{
    return CIRCLE_QT_PLUGIN_API_VERSION;
}

CQ_EXPORT int circle_qt_plugin_register(const char* /*appVersion*/, char* outJson, int outBufSize)
{
    json schema = json::array();
    schema.push_back(json{{"name", "thresh_value"}, {"label", "Threshold"}, {"type", "int"},
                          {"min", 1}, {"max", 255}, {"default", 100}, {"slider", true}});
    schema.push_back(json{{"name", "min_radius"}, {"label", "Min radius"}, {"type", "double"},
                          {"min", 1}, {"max", 2000}, {"step", 1}, {"default", 5}, {"slider", true}});
    schema.push_back(json{{"name", "max_radius"}, {"label", "Max radius"}, {"type", "double"},
                          {"min", 1}, {"max", 2000}, {"step", 1}, {"default", 1000}, {"slider", true}});
    schema.push_back(json{{"name", "dark_foreground"}, {"label", "Dark foreground"}, {"type", "bool"},
                          {"default", true}});

    json algo;
    algo["name"] = "customer_pin_hole";
    algo["label"] = "Pin hole (customer)";
    algo["category"] = "measure";
    algo["roi_type"] = "none";   // circle / ellipse / line / rect / tm / none
    algo["schema"] = schema;
    algo["defaults"] = json{{"thresh_value", 100}, {"min_radius", 5},
                            {"max_radius", 1000}, {"dark_foreground", true}};

    json root;
    root["api_version"] = CIRCLE_QT_PLUGIN_API_VERSION;
    root["algorithms"] = json::array({algo});
    return writeJson(root, outJson, outBufSize);
}

CQ_EXPORT int circle_qt_plugin_run(const char* algo,
                                   const CQImage* img,
                                   const char* paramsJson,
                                   char* outResultJson, int outResultBufSize,
                                   CQImage* outDisplayBgr,
                                   char* errBuf, int errBufSize)
{
    if (!img || img->rows <= 0 || img->cols <= 0 || !img->data) {
        writeError("invalid image", errBuf, errBufSize);
        return -1;
    }
    if (std::strcmp(algo, "customer_pin_hole") != 0) {
        std::snprintf(errBuf, errBufSize, "unknown algorithm: %s", algo);
        return -1;
    }
    if (img->channels != 1 && img->channels != 3) {
        std::snprintf(errBuf, errBufSize, "unsupported channels: %d", img->channels);
        return -1;
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    // ── 参数（解析失败回退默认）──
    json p;
    try { p = json::parse(paramsJson ? paramsJson : ""); }
    catch (...) { p = json::object(); }
    const int    thresh = p.value("thresh_value", 100);
    const double minR   = p.value("min_radius", 5.0);
    const double maxR   = p.value("max_radius", 1000.0);
    const bool   dark   = p.value("dark_foreground", true);

    // ── 转灰度 ──
    cv::Mat gray;
    if (img->channels == 3) {
        cv::Mat bgr = toCvOwn(img);
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = toCvOwn(img);
    }

    // ── 阈值 → 最大连通域 → 外包圆 ──
    cv::Mat bin;
    cv::threshold(gray, bin, thresh, 255,
                  dark ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY);

    double cx = 0.0, cy = 0.0, radiusPx = 0.0, areaPx = 0.0;
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto& c : contours) {
        const double a = cv::contourArea(c);
        if (a < areaPx) continue;
        cv::Point2f cc; float r = 0.f;
        cv::minEnclosingCircle(c, cc, r);
        cx = cc.x; cy = cc.y; radiusPx = r; areaPx = a;
    }
    const bool ok = (radiusPx >= minR) && (radiusPx <= maxR) && (radiusPx > 0);

    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t0).count();

    // ── 结果 JSON ──
    json result;
    result["ok"] = ok;
    result["status"] = ok ? "Pin hole OK"
                          : (radiusPx <= 0 ? "Pin hole NG (none found)"
                                           : "Pin hole NG (radius out of range)");
    result["ms"] = ms;
    result["measurements"] = json{{"radius_px", radiusPx},
                                  {"diameter_px", radiusPx * 2.0},
                                  {"area_px", areaPx}};
    result["overlay"] = json{
        {"circles", json::array({json{{"cx", cx}, {"cy", cy}, {"radius", radiusPx}}})}};

    // ── 烤图兜底（可选）：把检测圆画到 BGR 副本，写进宿主预留的可写缓冲。
    //    宿主优先用 overlay 渲染青色叠加；需要自定义绘制时可用此图替换显示。
    if (outDisplayBgr) {
        cv::Mat bgr;
        if (img->channels == 3) bgr = toCvOwn(img);
        else cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
        if (radiusPx > 0)
            cv::circle(bgr, cv::Point((int)cx, (int)cy), (int)radiusPx,
                       cv::Scalar(0, 255, 0), 2);
        // 宿主按输入 rows*cols*3 字节预留缓冲；插件直接写入并回填布局。
        std::memcpy(outDisplayBgr->data, bgr.data, (size_t)bgr.rows * bgr.cols * 3);
        outDisplayBgr->rows = bgr.rows;
        outDisplayBgr->cols = bgr.cols;
        outDisplayBgr->channels = 3;
        outDisplayBgr->step = bgr.cols * 3;
    }

    return writeJson(result, outResultJson, outResultBufSize);
}

} // extern "C"
