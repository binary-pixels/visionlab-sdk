# 配方 IPC 协议(RunRecipe / 语义化结果)

| 项目 | 内容 |
|------|------|
| 文档版本 | 1.2 |
| 日期 | 2026-09-13 |
| 适用范围 | 共享内存 IPC V3(V3 布局不变)+ 配方 v4(多点位)+ 语义化结果 JSON |

---

## 1. 背景与目标

circle-qt 是纯软件视觉工具:多"拍照点位"的照片由外部直线电机/龙门等硬件采集,
工具本身不控制运动。宿主(Host)通过共享内存 IPC 驱动工具:

- **入(IPC → 工具)**:多张照片 + 配方参数(算法名 + 算法参数,**内联 JSON**,不按名加载)。
- **出(工具 → IPC → 宿主)**:完整语义结果 —— 目标**位姿**、**有无**、**是否有缺陷**、
  **几何条件是否满足**;结果**还可能是字符串**(二维码 / 条码 / OCR / DataMatrix 内容)。

共享内存布局为 **V3**(params/result 各 ≈2KB)。三套宿主(QtHost / WpfHost / plugin_host.py)
均已使用 V3 字节偏移(2026-08-21 完成 R6 迁移),WpfHost 与 Python 宿主支持与 QtHost 相同的
多图槽(0..N-1)。语义 JSON 必须紧凑、适配 2KB;超限时降级(见 §7)。

---

## 2. 三种执行模式(决策矩阵)

| 维度 | **逐点位模式**(`PushAndSearch`=4) | **整配方模式**(`RunRecipe`=10) | **流式会话**(`StartRecipe`/`PushShot`/`FinishRecipe`) |
|---|---|---|---|
| IPC 调用 | 每点位一次 | 一次调用携带整条配方 | 1 Start + N PushShot + 1 Finish |
| 图像 | 1 张(槽 = camIndex) | 0..N-1 张(槽 0..N-1,**≤4**) | 逐张,每次覆盖相机槽,**无点数上限** |
| 参数 | 单算法 params JSON | 配方 JSON(v1/v2/v3/v4 均可) | 配方 JSON(StartRecipe)+ `{"shot_index":k}`(PushShot) |
| 步骤间约束 | ❌ 无(单算法) | ✅ 扁平步骤序评估 | ✅ 同整配方(FinishRecipe 时评估) |
| 语义结果 | `semantic` 键(单步) | 完整 `shots`+`constraints`+`strings` | 同整配方(最终一次性返回) |
| 算法 ⊥ 运控移动 | ❌ | ❌(等全部图到齐才跑) | ✅(逐 shot 处理藏在移动里) |
| 宿主 | QtHost / WpfHost / plugin_host.py | 三者均为 **V3 多图宿主**(QtHost 规范;WpfHost/Python 已迁移,见 §9 R6) | 三者均支持(逐图覆盖槽 0) |
| 典型用途 | 单相机在线检测、逐点触发 | 多相机/多工位组合判定、二维码+位姿+缺陷联合 | **大点位配方(如 20 张)、串行拍照 + 移动重叠** |

> 逐点位模式为向后兼容增强:结果 JSON 在原 `{status, detail}` 基础上**附加** `semantic` 键,
> 旧宿主解析时忽略即可。

---

## 3. 共享内存布局(不变)

```
Control block  SharedMem_Cmd_{shmName}  20 KB
  [0]       MultiCamHeader  4 KB
  [4096]    CamSlot[0]      4 KB   ← 命令/参数/结果
  ...       CamSlot[1..3]   4 KB
Image block   SharedMem_Img_{shmName}  128 MB
  ImageSlot[i] = i * 32 MB;  ImageHeader(64 B) + 像素
Events       Event_H2P_{shmName}_0..3 / Event_P2H_{shmName}_0..3
```

- `CamSlot.params_data[2012]` — 内联配方 JSON(RunRecipe)或算法参数(PushAndSearch)。
- `CamSlot.result_data[2012]` — 语义结果 JSON(紧凑)。
- 图像槽:`getImgSlot(base, k)`,每槽 32 MB。
- **`CamSlot.writer_seq`(offset 16, P2-2 seqlock)**:宿主写命令前先 `writer_seq++`(奇=写中),
  写完 cmd/params 再 `writer_seq++`(偶=已提交)后触发 H2P。插件只在偶且变化时处理,
  拒绝重复唤醒与撕裂读;字段读后二次校验,宿主中途覆盖则丢弃本次。`0` = 旧宿主(不启用)。
  三宿主(QtHost/WpfHost/plugin_host.py)均已实现;`MultiCamHeader.version != SHM_VERSION`
  时插件拒绝启动 IPC(见 ARCHITECTURE §P2-2)。

### 命令类型(CmdType)

| 值 | 命令 | 说明 |
|---|---|---|
| 1 | `PushImage` | 写图像 + params |
| 2 | `LoadImageFile` | params 含 `{"path":...}` |
| 3 | `Search` | 当前图 + params 运行算法 |
| 4 | `PushAndSearch` | 推图 + 单算法(增强:完成时回包 + `semantic`) |
| 7 | `GetParams` | 返回当前参数 |
| 9 | `Shutdown` | 退出 |
| **10** | **`RunRecipe`** | **整配方:图像槽 0..N-1 + 配方 JSON(params);结果 = 语义 JSON** |
| **11** | **`StartRecipe`** | **流式会话:params = 完整配方 JSON;插件缓存并重置暂存区** |
| **12** | **`PushShot`** | **流式会话:params `{"shot_index":k}` + 图像(相机槽);插件立即跑 shot k** |
| **13** | **`FinishRecipe`** | **流式会话:聚合暂存结果,返回完整语义 JSON,清空会话** |

未知命令 → 工具返回 `{"status":"error","reason":"unknown command"}`(向后兼容)。

---

## 4. 配方 JSON v4(多点位)

```jsonc
{
  "recipe_version": "4.1",
  "recipe_name": "电池端盖三工位总检",
  "shots": [
    { "position": {"x": 0,   "y": 0},     // 机械坐标(示教时由宿主经 IPC 填充)
      "camera": 0,                        // 示教来源相机(槽),-1/缺省 = 未指定
      "image_slot": 0,                    // IPC 模式:图像槽下标(0..3)
      "image": "",                        // 示教/离线路径(空 => 用推送/当前图);IPC 生产忽略
      "light": {"program": 1, "intensity": 80},   // 逐点位光源(宿主控光, 自由字段)
      "steps": [
        { "algorithm": "corner_detect", "parameters": { ... }, "emit": "pts_a" },
        { "algorithm": "golden_template", "parameters": { "template_path": "..." } }
      ] },
    { "position": {"x": 120, "y": 0}, "image_slot": 1,
      "transform": { "mode": "from_position", "angle_deg": 0.8 },   // 局部像点 → 机器坐标系(含 hand-eye 旋转;可省略)
      "steps": [
        { "algorithm": "corner_detect", "parameters": { ... }, "emit": "pts_b" },
        { "algorithm": "ocr", "parameters": { ... } }
      ] },
    { "position": {"x": 240, "y": 0}, "image_slot": 2,
      "steps": [ { "algorithm": "line_fit", "parameters": { ... }, "emit": "edge_c" } ] }
  ],
  "aggregates": [   // 跨点位聚合拟合(可选):所有 shot 跑完后执行
    { "algorithm": "fit_rect_from_lines", "lines": ["e1", "e2", "e3", "e4"] }
    // 4 边定矩形:4 条 line_fit 边线(经各自 emit 标签进线池)→ 求交得矩形位姿。
    // 也可用点池版本: { "algorithm": "fit_rect_from_points", "points": ["pts_a","pts_b","edge_c"] }
  ],
  "constraints": [
    { "type": "center_distance", "a": 4, "b": 0, "max_px": 5 },   // 聚合矩形 ↔ 角点A
    { "type": "angle_diff",      "a": 2, "b": 3, "target_deg": 90.0, "tol_deg": 1.0 }
  ],
  "logic": "AND",
  "preprocess": { ... }
}
```

- **shot → 槽映射**:`shots[k].image_slot` = 图像块槽下标(0..3)。整配方模式下,
  宿主把第 k 张照片写入槽 `image_slot`(或按顺序写槽 0..N-1)。
- **`image` 路径 = 示教/离线专用（关键）**：`shots[k].image` 是**示教时的图像路径**
  (存于某一工艺配方的示教目录,便于离线回放/复现)。**实际生产经 IPC 运行时,程序忽略该路径**,
  改用宿主推送的图像(整配方按 shot 顺序取图像槽;流式由 `PushShot` 逐片传入)。因此换机部署
  无需在目标机放示教图。实现:`runRecipeShots(..., ignoreImagePaths=true)`(IPC RunRecipe/流式
  路径恒为 true;UI"运行配方"离线回放为 false,用路径)。
- **内联体积上限 ~2KB + 大配方回退（关键）**：`params_data` 仅 **2012 字节**,配方 JSON
  超过会被**截断** → 插件解析失败。(旧实现此时误报 `empty image`,现已改为
  `recipe parse failed; inline recipe >2KB truncated?`。)此时宿主改送
  `{"recipe_file":"<绝对路径>"}`(<2KB),插件读取该文件内容作为配方
  (`DetectionWorker::resolveRecipeJson`);`RunRecipe` 与 `StartRecipe` 均适用,
  `IpcServer` 也据此解析 shot 数以读取正确数量的图像槽。UI 保存的完整参数配方常有
  4–7KB,**必须走 `recipe_file`**(QtHost 配方模拟器已自动:>1900 字节即改送路径)。
  > 若希望完全内联,需精简 `parameters`(去掉默认字段)使配方 ≤~2KB。
- **约束按扁平步骤序索引**:shot 0 的 steps 依次为扁平步骤 0,1,...;然后 shot 1 的步骤,
  依此类推;最后是**聚合拟合结果**。上面的 `center_distance a=4 b=0` 指聚合矩形
  (扁平步骤 4)与 shot0-step0(角点A)——
  注意非定位步骤(OCR)中心为 0,被几何约束引用会判 NG(防呆)。
- **跨视野坐标统一(可选)**:每个 shot 的像是**局部像素坐标**。跨 shot 的几何量(距离/角/
  拟合)需先把各 shot 的点变换到统一(机器)坐标系:shot 级 `transform`
  (`{mode:"from_position"|"translate"|"rigid"|"affine",...}`),或配方根级
  `"coordinate_frame":"machine"`(每个有 `position` 的 shot 按 `position(mm)×px_per_mm` 平移;
  加 `"calib":{"cam_to_machine_deg":φ}` 或 `transform.angle_deg` 做 **hand-eye 旋转**)。
  仅影响 `aggregates` 层;`constraints` 引用原始步骤测量仍为局部坐标。详见
  [RECIPE_GUIDE.md](RECIPE_GUIDE.md) §2.4。省略时各 shot 视为同帧(行为不变)。
- **逐点位光源(可选)**:shot 级 `"light":{...}`(自由字段,宿主控光的采集参数)——随配方下发、
  存盘、并在结果里**回显**(帮助宿主关联/日志每个点位用的光)。示教时宿主可在 `PushAndSearch`
  params 带 `light`,插件自动填入该 shot(§6.2)。
- **向后兼容**:无 `shots` 数组时按 v3 扁平 `steps`(单 shot、用推送/当前图)解析;v1 单算法对象亦兼容。

---

## 5. 语义结果 JSON(schema)

```jsonc
{
  "status": "ok|ng",                 // 整体 PASS/NG 速查
  "overall": "PASS|NG",              // = (shots 按 logic AND/OR) AND (约束全过)
  "logic": "AND|OR",
  "shots": [
    { "position": {"x":0,"y":0},     // 来自配方(可为空)
      "image": "slot0 | 路径 | pushed",
      "ok": 1,
      "steps": [
        { "algorithm": "circle_fit", "ok": 1, "ms": 12.3,
          "pose": {"x":100.5, "y":50.2, "an":12.3, "ms":0.9},  // cx/cy 全 0 时省略
          "presence": {"ok": 1},                               // 有无 = 步骤 ok
          "defect": {"count":2, "area":100, "has_defect":true},// 仅缺陷类步骤出现
          "string": "QR123" }                                  // 二维码/OCR/条码/DM
      ] }
  ],
  "constraints": [
    { "type": "center_distance", "ok": 1, "description": "中心距 1.2 px ≤ 5 px" }
  ],
  "aggregates": [                    // 跨点位聚合拟合 + 测量节点(v4.1)
    { "algorithm": "fit_rect_from_points", "ok": 1, "ms": 0.1,
      "pose": {"x": 150.0, "y": 150.0, "an": 0.0},
      "shape": {"type": "rect", "w": 180.0, "h": 180.0, "n": 4} },
    { "algorithm": "distance_points", "ok": 1, "ms": 0.1,
      "pose": {"x": 150.0, "y": 100.0},                  // 锚点=两中点(仅用于串联)
      "shape": {"type": "distance", "v": 100.0} }        // 标量测量值 → shape.v
  ],
  "strings": ["QR123", "..."]         // 所有非空字符串汇总(shot 序,去重)
}
```

**字段来源**

- `pose`:`measurements` 的 `cx` / `cy` / `angle` / `match_score`。非几何步骤 cx=cy=0 → `pose` 省略。
- `defect` 来源优先级:
  `defect_count + defect_total_area`(golden / scratch)→ `crack_count + crack_coverage`(crack)→
  `blob_count + blob_total_area`(blob)→ 否则不出现。
- `string` 优先级:`ocr_text` → `barcode_first_text` → `dm_first_text`(回退
  `barcode_texts` / `dm_texts` 首元素)。空 → 省略。
- `shape`(仅聚合拟合 / 测量节点):`{"type":..,"r":..,"w":..,"h":..,"n":..,"v":..}`。
  - 拟合类:`type` = `line|rect|circle`,带 `r`(半径)/`w`/`h`/`n`(点数或角点数)。
    `fit_rect_from_lines`(4 边定矩形)的 `n` = 角点数;其完整 `measurements` 另含
    `corners`(4 个角点)与 `line_count`,供 SQLite 全量查询。
  - 测量节点:`type` = `distance|angle|point|position|stat`,标量值放 **`v`**
    (距离取 `distance_px`、位置度取 `deviation_px`、角度取 `angle`、统计取 `value`)。
    `point`(交点)无 `v`,数值在 `pose.x/y`。
  - `v` 为**新增可选字段**;旧消费者忽略未知 `type` / `v` 即可(向后兼容)。
- `presence.ok` = 该步骤 `ok`(1/0)。
- 数值取 ≤3 位小数;空字段省略,保持紧凑。

**PushAndSearch 增强回包**(逐点位模式)

```jsonc
{
  "cmd_id": 42,
  "status": "ok|fail",
  "detail": "Center (100.5, 50.2), Radius 30.0 ...",
  "reason": "...",                    // 失败时
  "semantic": { "algorithm":"circle_fit", "ok":1, "ms":12.3,
                "pose":{...}, "presence":{"ok":1} }
}
```

---

## 5.1 输出字段全集(所有可能的输出)

一次配方运行可产生的输出分 **5 条通道**,内容同源,均源自步骤级"语义结果"。

### 5.1.1 输出通道

| 通道 | 内容 | 入口 |
|---|---|---|
| 语义结果 JSON | 配方级 + shot + 步骤/聚合(IPC 回包同款) | `buildRecipeResultJson` |
| IPC RunRecipe 回包 | 同一 JSON,`result_data` **≤2KB**,超限降级(见 §7) | `IpcServer::sendResult` |
| 运行结果弹窗 | 步骤表 + 约束表 + 总判定 | `InspectTab` 结果对话框 |
| SQLite 落库 | **每步一行**,`measurements` 列存该步语义 JSON | `recordRecipeRun` |
| 原始测量值 | 每步 `RecipeStepResult.measurements`(上面字段的来源,全量不受 2KB 限) | `runAggregateStep` / `runStepsOnImage` |

### 5.1.2 配方级键(top-level)

| 键 | 取值 | 说明 |
|---|---|---|
| `status` | `"ok"` / `"ng"` | 整体速查 |
| `overall` | `"PASS"` / `"NG"` | =(shots 按 logic)AND(约束全过)AND(聚合全过) |
| `logic` | `"AND"` / `"OR"` | shot 组合方式 |
| `shots[]` | 数组 | 每点位:`position` / `light`(回显) / `camera` / `image` / `ok` / `steps[]` |
| `aggregates[]` | 数组 | 聚合拟合 + 测量节点(见 5.1.3) |
| `constraints[]` | 数组 | 约束判定(见 5.1.5) |
| `strings[]` | 数组 | 所有非空识别字符串(shot 序,去重) |
| `truncated` / `reason` | `true` / `"detail dropped"\|"result too large"` | 仅降级形态出现(§7) |

### 5.1.3 步骤 / 聚合级字段

| 字段 | 出现条件 | 内容 |
|---|---|---|
| `algorithm` | 总是 | 算法 / 节点名 |
| `name` | 聚合(带 `name`) | 聚合输出名（宿主按名定位 frame/placement） |
| `ok` | 总是 | 1/0(该步 PASS/NG) |
| `ms` | 总是 | 耗时 |
| `pose` | `cx`/`cy` 非 0 | `{x, y, an(angle), ms(match_score)}` |
| `presence` | 总是 | `{ok}` — 有无 |
| `defect` | 缺陷类步骤 | `{count, area, has_defect}` |
| `shape` | 有 `shape_type`(非 frame/placement) | `{type, r, w, h, n, v}`(见 5.1.4) |
| `frame` | `shape_type=="frame"` | 坐标系 `{ox,oy,theta_deg,x_axis,y_axis,perp_deg,unit}`(mm/px) |
| `pattern` / `poses` / `count` | `shape_type=="placement"` | 阵列描述 + count + (片数≤16)枚举位姿数组(机器坐标 x/y/an, mm) |
| `string` | OCR/条码/DM 有内容 | 识别文本 |

### 5.1.4 `shape.type` 取值(= 配方能产出的几何 / 量值结果种类)

| type | 来源节点 | 数值字段 |
|---|---|---|
| `line` | `fit_line_from_points` | `an`(+端点);另含 `rms/max_error_px`(直线度) |
| `rect` | `fit_rect_from_points` / `fit_rect_from_lines` | `w, h, an, n` |
| `circle` | `fit_circle_from_points` / `arc_from_three_points` | `r`(+`diameter`;圆度 `rms/max_error_px`) |
| `point` | `intersect_lines` | `pose.x/y`(交点) |
| `distance` | `distance_points/lines/circles/point_line`、`concentricity` | `v = distance_px`(圆距另含 `gap_px`) |
| `angle` | `angle_between_lines` / `angle_three_points` / `directed_angle_between_lines` | `v = angle`(有向另含 `signed_angle`) |
| `position` | `true_position` | `v = deviation_px`(直径式) |
| `stat` | `aggregate_stat` | `v = value`(另含 `min/max/mean/stddev/count`) |
| `frame` | `build_frame` | **独立 `frame` 对象**:`{ox,oy,theta_deg,x_axis,y_axis,perp_deg,unit}`(非 `shape`) |
| `placement` | `placement_pattern` | **独立 `frame`+`pattern`+`count`(+`poses[]`)**(非 `shape`) |

> 带标定时,距离 / 弧 / 位置度节点另产 `*_mm` 键(在 `measurements`,非 `shape`)。

### 5.1.5 约束输出

`constraints[]` 每项:`{ "type":.., "ok":1|0, "description":".." }`。
`type` 共 **8 类**:`center_distance` / `radius_ratio` / `size_ratio` / `angle_diff` /
`equidistant` / `line_distance` / `symmetry` / `measure_range`。

### 5.1.6 各算法原始 `measurements` 字段(全量,落 SQLite)

| 类别 | 算法 | 主要字段 |
|---|---|---|
| 定位 | `circle_fit` | cx, cy, radius, radius_sigma_px, center_sigma_px, contour_gap_px |
| 定位 | `line_fit` | cx, cy, angle, line_x1..y2 |
| 定位 | `ellipse_fit` / `rectangle_fit` | cx, cy, width, height, angle |
| 定位 | `template_match` | cx, cy, angle, match_score |
| 缺陷/有无 | `blob_analysis` | blob_count, blob_total_area, blob_first_area, points[] |
| 缺陷 | `golden_template` / `scratch_detect` | defect_count, defect_total_area |
| 缺陷 | `crack_detect` | crack_count, crack_coverage |
| 字符串 | `ocr` | ocr_text, ocr_length |
| 字符串 | `barcode` / `data_matrix` | *_count, *_texts[], *_first_text |
| 拟合 | `fit_line_from_points` | cx, cy, angle, line_x1..y2, rms_error_px, max_error_px |
| 拟合 | `fit_circle_from_points` | cx, cy, radius, diameter, rms_error_px, max_error_px |
| 拟合 | `fit_rect_from_lines` | cx, cy, width, height, angle, dir_x/y, corners[], line_count |
| 测量 | 距离节点 | distance_px/_mm, gap_px/_mm, center_distance_px/_mm, dx, dy, offset_px |
| 测量 | `angle_three_points` / `directed_...` | angle, signed_angle, angle_a/b |
| 测量 | `arc_from_three_points` | cx, cy, radius, diameter, central_angle, arc_length_px, chord_length_px |
| 测量 | `aggregate_stat` | value, count, min, max, mean, stddev, field, stat |
| 测量 | `true_position` | dx, dy, lateral_px/_mm, deviation_px/_mm |
| 测量 | `concentricity` | dx, dy, distance_px/_mm, offset_px, eccentricity |

> 语义 JSON 仅暴露上述字段中"语义化"的部分(pose/presence/defect/string/shape);
> 其余细节(如 `corners`、`line_count`、各 `*_mm`)只在 `measurements` 全量里,
> 经 SQLite 查询获取(见 §7)。

---

## 6. 端到端流程(RunRecipe=10)

1. 宿主打开共享内存 + 事件(命名 `{shmName}`)。
2. 宿主把照片写入图像槽:`getImgSlot(imgBase, k)` + `ImageHeader`(宽/高/通道/stride/data_size)+ 像素。
3. 宿主把**配方 JSON** 写入 `CamSlot[cam].params_data`,设置
   `cmd_type=10, cmd_status=Pending, cmd_id=++seq`,然后 `SetEvent(H2P[cam])`。
4. 工具 `IpcServer::processCamera` 读取槽 0..N-1 的图像与 params,
   `emit cmdRunRecipe(cam, cmdId, images, recipeJson)` → `InspectTab::onIpcRunRecipe`。
5. `onIpcRunRecipe` 解析配方 → `runRecipeOnWorker`(QtConcurrent 后台执行)。
6. 完成后(回 GUI 线程)构建语义 JSON → `emit ipcResultReady(cam, cmdId, json)` →
   `MainWindow::onIpcResult` → `IpcServer::sendResult` 写入 `result_data` + `SetEvent(P2H[cam])`。
7. 宿主 `WaitForSingleObject(P2H[cam])` 返回后读取 `result_data` 并解析 JSON。

> **并发守卫**:单全局 `m_detectionRunning`。配方/检测进行中收到新命令 → 立即回
> `{"status":"error","reason":"busy"}`,宿主可稍后重试。

## 6.1 流式配方会话(StartRecipe / PushShot / FinishRecipe)

**适用**:点位很多(远超 4 个图像槽上限,如 20 张)、且拍照是**串行**的(运控逐个点位移动)。
一次性 `RunRecipe` 全发会(1)受 4 槽限制;(2)算法只能等全部图像到齐才开始,**运控移动时间被浪费**。

流式会话让插件**逐 shot 处理**,与运控移动重叠:

```
StartRecipe(配方JSON)                 → 插件缓存配方,重置暂存区,回 {"status":"ok","shots":N}
for k in 0..N-1:
    移运控到位置k → 拍照
    PushShot(k, 图像)                 → 插件【立即】跑 shot k(后台)
                                        ← 宿主同时移往位置k+1(算法 ⊥ 移动)
    (PushShot 在完成时回 {"status":"ok","shot_index":k,"ok":0|1})
FinishRecipe()                        → 插件聚合(4边定矩形/约束)
                                        → 返回完整语义 JSON,清空会话
```

- **图像槽只用一个**:每次 `PushShot` 覆盖相机槽,不受 4 槽限制,大点位配方可行。
- **中间结果由插件暂存**(`runRecipeShot` → staged 结果),`FinishRecipe` 用 `finalizeRecipeShots`
  建点池/线池 → 跑聚合 → 约束 → 返回。结果格式与 `RunRecipe` 完全相同。
- **时序收益**:总耗时 ≈ Σ移动 + 最后一张的算法 + 聚合;前 N-1 张的算法处理藏在移动里。
- **幂等/校验**:`PushShot` 的 `shot_index` 必须等于下一个待收序号;`FinishRecipe` 要求全部收齐,
  否则回 error。会话中途出错后,宿主重新 `StartRecipe` 即可重开。
- **宿主 SDK**:QtHost `sendStartRecipe / sendPushShot / sendFinishRecipe`(V3,规范);
  Python `send_start_recipe / send_push_shot / send_finish_recipe`、WpfHost
  `StartRecipeAsync / PushShotAsync / FinishRecipeAsync`(均为 V3 偏移,逐图覆盖槽 0)。

## 6.2 示教 IPC(拍照 + 机器坐标 + 相机)

示教时,宿主"点击拍照"通过 **`PushAndSearch`(=4)** 把图像 + 算法参数 + **示教元数据**
一次发给插件;插件显示图像,用户设参/搜索/加配方。示教元数据附加在 params 顶层(不破坏
现有 algorithm/parameters):

```jsonc
{
  "algorithm": "line_fit",
  "parameters": { ... },            // 与平时一致
  "position": {"x": 123.5, "y": 45.2},   // 运控当前机器坐标(拍照位)
  "shot": 0,                              // 可选:归属拍照位(shot 下标)
  "camera": 1,                            // 可选:相机编号(通常=槽)
  "light": {"program": 2, "intensity": 80} // 可选:当前点位光源,插件自动填入该 shot
}
```

插件记住这些元数据;用户点"**+ 添加当前算法**"加入配方时,自动把该步骤归入对应 shot
(`shot`,缺省 0)并填充该 shot 的 `position` / `camera`。配方保存/加载时随 `shots[].position`
与 `shots[].camera` 往返。生产时按 §6 / §6.1 发配方与指令,不需要再发 position。

> **相机编号**本身已由 IPC 槽(camIndex 0..3)隐含;`camera` 字段用于示教溯源与多相机
> 配方中记录"这个点位是哪台相机教的",运行时实际使用命令携带的 camIndex。

## 6.3 生产运行数据流（插件侧承载与运算）

以"**4 次拍照 → 相对基板左上角的贴装位姿**"为例（跨视野：上边缘 2 段、左边缘 2 段 →
两条线 → 建坐标系 → 阵列贴装）。核心结论：**插件不缓存图像**——每次 `PushShot` 立刻算完
本 shot，只把**每 shot 的测量结果**暂存；`FinishRecipe` 时用这些结果做跨视野拼接。

### 6.3.1 投喂方式（4 张）

| 方式 | 命令 | 图像承载 | 说明 |
|---|---|---|---|
| 整配方一次发 | `RunRecipe(10)` | 4 张写入图像槽 0..3（`MAX_CAM_SLOTS=4`） | 图都到齐再算，4 张刚好够 |
| **流式会话** | `StartRecipe(11)` + 4×`PushShot(12)` + `FinishRecipe(13)` | 每张**复用槽 0**，无点数上限 | 串行拍照、算法与运控移动重叠（推荐生产） |

配方 JSON 只在 `StartRecipe` 发**一次**；`PushShot` 只带 `{"shot_index":k}` + 图像。

### 6.3.2 插件数据承载（`RecipeSession`）

会话状态（IPC 命令在 GUI 线程串行处理）：

```
startSession(...):
  m_base        ← 标定/预处理快照(pxPerMm 等)
  m_shotSpecs   ← 配方 shots[]（含 position / light / transform / steps）
  m_constraints / m_aggregates / m_logic
  m_stagedResults.clear(); m_nextIndex = 0

每次 pushShot(image, k):
  ① 要求 k == m_nextIndex（严格按序，否则拒绝）
  ② 后台线程跑【本 shot】→ RecipeShotResult
  ③ m_stagedResults.append(结果); m_nextIndex++   ← 只存"结果"，【丢弃图像】
  ④ 回 shotAcked(k, ok)

finishRecipe():
  finalizeRecipeShots(m_shotSpecs, m_stagedResults, m_constraints, m_logic, pxPerMm, m_aggregates)
  → 语义 JSON；清空会话
```

- **只持有测量值**（`line_x1..y2` / `points` / `angle` …），**不驻留像素**；内存与照片张数无关。
- 单全局并发守卫 `m_detectionRunning`：会话进行中收到新命令 → `{"status":"error","reason":"busy"}`。

### 6.3.3 逐 PushShot 的运算（每次只算本 shot）

```
图 k ─► 预处理(灰度/光照补偿) ─► ROI 内边缘提取(Canny/亚像素) ─► 稳健直线拟合
     ─► 结果 line_x1..y2 / angle / points[两端点] ─► 存入 m_stagedResults[k]
```

四张图得到 4 段边缘（局部像素坐标、彼此独立），此时尚无"基板坐标系"。

### 6.3.4 FinishRecipe 的合并运算

```
finalizeRecipeShots(specs, staged[4], constraints, logic, pxPerMm, aggregates):

 ① 建池（跨视野统一到机器坐标系）
    每个 shot 的带 emit 步骤: 局部像素 ─(该 shot transform: position(mm)×px_per_mm + hand-eye 旋转)─► 机器坐标
      pool[T1]+=.., pool[T2]+=..  (上边缘两段)
      pool[L1]+=.., pool[L2]+=..  (左边缘两段)
      同时登记 linePool / namedResults[emit 标签]

 ② 聚合按序执行:
    fit_line_from_points [T1,T2] → "top"     // 上边缘线(机器坐标)
    fit_line_from_points [L1,L2] → "left"    // 左边缘线
    build_frame(lines=[top,left], x_axis=top) → "substrate"
        原点 O = top ∩ left   ← 基板【左上角】的机器坐标
        X 轴 = 上边缘方向；θ = 基板相对机器轴夹角；unit=mm
    placement_pattern(frame="substrate", mode=grid, ...) → "place"
        先在坐标系内展开局部点 local(i)（相对左上角）
        再合成 machine(i) = O + R(θ)·local(i)

 ③ 约束: angle_diff(top,left, 90±tol) …
 ④ 整体 PASS/NG → buildRecipeResultJson → 经 IPC 回包
```

### 6.3.5 "相对基板左上角"的体现 + 数值示例

左上角 = `frame` 原点 O（机器坐标）。贴装阵列的**局部坐标即相对左上角**：
`pattern.origin` / `pitch` 存局部量（`origin_in:"machine"` 时自动反解为局部）；
`poses[]` 存合成后的机器坐标。两者互转：`local = R(-θ)·(machine − O)`。

```
px_per_mm=10, hand-eye=0;  左上角 O=(120.0,80.0)mm, θ=0°
贴装: origin 相对左上角 {x:10,y:8}mm, cols=3, pitch_x=12, dtheta=0
  local(i)   = (10,8) (22,8) (34,8)                    ← 相对左上角
  machine(i) = O + local = (130,88) (142,88) (154,88)   ← 回包 poses[]
```

```jsonc
{"algorithm":"build_frame","ok":1,
 "frame":{"ox":120.0,"oy":80.0,"theta_deg":0.0,"x_axis":[1,0],"y_axis":[0,1],"perp_deg":90.0,"unit":"mm"}},
{"algorithm":"placement_pattern","ok":1,"count":3,
 "frame":{"ox":120.0,"oy":80.0,"theta_deg":0.0,"unit":"mm"},
 "pattern":{"mode":"grid","origin":{"x":10,"y":8},"pitch_x":12,"angle":0,"dtheta":0,"unit":"mm"},
 "poses":[{"x":130,"y":88,"an":0},{"x":142,"y":88,"an":0},{"x":154,"y":88,"an":0}]}
```

基板换位后重算，O/θ 变→整列位姿随坐标系**平移+旋转**，无需重教。

### 6.3.6 时序图（流式，4 点位）

```
宿主                                    插件(RecipeSession / QtConcurrent)
 │  StartRecipe(配方JSON) ─────────────► startSession: 缓存配方, 清暂存
 │  ◄──────────────── {"status":"ok","shots":4}
 │  移到 P1·打光·拍照
 │  PushShot(img0,{shot_index:0}) ─────► runRecipeShot(shot0) [后台]
 │  ◄──────────────── shotAcked(0)
 │  移到 P2·打光·拍照                      (P1 的算法在移动时已算完)
 │  PushShot(img1,{shot_index:1}) ─────► runRecipeShot(shot1)
 │  ◄──────────────── shotAcked(1)      ... P3/P4 同上 ...
 │  FinishRecipe() ────────────────────► finalizeRecipeShots(池化→聚合→建系→阵列→约束)
 │  ◄──────────────── 语义JSON(frame + pattern + poses[] + constraints)
 │  expand_placement() → 逐片驱动贴装头
```

### 6.3.7 工程要点

- **顺序**：`PushShot` 的 `shot_index` 必须等于下一个待收序号，否则拒绝（防错位）；`FinishRecipe`
  要求全部收齐，否则 error。
- **线程**：IPC 命令在 GUI 线程串行；每步运算丢 QtConcurrent 池，结果经信号回 GUI 线程回包（不卡 UI）。
- **2KB**：阵列片数 ≤16 枚举 `poses[]`；更多只给 `frame+pattern`，宿主 `expand_placement()` 展开（§8.4）。
- **持久化**：每步语义结果落 SQLite（不受 2KB 限），回包超限自动 `truncated`（§7）。
- **图像**：不落盘、不驻留；整个会话只堆 ~4 份测量结果。
- **配方常驻（连续生产，可选）**：`StartRecipe` 带配方时插件**缓存**该配方；之后各周期
  的 `StartRecipe` 若 **params 为空 / 只带 `{"recipe_id":..}`**（不含 `shots`/`steps`/`algorithm`）
  则复用缓存 → **每周期不必重发配方**（只发"开始 + N 图 + 结束"）。换配方时再发一次即可；
  插件重启后缓存失效，宿主须重发（QtHost 在插件掉线时复位其"已缓存"标志）。未缓存且没发配方
  → 回 `{"status":"error","reason":"no cached recipe; ..."}`。QtHost 勾选 **`Send recipe once`** 即启用。

---

## 7. 2KB 结果上限与降级

- `result_data` 有效载荷为 2012 字节。`DetectionWorker::buildRecipeResultJson` 先构建完整 JSON,
  序列化后若超过 `kRecipeResultBudgetBytes = 1900`,输出 **`truncated` 降级形态**。
  降级是**分级的**——尽量保留诊断信息,只丢弃最不重要的字段:

**Tier A**(`reason="detail dropped"`):每步仅保留判定(algorithm/ok/ms),丢弃位姿/缺陷/字符串详情;
保留聚合拟合判定、**约束判定**(NG 关键原因)与 `strings` 汇总。超大 `strings` 或聚合仍超限时再丢弃。

```jsonc
{
  "truncated": true,
  "reason": "detail dropped",
  "status": "ok|ng",
  "overall": "PASS|NG",
  "logic": "AND",
  "shots": [ { "position": {...}, "image": "slot0", "ok": 1,
               "steps": [ {"algorithm": "circle_fit", "ok": 1, "ms": 2.3} ] } ],
  "constraints": [ { "type": "center_distance", "ok": 0, "description": "..." } ],
  "strings": ["QR123", "..."]
}
```

**Tier B**(`reason="result too large"`):仅 shot 判定 + 约束;超大 `strings` 先丢弃,
仍超限再丢弃 `constraints`。

```jsonc
{
  "truncated": true,
  "reason": "result too large",
  "status": "ok|ng",
  "overall": "PASS|NG",
  "logic": "AND",
  "shots": [ { "position": {...}, "image": "slot0", "ok": 1 } ]   // 仅 shot 判定
}
```

- 详细逐步骤值(pose/defect/string 全量)仍写入 SQLite `inspection_results.measurements`(JSON 文本,
  不受 2KB 限制),宿主可另开查询获得。
- 压缩手段:紧凑键名(`x/y/an/ms`、`ok` 用 0/1)、省略空字段、≤3 位小数。

---

## 8. 宿主示例

### 8.1 QtHost(C++,规范多图宿主)

`Master/QtHost/IpcClient`:

```cpp
// images[0..N-1] → 槽 0..N-1;recipeJson → params_data;cmd=RunRecipe
bool IpcClient::sendRunRecipe(const QVector<cv::Mat>& images,
                              const QString& recipeJson);
```

结果经 `IpcClient::resultReceived(QString)` 信号返回。

**配方模拟器（离线手动测整配方，无需相机/运控）**：`HostApp` 工具栏新增四个按钮 ——
`Load Recipe…`（载入配方 JSON）、`Shot Images…`（选目录，按文件名排序作为各 shot 的图）、
`Run Recipe`（**≤4 图** → `sendRunRecipe`，一次把图写槽 0..N-1）、
`Stream Run`（**任意张数** → 流式 `sendStartRecipe` → 逐片 `sendPushShot(k)` → `sendFinishRecipe`；
每片 ack 后再推下一片）。两种运行的结果框都显示完整语义 JSON，并**自动 `expandPlacement` 展开贴装阵列**
（机器坐标 x/y/an）。这样不改代码即可连通"宿主 → IPC → 插件 → 语义结果"全链路（`RunRecipe` 或流式）。

### 8.2 WpfHost(C#)

```csharp
// 多图:每个 shot 一张图 → 图像槽 0..N-1(≤ 4);单图可用单参重载。
var result = await _plugin.RunRecipeAsync(new[] { img0, img1, img2 }, recipeJson);
var overall = result.GetProperty("overall").GetString();
var shots   = result.GetProperty("shots").EnumerateArray();

// 流式(任意张数): StartRecipeAsync → PushShotAsync×N → FinishRecipeAsync
await _plugin.StartRecipeAsync(recipeJson);
for (int k = 0; k < imgs.Count; k++) await _plugin.PushShotAsync(imgs[k], k);
var r = await _plugin.FinishRecipeAsync();
```
`MainWindow` 内置**配方模拟器**：`Load Recipe…` / `Shot Images…` / `Stream Run` +
**`Send recipe once`**(配方常驻，首周期发、之后复用插件缓存)，结果框显示语义 JSON 并展开贴装阵列。

### 8.3 Python(plugin_host.py)

```python
# 多图:列表 → 图像槽 0..N-1(≤ 4);单图可直接传 np.ndarray。
ipc.send_run_recipe([img0, img1, img2], recipe_json)
result = ipc.wait_result(timeout_ms=15000)
print(result["overall"], result["shots"])

# 流式(任意张数):
ipc.send_start_recipe(recipe_json)
for k, img in enumerate(imgs): ipc.send_push_shot(img, k)
ipc.send_finish_recipe()
```
UI 内置**配方模拟器**：`Load Recipe…` / `Shot Images…` / `Stream Run` + **`Send recipe once`**，
结果框显示语义 JSON 并展开贴装阵列（`expand_placement`）。

## 8.4 贴装阵列展开（frame + pattern → 机器坐标位姿）

配方用 `build_frame` + `placement_pattern` 输出**坐标系 + 阵列描述**（§2.5）。宿主据此展开
每片芯片的机器坐标位姿：`machine(i) = O + R(θ)·local(i)`，`an = θ + local_angle(i)`。
结果里 `aggregates[]` 的 placement 项含 `name`（定位用）、`frame`、`pattern`、`count`，
片数 ≤16 时另有 `poses[]`（机器坐标，宿主可直接用，无需展开）。

三套宿主 SDK 均提供**展开助手**（规则与插件端完全一致）：

```cpp
// QtHost — Master/QtHost/PlacementExpand.h
#include "PlacementExpand.h"
QJsonObject result = QJsonDocument::fromJson(json).object();
QJsonObject place  = findPlacement(result, "place");     // 按 name（可空取第一个）
for (const PlacementPose& p : expandPlacement(place))
    stage.moveTo(p.x, p.y, p.an);                        // 单位 = frame.unit(mm)
```

```csharp
// WpfHost — Master/WpfHost/PlacementExpand.cs
var place = PlacementExpand.FindPlacement(result, "place");
foreach (var p in PlacementExpand.Expand(place.Value))
    Stage.MoveTo(p.X, p.Y, p.An);
```

```python
# plugin_host.py
place = find_placement(result, "place")        # 或 None 取第一个
for p in expand_placement(place):              # [{"x","y","an"}, ...]
    stage.move_to(p["x"], p["y"], p["an"])
```

> 单位随 `frame.unit`（有标定 = mm）。未标定场景 `unit="px"`，宿主需自行 /px_per_mm。

---

## 9. 风险与缓解

- **R1 2KB 上限**:紧凑键名 + 省略 + ≤3 位小数;超限**分级降级**(Tier A 保留每步/约束判定,Tier B 仅 shot 判定,见 §7);详细值落 SQLite。
- **R2 向后兼容**:旧宿主不认知 `RunRecipe` → 收到 `{"status":"error","reason":"unknown command"}`;
  `semantic` 为附加键,旧解析忽略;配方无 `shots` 时保持 v3。
- **R3 线程安全**:配方在 QtConcurrent 池运行,结果经 QFutureWatcher 回 GUI 线程;
  `ipcResultReady` 在 GUI 线程 emit;单全局 `m_detectionRunning` 守卫并发命令。
- **R4 图像槽上限**:`MAX_CAM_SLOTS=4` → IPC 内联至多 4 shot。引用越界槽(≥4)且无 `image` 路径的 shot 会**清晰报错**(不再静默回退到槽 0 的图);离线 `image` 路径不受限,大点位配方须用。
- **R5 deploy 精简构建**:新代码纯 Qt/OpenCV,不引 pylon/torch;WITH_TORCH=OFF 可构建。
- **R6 宿主偏移(已迁移,2026-08-21)**:WpfHost / plugin_host.py 原为 V1 字节偏移
  (命令头 144/2124、图像块 16 MB、图像头 32 B),与 V3 `CamSlot` 不一致,多图配方不可用。
  现已全部迁移到 **V3 布局**并与 QtHost 对齐,支持多图(图像槽 0..N-1)。偏移常量位于
  `Master/WpfHost/PluginIpc.cs`(`ShmConst`)与 `Master/plugin_host.py`(顶部常量块):
  - 命令块 20 KB = `MultiCamHeader(4096)` + `CamSlot[0..3](4096)`;`cam_count` 于偏移 16;
  - `CamSlot[0]` 绝对偏移:`cmd_id 4096 / cmd_type 4100 / cmd_status 4104`,
    `params_len 4160 / params_data 4164`, `result_len 6176 / result_data 6180`;
  - 心跳在 `MultiCamHeader`:`host 64` / `plugin 72`;
  - 图像块 128 MB,`ImageHeader` 64 B,图像槽 `k * 32 MB`(k=0..3);
  - 事件名带槽后缀:`Event_H2P/P2H_{shm}_0.._3`(单槽宿主用 `_0`)。
- **R7 陈旧 IPC 结果竞态(既有 bug)**:PushAndSearch/Search 已改为**本次检测完成时回包**
  (onDetectionFinished),不再读取上一次 `lastSuccess` 的陈旧结果。

---

## 10. 关键代码位置

| 功能 | 位置 |
|---|---|
| QtHost 宿主 | `Master/QtHost/IpcClient.{h,cpp}`(sendRunRecipe / sendStartRecipe / sendPushShot / sendFinishRecipe) |
| 配方模拟器(QtHost) | `Master/QtHost/HostWindow.{h,cpp}`(onLoadRecipe / onPickShotImages / onRunRecipe) |
| WpfHost 宿主 | `Master/WpfHost/PluginIpc.cs`(RunRecipeAsync / StartRecipeAsync / PushShotAsync / FinishRecipeAsync) |
| Python 宿主 | `Master/plugin_host.py`(send_run_recipe / send_start_recipe / send_push_shot / send_finish_recipe) |
| 阵列展开(QtHost) | `Master/QtHost/PlacementExpand.h`(findPlacement / expandPlacement) |
| 阵列展开(WpfHost) | `Master/WpfHost/PlacementExpand.cs`(FindPlacement / Expand) |
| 阵列展开(Python) | `Master/plugin_host.py`(find_placement / expand_placement) |
| 回归测试 | `tests/unit/test_recipe.cpp`(recipe_tests,含 RecipeStreaming.StagedShotsThenFinalize) |
