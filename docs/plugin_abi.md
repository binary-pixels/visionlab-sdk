# 插件 ABI（进程内算法插件）— 最小设计

> 状态：**已实现 v1（2026-08-31）**。
>

---

## 1. 设计原则

- **纯 C ABI + JSON 契约**。跨 DLL 边界不传任何 C++ 对象（`cv::Mat`、`QJsonObject`、STL 容器在 DLL 边界是 ABI 灾难）。
- **schema 复用**：插件声明的 `schema` 就是 `ParamFormWidget` 现在消费的字段格式 → 参数面板 **零 UI 改动**自动生成。
- **结果复用**：插件返回的 `overlay` 映射到现有 `DetectionOutcome` 字段 → 走现有 `applyDetectionOverlay` 渲染（青色）。
- **配方复用**：插件算法是「algorithm key + JSON 参数」，配方序列化/约束引擎天然兼容（见 §7）。
- **宿主分配缓冲，插件只填**。不跨 DLL 分配/释放内存。

---

## 2. 头文件 `circle_qt_plugin.h`（交付给客户的唯一头文件）

> 已实现于 `shared/plugin_sdk/algo_plugin/circle_qt_plugin.h`（随仓库 CMake 目标分发），`CIRCLE_QT_PLUGIN_API_VERSION = 1`。下方摘录与实现一致（注释略作精简）。

```c
#ifndef CIRCLE_QT_PLUGIN_H
#define CIRCLE_QT_PLUGIN_H

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

/* 图像：行主序。channels=1 灰度 或 3 BGR。
   输入 CQImage.data 宿主所有、插件只读（约定）；输出显示图 outDisplayBgr->data
   为宿主分配的可写缓冲（建议按输入 rows*cols*3 预留）。 */
typedef struct {
    unsigned char* data;
    int rows;
    int cols;
    int channels;   /* 1 或 3 */
    int step;       /* 每行字节数（≥ cols*channels，允许 stride） */
} CQImage;

/* ── 版本握手：宿主加载 DLL 后先调，不匹配就拒绝加载 ── */
CQ_EXPORT int circle_qt_plugin_api_version(void);

/* ── 注册：返回该插件所有算法的元数据 JSON（写入 outJson）──
   返回 0 成功；<0 出错；outBufSize 不足返回 -1。 */
CQ_EXPORT int circle_qt_plugin_register(const char* appVersion,
                                        char* outJson, int outBufSize);

/* ── 运行：一个通用入口，algo 决定走哪个算法。
   返回 0 成功（含算法判 NG，NG 在 result JSON 的 ok 字段）；
   <0 硬错误（入参非法/插件内部异常），errBuf 给可读原因。 */
CQ_EXPORT int circle_qt_plugin_run(const char* algo,
                                   const CQImage* img,
                                   const char* paramsJson,
                                   char* outResultJson, int outResultBufSize,
                                   CQImage* outDisplayBgr,   /* 可空 */
                                   char* errBuf, int errBufSize);

#ifdef __cplusplus
}
#endif
#endif
```

**约定**：所有字符串 UTF-8；缓冲区宿主分配、插件填写；插件不得返回自己 `new`/`malloc` 的指针。

---

## 3. 注册 JSON（`circle_qt_plugin_register` 输出）

```json
{
  "api_version": 1,
  "algorithms": [
    {
      "name": "customer_pin_hole",
      "label": "Pin hole (customer)",
      "category": "measure",
      "roi_type": "circle",
      "schema": [
        {"name": "roi.center.x", "label": "Center X", "type": "double",
         "min": 0, "max": 2448, "step": 1, "default": 0, "slider": true},
        {"name": "min_diameter", "label": "Min diameter", "type": "double",
         "min": 0, "max": 2000, "step": 1, "default": 50, "slider": true}
      ],
      "defaults": { "roi": {"center": {"x": 0, "y": 0}},
                    "min_diameter": 50 }
    }
  ]
}
```

字段说明：

| 字段 | 说明 |
|------|------|
| `name` | 算法唯一 key（配方里用这个引用） |
| `label` | UI 显示名 |
| `category` | 分类元数据：`measure` / `defect` / `other`。当前 UI 把所有插件算法统一挂在一个顶层「Plugins」分组，未按 `category` 分叶（见 §5） |
| `roi_type` | 固定枚举：`circle` / `ellipse` / `line` / `rect` / `tm` / `none`，映射到现有 `ROIType`；`none`（或空）→ 该插件算法隐藏 ROI 编辑器（全局阈值类） |
| `schema` | **就是 `ParamFormWidget` 的字段格式**（`name/label/type/min/max/step/default/slider`），宿主直接 `setSchema` |
| `defaults` | 配方/加载配置时的默认参数（dotted-path 嵌套 JSON） |

`schema` 类型：`int` / `double` / `bool` / `enum`（带 `options` 数组）/ `string`，与内置算法完全一致。

> 仓库真实示例 `shared/plugin_sdk/algo_plugin/examples/pin_hole/pin_hole_plugin.cpp` 注册的是 `customer_pin_hole`：`roi_type=none`、`category=measure`，schema 为 `thresh_value`(int) / `min_radius`(double) / `max_radius`(double) / `dark_foreground`(bool)，均带 `slider`（除 bool）。

---

## 4. 运行结果 JSON（`circle_qt_plugin_run` 输出）

```json
{
  "ok": true,
  "status": "Pin hole diameter OK",
  "ms": 1.2,
  "measurements": {
    "diameter_px": 88.5,
    "diameter_mm": 2.14,
    "diameter_sigma_px": 0.3
  },
  "overlay": {
    "circles": [{ "cx": 320.0, "cy": 240.0, "radius": 88.5 }],
    "lines":   [{ "x1": 0, "y1": 0, "x2": 10, "y2": 10 }],
    "rects":   [{ "cx": 0, "cy": 0, "width": 100, "height": 80, "angle": 0 }]
  }
}
```

字段说明：

| 字段 | 说明 |
|------|------|
| `ok` | 算法判定（OK / NG），`status` 给可读文字 |
| `ms` | 耗时（宿主展示 + 结果面板） |
| `measurements` | **数值/字符串键值对**，喂给配方约束引擎与结果面板；`points` / `line_x1/y1/x2/y2` 供跨拍聚合消费（见 §7.3） |
| `overlay.circles/lines/rects` | 映射到现有 `DetectionOutcome` 的 `hasCircle`/`hasLine`/`rectResults` → 现有青色渲染 |
| `outDisplayBgr`（可空） | 插件自己烤好整幅 BGR 图 → 宿主按 `replaceDisplayImage` 显示（与 arc/caliper 的烤图路径一致）。**自定义形状/调试叠加用它兜底** |

**overlay → DetectionOutcome 映射**：

| 插件 overlay | DetectionOutcome | 显示 |
|---|---|---|
| `circles[]` | `hasCircle` / `cx,cy,radius` | 青色圆 + 十字（`setFitResult`） |
| `lines[]` | `hasLine` / `linePt1,linePt2` | 青色线（`setLineFitResult`） |
| `rects[]` | `rectResults` | 青色矩形（`setRectangleFitResults`） |
| `outDisplayBgr` | `displayImg` + `replaceDisplayImage=true` | 整幅替换 |

第一版只支持圆/线/矩形 + 烤图兜底；自定义点云叠加（`points`）列入 v1.1。

---

## 5. 宿主集成（已实现）


| 位置 | 现状 |
|---|---|
| `PluginRegistry` | `scanAndLoad()`：`QLibrary` 加载 → resolve 三个导出 → `api_version` 握手（不匹配拒载）→ `register` 解析元数据 → 算法名冲突检测（同名单拒载）；对外 `algorithms() / meta() / defaultsFor() / isPluginAlgorithm() / run()` |
| 算法树 | 新增顶层「Plugins」分组，每个插件算法一项（`AlgorithmType::PluginAlgo` + 插件算法名于 UserRole）；点击复用 `onAlgoTreeChanged` |
| 参数页 | 通用插件表单 `m_formPlugin`（`ParamFormWidget`）：`setSchema(meta.schema)` + `setValues(meta.defaults)` —— 复用，零 UI 改动 |
| ROI 编辑器 | 按 `meta.roiType`：`none`/空 → `setShowROI(false)`；否则 `pluginRoiType()` 映射到现有 `ROIType` |
| `DetectionWorker::computeOne` | 快照带 `pluginAlgo/pluginParams` 时走 `runPluginAlgorithm()`（`PluginRegistry::run` → 结果 JSON 填 `DetectionOutcome`） |
| 配方执行 `runStepsOnImage` | 步骤算法是插件算法时在 `fillParamsFromJson` 前拦截，直接 `PluginRegistry::run`（见 §7.1） |

执行分支（单发与配方步骤共用 `runPluginAlgorithm`）：

```cpp
// computeOne：插件算法由快照标记（非 native runner）
if (!s.pluginAlgo.isEmpty())
    return runPluginAlgorithm(s);      // → PluginRegistry::run() → DetectionOutcome

// 配方步骤（runStepsOnImage 内，fillParamsFromJson 之前拦截）
if (PluginRegistry::instance().isPluginAlgorithm(r.algorithm)) {
    snap.pluginAlgo   = r.algorithm;
    snap.pluginParams = parameters;
    const DetectionOutcome po = runPluginAlgorithm(snap);
    r.ok = po.success;  r.ms = po.ms;  r.statusText = po.statusText;
    r.measurements = po.measurements;   // 约束引擎可直接消费
    // ...
}
```

校验：`tests/unit/test_plugin_registry.cpp`（`PluginRegistryTests`，构建示例插件并 `scanAndLoad` → 断言 `customer_pin_hole` 元数据 → `run()` 返回 0 且测量合理；插件未构建时 `GTEST_SKIP`，需 `CIRCLE_QT_BUILD_PLUGIN_EXAMPLES=ON`）。

---

## 6. 交付物清单

1. ✅ `shared/plugin_sdk/algo_plugin/circle_qt_plugin.h` + 导出宏 —— ABI 定稿 v1。
2. ✅ 示例插件工程 `shared/plugin_sdk/algo_plugin/examples/pin_hole/`（CMake；主构建 `CIRCLE_QT_BUILD_PLUGIN_EXAMPLES=ON` 门控）：一个 `customer_pin_hole`，用 `overlay.circles` + `measurements` 返回结果。
3. ✅ 宿主 `PluginRegistry`：扫描/加载/握手/注册 → 算法树 + 参数表单（见 §5）。
4. ✅ `DetectionWorker` 插件分支 + 结果 JSON 解析（单发 + 配方步骤）。
5. （v1.1）`ImageDisplay` 加 `pluginPoints` 叠加，支持自定义点云/`overlay.points`。

---

## 7. 配方兼容性（关键问题：这个 ABI 能跑配方吗？）

**能跑步骤 + 约束 + 跨拍聚合**（插件的 `measurements` 原样透传，聚合机制已就位）。

配方执行路径 `DetectionWorker::runStepsOnImage` 对每个 step 先在 native registry 之外拦截插件算法（见 §5），不会走到 `fillParamsFromJson` 的"不支持的算法"失败：

```cpp
if (PluginRegistry::instance().isPluginAlgorithm(r.algorithm)) {
    snap.pluginAlgo   = r.algorithm;
    snap.pluginParams = parameters;
    const DetectionOutcome po = runPluginAlgorithm(snap);   // → PluginRegistry::run()
    r.ok = po.success;  r.ms = po.ms;
    r.statusText = po.statusText;
    r.measurements = po.measurements;   // 原样透传，约束/聚合直接消费
    // ...
}
```

### 7.1 支持步骤（Step）— ✅ 已实现
- 插件步骤执行已接线（见 §5 代码）；步骤编辑器（算法选择列表 + 参数表单）自动包含插件算法。
- 配方的**保存/加载**本就只存 `{algorithm, parameters, emit, ...}` JSON，插件参数原样存取，**序列化零改动**。
- 提示：插件收到的是宿主当前**整幅工作图**（gray/BGR，无 ROI 裁剪——native 算法的 ROI 裁剪是在各自 runner 内部做的）。插件需要 ROI 时，把 roi 坐标/尺寸放进参数 JSON，由插件自行裁剪；后续可评估宿主回传 ROI 归一化坐标。

### 7.2 支持约束（Constraint）— ✅ 已实现
约束引擎 `evaluateRecipeConstraints` 按 step 的 `measurements` 键取值（如 `diameter_px`、`blob_count`）。插件结果 JSON 的 `measurements` 直接填入 `RecipeStepResult.measurements` → **约束零改动**。

示例：`customer_pin_hole.diameter_px` 约束、半径区间约束等直接可用（内置示例未带 mm 标定，用像素键示例）。

### 7.3 跨拍聚合（Aggregate）— ✅ 已实现（机制已就位，示例未演示）
跨拍聚合（`fit_line_from_points` / `fit_circle_from_points` / `fit_rect_from_points` / `angle_between_lines` 等）消费的是：带 `"emit":"tag"` 步骤的 `RecipeStepResult.measurements` 里的 `points:[{x,y},...]`（点池）或 `line_x1/y1/x2/y2`（线池）。

插件步骤的 `measurements` 原样透传到 `RecipeStepResult.measurements`，因此插件只需在结果 JSON 里返回：

```json
{
  "ok": true,
  "measurements": {
    "radius_px": 88.5,
    "points": [ { "x": 318.1, "y": 241.0 }, { "x": 322.0, "y": 239.5 } ]
  }
}
```

并在配方步骤编辑里给该步骤打 `emit` tag，就能作为聚合输入——**不需要新增顶层 `emit` 字段或新 ABI 版本**（点坐标约定为原图像素坐标，与聚合点池一致）。

> 仓库尚无"插件 → 聚合"的端到端用例；示例插件也未返回 `points`。若未来要支持"插件自定义语义点（非几何边缘点）"，仍可评估在结果 JSON 增加显式结构。

### 7.4 结论

| 配方能力 | v1 现状 | 备注 |
|---|---|---|
| 步骤执行 + OK/NG | ✅ 已实现 | `runStepsOnImage` 插件分支（§5） |
| 约束判定（measurements） | ✅ 已实现 | `measurements` 原样透传，零改动 |
| 跨拍聚合点池 | ✅ 机制就位 | 插件在 `measurements` 返回 `points`/`line_*` + 步骤 `emit` tag 即可；示例未演示 |

---

## 8. ABI 稳定性规则

- **ABI 冻结**：`CQImage`、三个导出函数签名一旦发布，改动必须升 `CIRCLE_QT_PLUGIN_API_VERSION`，宿主按版本拒载。参数/结果 JSON 可**加字段**（向前兼容），不可删/改已有字段语义。
- **信任模型**：插件是进程内原生代码，崩溃会拖垮宿主。第一版面向「自家 SDK / 已签约客户」；不可信插件再谈子进程（那套可参考既有 `PLUGIN_SDK_*.md` 的独立进程模型）。
- **内存纪律**：只写宿主给的缓冲区；不得返回插件自分配内存。
- **图像所有权**：输入 `CQImage.data` 宿主所有、插件**只读**（约定，struct 用非 const 便于复用）；`outDisplayBgr->data` 为宿主分配的可写缓冲（按输入 `rows*cols*3` 预留），插件向其中写入 BGR 显示图并回填 rows/cols/channels/step。

---

## 9. 后续扩展（非 v1）

- `overlay.points` / `points`（插件自定义点云，宿主加 `pluginPoints` 叠加）——尚未实现，当前用烤图兜底
- 插件返回显式"语义点/几何对象"结构（现在可用 `measurements.points` 供聚合，见 §7.3）
- 插件算法的 ROI 裁剪图（v1 插件只拿到整幅工作图）
- 一键 HALCON 后端切换（`runner` 层再加一个 HALCON 实现，与插件无冲突）
- 嵌入式 Python 路径（`nanobind` 解释器，schema 注册 + run 契约与 C ABI 一致）
