#ifndef CIRCLE_QT_PLUGIN_H
#define CIRCLE_QT_PLUGIN_H

/*
 * 进程内算法插件 ABI（In-process Algorithm Plugin ABI）
 * 完整设计见 docs/plugin_abi.md。
 *
 * 与 shared/plugin_sdk 下的「独立进程插件宿主」互补：
 *   - 这里是进程内 C ABI，插件算法直接吃 AlgorithmRegistry / 参数表单 / 配方；
 *   - shared/plugin_sdk 是独立进程 + 嵌入 UI + 共享内存 IPC 的宿主模型。
 *
 * 约定：
 *   - 纯 C ABI：跨 DLL 边界不传任何 C++ 对象（cv::Mat / STL / QJson 都是灾难）。
 *   - 所有字符串 UTF-8；缓冲区由宿主分配、插件填写；不跨边界分配/释放内存。
 *   - 输入 CQImage.data 宿主所有、插件只读；outDisplayBgr->data 为宿主分配的
 *     可写缓冲（建议按输入 rows*cols*3 字节预留），插件向其中写入 BGR 显示图。
 *   - ABI 冻结：导出函数签名 / CQImage 布局一旦发布，改动必须升
 *     CIRCLE_QT_PLUGIN_API_VERSION，宿主按版本拒载。
 */

#ifdef _WIN32
#  ifdef CIRCLE_QT_PLUGIN_BUILD
#    define CQ_EXPORT __declspec(dllexport)
#  else
#    define CQ_EXPORT __declspec(dllimport)
#  endif
#else
#  define CQ_EXPORT __attribute__((visibility("default")))
#endif

#define CIRCLE_QT_PLUGIN_API_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/* 图像：行主序。channels=1 灰度 或 3 BGR。 */
typedef struct {
    unsigned char* data;    /* 输入：宿主内存（插件只读）；输出显示图：宿主分配的可写缓冲 */
    int rows;
    int cols;
    int channels;           /* 1 或 3 */
    int step;               /* 每行字节数（允许 > cols*channels 的 stride） */
} CQImage;

/* ── 版本握手：宿主加载 DLL 后先调，不匹配就拒绝加载 ── */
CQ_EXPORT int circle_qt_plugin_api_version(void);

/* ── 注册：返回该插件所有算法的元数据 JSON（写入 outJson）。
   schema 数组即 ParamFormWidget 的字段格式（name/label/type/min/max/step/
   default/slider），宿主据此自动生成参数表单。
   返回 0 成功；<0 出错；outBufSize 不足返回 -1。 */
CQ_EXPORT int circle_qt_plugin_register(const char* appVersion,
                                        char* outJson, int outBufSize);

/* ── 运行：一个通用入口，algo 决定走哪个算法。
   返回 0 = 成功（含算法判 NG，NG 在 result JSON 的 ok 字段）；
   <0 = 硬错误（入参非法 / 插件内部异常），errBuf 给可读原因。 */
CQ_EXPORT int circle_qt_plugin_run(const char* algo,
                                   const CQImage* img,
                                   const char* paramsJson,
                                   char* outResultJson, int outResultBufSize,
                                   CQImage* outDisplayBgr,   /* 可空 */
                                   char* errBuf, int errBufSize);

#ifdef __cplusplus
}
#endif

#endif /* CIRCLE_QT_PLUGIN_H */
