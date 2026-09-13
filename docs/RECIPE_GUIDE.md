# 工艺链配方与步骤间几何约束指南

| 项目 | 内容 |
|------|------|
| 文档版本 | 1.3 |
| 日期 | 2026-09-13 |
| 适用范围 | 配方(Recipe)v3/v4:完整工艺链 + 多点位(shot)+ 测量计算节点 + 步骤间判定逻辑 + 语义化结果 |

---

## 1. 概述

**工艺链配方(Process-Chain Recipe)** 是"工件检测程序"的载体,由三部分组成:

1. **有序步骤列表** `steps` — 每个步骤 = 一个算法 + 完整参数(含 ROI)。
2. **预处理管线** `preprocess` — 光照补偿 / 滤波 / FFT / 灰度 + 标定引用。
3. **步骤间判定逻辑** — 整体 PASS/NG 的组合方式(AND/OR)+ 几何约束。

**v4 扩展(2026-08-20)**:步骤可分组到多个**拍照点位(shot)**——每个 shot 消费**一张独立图像**
(直线电机/龙门移动由外部硬件处理,工具本身是纯软件)。配方可整体通过 IPC 下发给工具执行,
返回**语义化结果**(位姿 / 有无 / 缺陷 / 字符串)。详见 [RECIPE_IPC_PROTOCOL.md](RECIPE_IPC_PROTOCOL.md)。

配方保存为 JSON 文件,放在 `<可执行目录>/recipes/` 下,可换线/换工件复用。

## 2. 配方 JSON 结构(v3)

```jsonc
{
  "recipe_version": "3.0",
  "recipe_name": "电池端盖总检",
  "saved_at": "2026-08-20T10:30:00",
  "preprocess": { "light_method": 2, "filter_type": 1, "to_gray": true,
                  "calib_valid": true, "calib_px_per_mm": 12.34 },
  "logic": "AND",                       // "AND" | "OR"
  "steps": [
    { "algorithm": "circle_fit", "parameters": { "roi": {...}, "radius_range": {...}, ... } },
    { "algorithm": "line_fit",   "parameters": { ... } }
  ],
  "constraints": [
    { "type": "center_distance", "a": 0, "b": 1, "max_px": 20.0 },
    { "type": "angle_diff",      "a": 0, "b": 1, "target_deg": 90.0, "tol_deg": 1.0 }
  ]
}
```

## 2.1 多点位配方(v4)

v4 把步骤按**拍照点位(shot)**分组,每个 shot 消费一张图像;约束仍按**扁平步骤序**
(shot 0 的步骤、然后 shot 1 的步骤...)索引,`evaluateRecipeConstraints` 无需改动。

```jsonc
{
  "recipe_version": "4.1",
  "recipe_name": "三工位总检",
  "logic": "AND",
  "shots": [
    { "position": {"x": 0,   "y": 0},     // 机械坐标(信息性)
      "image_slot": 0,                    // IPC 模式:图像槽下标(0..3)
      "image": "shots/p1.png",            // 离线模式:文件路径(空 => 当前/推送图)
      "steps": [
        { "algorithm": "corner_detect", "parameters": { ... }, "emit": "pts_a" },
        { "algorithm": "golden_template", "parameters": { "template_path": "..." } }
      ] },
    { "position": {"x": 120, "y": 0}, "image_slot": 1,
      "steps": [ { "algorithm": "corner_detect", "parameters": { ... }, "emit": "pts_b" } ] },
    { "position": {"x": 240, "y": 0}, "image_slot": 2,
      "steps": [ { "algorithm": "line_fit", "parameters": { ... }, "emit": "edge_c" } ] }
  ],
  "aggregates": [
    { "algorithm": "fit_rect_from_points", "points": ["pts_a", "pts_b", "edge_c"] }
  ],
  "constraints": [ { "type": "center_distance", "a": 3, "b": 0, "max_px": 5 } ]
}
```

- **shot → 图像**:IPC 整配方模式按 `image_slot` 取图像槽;离线/UI 模式按 `image` 路径
  `imread`(灰度),`image` 为空则用当前推送图/当前画面。
- **`image` 路径 = 示教/离线专用**:`shots[k].image` 存**示教图路径**(可放该配方的示教目录,
  便于离线回放/复现)。**实际生产经 IPC 运行时忽略该路径**,改用宿主推送的图(整配方按 shot
  顺序取槽;流式由 `PushShot` 逐片传入)——故换机部署不需要目标机存在示教图。
- **shot → 约束下标**:上例扁平步骤序 = [shot0-corner(0), shot0-golden(1), shot1-corner(2),
  shot2-line(3), 聚合拟合(4)];`center_distance a=4 b=0` 引用"聚合拟合矩形中心 ↔ 角点 A"。
  注意 OCR 等非定位步骤(cx=cy=0)被几何约束引用会判 NG(防呆)。跨 shot 几何约束应以定位步骤为参照。
- **整体判定**:`整体 = (shots 按 logic AND/OR 组合) AND (所有约束通过)`。
  shot 判定 = 该 shot 内全部步骤 AND(该 shot 内任一步骤 NG 则 shot NG)。
- **跨视野坐标统一**:每个 shot 的像是**局部像素坐标**;跨 shot 的几何量(距离/角/拟合)
  必须先把各 shot 的点变换到统一(机器)坐标系,见 **§2.4**(shot `transform` 或配方级
  `"coordinate_frame":"machine"`)。不配置时各 shot 视为同帧(行为不变)。
- **逐点位光源**:shot 可带 `"light": {"program":N, "intensity":V, ...}`(宿主控光的采集参数,
  自由字段)。随配方保存/加载,并在 IPC 结果里回显(§5),宿主据此/日志关联每个点位用的光。
  示教时宿主在 `PushAndSearch` 的 params 里带上 `light`,插件自动填入该 shot(见 §6.2)。
- **向后兼容**:无 `shots` 数组的 v1/v2/v3 配方视作**单 shot**(用当前/推送图),行为不变;
  无 `aggregates` 数组时跳过聚合拟合。

## 2.2 跨点位聚合拟合(v4.1,缺口 A/B)

**场景**:一个相机在多个点位拍照,最后用**所有点位收集到的坐标点**拟合 直线/矩形/圆
(如"两对角 + 一边"确定超出视野的大矩形中心位姿)。

**机制(两步)**:

1. **发布点 / 发布线**(缺口 A):步骤 JSON 可带 `"emit":"标签"`。该步骤执行后,其 `points`
   点数组(`measurements["points"]`)被并入点池;**line_fit 步骤同时把拟合直线
   (line_x1..y2)发布进线池**(同标签均**累加**,适合同一算法在多个 shot 重复)。
   以下算法已发布 `points`:

   | 算法 | `points` 内容 | 线池 |
   |---|---|---|
   | `blob_analysis` | 所有 blob 质心 | — |
   | `corner_detect` | 所有角点位置 | — |
   | `line_fit` | 拟合线两端点 | ✓ 直线方程 |
   | `caliper` | 所有有效边缘点 | — |

2. **聚合拟合**(缺口 B):配方根级 `aggregates` 数组,在**所有 shot 跑完后**执行,按
   `points`(点池)或 `lines`(线池)标签列表(顺序)取数拼接,拟合后输出到自己的
   `measurements`(cx/cy/angle/width/height/radius/shape_type),并**追加进扁平步骤序**
   → 语义结果里有它的位姿与 shape,约束可按索引引用,落库按步骤行记录。

| `aggregates[].algorithm` | 拟合 | 取数 | 输出 |
|---|---|---|---|
| `fit_rect_from_points` | 点集的最小面积(可旋转)外接矩形 | `points` | cx, cy, width, height, angle[0,90), shape_type="rect" |
| `fit_rect_from_lines` | **4 边定矩形**:4 条边线两两求交得 4 角点 | `lines` | cx, cy, width, height, angle[0,90), corners(4 角点), shape_type="rect" |
| `fit_line_from_points` | 点集最小二乘直线(PCA 方向) | `points` | cx, cy, angle[0,180), line_x1..y2, shape_type="line" |
| `fit_circle_from_points` | 点集代数最小二乘圆(Kasa) | `points` | cx, cy, radius, **diameter**, **rms_error_px/max_error_px**, shape_type="circle" |

> 拟合类节点还输出**拟合质量**(`fit_line_from_points` 的 `rms_error_px`/`max_error_px` = 直线度;
> `fit_circle_from_points` 的 `diameter` 与 `rms_error_px`/`max_error_px` = 圆度)。
> 除拟合外,`aggregates` 还支持**测量计算节点**(距离/角度/交点/弧/统计/位置度),详见 §2.3。

**4 边定矩形(`fit_rect_from_lines`)**:对 4 条 `line_fit` 步骤的直线方程,先按方向角
把 4 条线配成两对近平行边,再用单位法向把每对边法向对齐(点积对齐,避免反平行),两两求交
得到精确的 4 个角点;中心 = 角点质心,宽/高 = 两对平行边的垂直距离,angle ∈ [0,90)。
角点同时写入 `measurements["corners"]`。任意两条边平行或点数不足(<4 条有效直线)→ NG。

> 点数不足 / 共线退化时该聚合步骤判 NG(如 2 点不能拟合矩形/圆)。聚合步骤不绑定任何
> 单一图像,`shotIndex = -1`。纯函数、在 shot 循环之后同一任务线程内执行 → 线程安全。

## 2.3 测量计算节点（距离 / 角度 / 交点 / 弧 / 统计 / 位置度）

除拟合类（`fit_*`）外，`aggregates` 还支持一批**测量计算节点**：它们不产出新形状，
而是把已有点/线/圆**换算成标量测量值或派生几何**（距离、夹角、交点、弧长、统计量、
位置度），供后续节点继续串联或被约束判定。

**数据源约定**（节点按算法从对应键取数）：

| 键 | 内容 | 可引用对象 |
|---|---|---|
| `points` | 点源 | 步骤 `emit` 标签（点池，取该标签下所有点的**质心**）；或命名输出的中心 `cx,cy` |
| `lines` | 线源 | 线池 `emit` 标签（`line_fit` 发布）；或命名输出的 `line_x1..y2` 端点 |
| `circles` | 圆源 | 命名输出（`fit_circle_from_points` 的 `cx,cy,radius`） |
| `names` | 命名输出集合 | 一组命名结果（配合 `field` / `stat`，用于 `aggregate_stat`） |

> **点源取的是"质心"而非"端点"**：一个标签若只发布 1 个点（`circle_fit` 的圆心、
> 单 blob 的质心、`intersect_lines` 的交点），点到点距离就是这两个点的真实距离；
> 若标签发布了**多个点**（如 `line_fit` 的 2 个端点、`blob_analysis` 的多个质心、
> `caliper` 的一串边缘点），则取这些点的**平均位置（质心）**——例如用 `line_fit` 的
> emit 标签做 `distance_points`，得到的是两条线的**中点距**，不是端点距。需要端点/边
> 之间的距离请改用 `distance_lines`（线到线）或直接引用命名线的 `line_x1..y2`。

**串联（计算 DAG）**：任何带 `"name"` 的聚合，其输出不仅登记进命名结果表，**中心点会
重新发布进点池、线端点发布进线池**（同一 `name` 键）。因此下游节点可把上游节点的 `name`
当作点/线源引用，例如 `intersect_lines → name:"corner"`，再用
`distance_points points:["corner", "holeA"]`。节点按 `aggregates` 数组顺序执行。

**单位**：配方带有效标定（`px_per_mm > 0`）时，距离/弧/位置度节点额外输出 `*_mm` 字段；
否则仅有像素值。

### 节点总表

| `algorithm` | 输入键 | 主要输出 | 适用场景 |
|---|---|---|---|
| `distance_points` | `points`(2) | distance_px, distance_mm, cx, cy(中点) | 孔心距、两点间距、特征到特征 |
| `distance_lines` | `lines`(2) | distance_px, distance_mm | 平行边间距、槽宽、导轨间距、板厚（两平行面） |
| `distance_circles` | `circles`(2) | **distance_px = 圆心距**（=center_distance_px）, **gap_px = 净距**（圆心距−r1−r2, 可负）, *_mm | 孔心距/孔位；净距·壁厚·干涉（`gap_px < 0` = 两圆重叠） |
| `distance_point_line` | `points`(1) + `lines`(1) | distance_px, distance_mm | 特征到基准边的距离、轮廓到基准的对中 |
| `intersect_lines` | `lines`(2) | cx, cy（交点）, shape_type="point" | 两边缘虚拟角点/尖点；为后续拟合补点 |
| `angle_three_points` | `points`(3，**第一个为顶点**) | angle ∈ [0,180] | 拐角夹角、卡尺三边夹角、三点定义的张角 |
| `directed_angle_between_lines` | `lines`(2) | angle ∈ [0,360), signed_angle ∈ (-180,180] | 有向旋转角、平行四边形/反射角判定（保留方向，区别于折叠到锐角的 `angle_between_lines`） |
| `concentricity` | `circles`(2) | dx, dy, distance_px(圆心偏移), eccentricity | 同心度、偏心量、内外圈对中 |
| `arc_from_three_points` | `points`(3，start, mid, end) | cx, cy, radius, diameter, central_angle, arc_length_px, chord_length_px | 圆弧半径/弧长/弦长/圆心角（倒角、圆角、外缘圆弧） |
| `aggregate_stat` | `names` + `field` + `stat` | value, min, max, mean, stddev, count | 跨点位/多测量的统计：均值直径、最大偏差、一致性(stddev/range) |
| `true_position` | `points`(1) + `nominal_x/nominal_y` | dx, dy, lateral_px, deviation_px | GD&T 位置度（直径式 2·√(dx²+dy²)） |
| `build_frame` | `lines`(2) + `x_axis`/`handedness` | frame_ox/oy, frame_theta_deg, x_axis/y_axis, perp_deg, shape_type="frame" | 两(近)垂直线建坐标系（原点=交点，X轴=指定线方向），供阵列/贴装引用 |
| `placement_pattern` | `frame`(名) + `mode`(single/line/grid/list) + 阵列参数 | count, poses[]（机器坐标 x/y/an）, pattern 描述, shape_type="placement" | 在坐标系上生成**贴装位姿阵列**（单点/一线/方阵/逐片转角），mm |

### 拟合类节点的附加测量字段

| 节点 | 新增字段 | 含义 |
|---|---|---|
| `fit_line_from_points` | `rms_error_px`, `max_error_px` | **直线度**：源点到拟合线的 RMS / 最大垂直偏差 |
| `fit_circle_from_points` | `diameter`, `rms_error_px`, `max_error_px` | 直径；**圆度**：源点到拟合圆的径向偏差 |

### 判定这些测量值

标量测量用 `measure_range` 约束判定（见 §4）：`{ "type": "measure_range", "a": <步骤下标>, "field": "distance_px", "min": …, "max": … }`。
`a` 可指向聚合节点在**扁平步骤序**里的下标（shot 步骤之后依次排列）。

**串联示例**（两边缘求交 → 角点到孔心距离 → 判定 ≤ 公差）：

```jsonc
{
  "shots": [
    { "image_slot": 0, "steps": [
        { "algorithm": "line_fit", "parameters": { ... }, "emit": "e1" } ] },
    { "image_slot": 1, "steps": [
        { "algorithm": "line_fit", "parameters": { ... }, "emit": "e2" } ] },
    { "image_slot": 2, "steps": [
        { "algorithm": "circle_fit", "parameters": { ... }, "emit": "hole" } ] }
  ],
  "aggregates": [
    { "algorithm": "fit_line_from_points", "points": ["e1"], "name": "edge1" },
    { "algorithm": "fit_line_from_points", "points": ["e2"], "name": "edge2" },
    { "algorithm": "intersect_lines", "lines": ["edge1", "edge2"], "name": "corner" },
    { "algorithm": "distance_points", "points": ["corner", "hole"], "name": "corner_to_hole" }
  ],
  "constraints": [
    // 扁平序: e1(0) e2(1) hole(2) edge1(3) edge2(4) corner(5) corner_to_hole(6)
    { "type": "measure_range", "a": 6, "field": "distance_px", "min": 0, "max": 50 }
  ]
}
```

> 标量节点（距离/角度/交点/位置度）在语义结果 JSON 里以 `shape.type` =
> `distance`/`angle`/`point`/`position` 呈现，标量值放在 `shape.v`（见 §6.1）。

## 2.4 跨视野拍照与坐标统一

**问题**：多 shot 时每个 shot 是一张**独立图像**（相机/工件移动，FOV 不同）。步骤产出的
像点是**该图局部坐标**；直接把不同 shot 的点放进同一个池做距离/角/拟合，数值会差一个
位移（或旋转），**没有物理意义**。

**解决**：给每个 shot 声明一个"局部像点 → 统一(机器)坐标系"的二维变换，`finalizeRecipeShots`
在把点/线发布进池子时应用它，再让聚合节点在统一坐标系里计算。

### 配置（两种）

1. **整配方开关**（最省事）：配方根级

   ```jsonc
   { "coordinate_frame": "machine", ... }
   ```

   每个带 `position` 的 shot 自动按 `position(mm) × px_per_mm` 平移（需标定）。

2. **shot 级 `transform`**（精确控制，可覆盖）：

   ```jsonc
   "shots": [
     { "position": {"x": 0,   "y": 0}, "steps": [ ... ] },
     { "position": {"x": 120, "y": 0},
       "transform": { "mode": "from_position" },   // position(mm)×px_per_mm 平移
       "steps": [ ... ] }
   ]
   ```

| `transform.mode` | 参数 | 含义 |
|---|---|---|
| `none`（或缺省） | — | 恒等（各 shot 同帧，行为不变） |
| `from_position` | `angle_deg`(可选) | 用本 shot 的 `position`(mm) × `px_per_mm` 平移；`angle_deg` = **hand-eye 相机↔机器轴夹角**（先旋转再平移） |
| `translate` | `dx, dy` | 像素平移 |
| `rigid` | `angle_deg, dx, dy` | 旋转(角度)+平移（相机安装角/旋转台） |
| `affine` | `a,b,c,d,e,f` | 通用 2×3 仿射 `x'=ax+by+c; y'=dx+ey+f` |

### 生效范围

- **只影响聚合层**：点池、线池、命名结果（供 `fit_*` / 距离 / 角 / 交点 / 弧 / 统计 / 位置度节点使用）。
- **约束** `constraints[]` 引用的是**原始步骤测量**（仍是各图局部坐标）。跨视野判定请引用
  **聚合节点的输出**（其下标在扁平步骤序里），或直接对聚合输出的标量用 `measure_range`。
- **旋转**：平移不改变线方向角；若相机/工件有旋转（机器坐标系与图像坐标系有夹角），用
  `rigid`/`affine` — 变换会把线端点一起旋转，拟合出的线方向角即为**相对机器坐标系**的角。
- **需标定**：`from_position` 与 `*_mm` 输出都依赖 `px_per_mm`；未标定时 `from_position` 退化为恒等。
- **hand-eye 旋转**：相机轴与机器轴有夹角时，配置配方根级

  ```jsonc
  { "coordinate_frame": "machine",
    "calib": { "px_per_mm": 12.34, "cam_to_machine_deg": 0.8 }, ... }
  ```

  `cam_to_machine_deg` 会注入每个 `from_position` shot（也可在 shot `transform.angle_deg` 单独覆盖）。
  变换为 **先旋转(φ)再平移**，使统一坐标系与机器轴对齐 → 拟合出的线方向角即**机器坐标系角度**。

### 另外：带 `emit` 的步骤可直接按标签引用

发布点/线时，带 `emit` 标签的步骤也会把自身 `measurements`（`cx/cy/radius/line_x1..y2/angle`，
已按上述变换统一）登记进**命名结果表**（同名 first-wins）。因此像 `distance_circles` /
`concentricity` 这类需要"命名圆"的节点，可直接引用 **`circle_fit` 步骤的 emit 标签**，
无需先做 `fit_circle_from_points` 聚合。

### 示例：跨视野拍两个圆，求圆心距（mm）

```jsonc
{
  "coordinate_frame": "machine",
  "shots": [
    { "position": {"x": 0,   "y": 0}, "steps": [
        { "algorithm": "circle_fit", "parameters": { ... }, "emit": "cA" } ] },
    { "position": {"x": 120, "y": 0}, "steps": [
        { "algorithm": "circle_fit", "parameters": { ... }, "emit": "cB" } ] }
  ],
  "aggregates": [
    { "algorithm": "distance_points",  "points":  ["cA", "cB"], "name": "hole_dist" },
    { "algorithm": "distance_circles", "circles": ["cA", "cB"], "name": "hole_gap" }
  ],
  "constraints": [
    // 扁平序末尾: hole_dist(=2) → 圆心距 ∈ [118, 122] mm（标定后 mm 判定）
    { "type": "measure_range", "a": 2, "field": "distance_mm", "min": 118, "max": 122 }
  ]
}
```

- `hole_dist.distance_mm` = 两圆心在机器坐标系下的距离（mm）。
- `hole_gap.gap_px` = 净距（圆心距 − rA − rB；负=重叠）。
- 求**直线相对机器坐标系的角**：把每张图的点按 `rigid`（含机器-图像夹角）变换后，
  用 `fit_line_from_points` 拟合，其 `angle` 即为机器坐标系的线方向角。

## 2.5 跨视野建坐标系 + 阵列贴装（贴装位姿输出）

**场景**：大基板边缘超出单视野 → 跨视野拍左边缘（2 段 `line_fit` 聚合成一条线）、
上边缘（同理）→ 两条(近)垂直线建立**基板坐标系** → 在其上生成**贴装位姿阵列**（单芯片 /
一排 / 方阵 / 逐片转角），全部输出到**机器坐标系(mm)**，供宿主驱动贴装头。

```jsonc
{
  "recipe_version": "4.1",
  "coordinate_frame": "machine",               // 跨视野点统一到机器坐标系(见 §2.4)
  "shots": [
    { "position": {"x":0,   "y":0},   "steps":[{"algorithm":"line_fit","parameters":{...},"emit":"L1"}] },
    { "position": {"x":0,   "y":200}, "steps":[{"algorithm":"line_fit","parameters":{...},"emit":"L2"}] },
    { "position": {"x":0,   "y":0},   "steps":[{"algorithm":"line_fit","parameters":{...},"emit":"T1"}] },
    { "position": {"x":200, "y":0},   "steps":[{"algorithm":"line_fit","parameters":{...},"emit":"T2"}] }
  ],
  "aggregates": [
    { "algorithm":"fit_line_from_points", "points":["L1","L2"], "name":"left" },
    { "algorithm":"fit_line_from_points", "points":["T1","T2"], "name":"top"  },
    // 坐标系：原点=两线交点；X 轴=top（x_axis 指定，缺省=第一条）；Y 轴按手性正交化
    { "algorithm":"build_frame", "lines":["top","left"], "x_axis":"top",
      "handedness":"right", "name":"substrate" },
    // 贴装阵列：在 substrate 坐标系上生成位姿（mm）
    { "algorithm":"placement_pattern", "frame":"substrate", "name":"place",
      "mode":"grid", "origin":{"x":10.0,"y":8.0}, "angle":0.0,
      "rows":3, "cols":8, "pitch_x":12.0, "pitch_y":10.0, "dtheta":5.0, "snake":false }
  ],
  "constraints": [
    { "type":"angle_diff", "a":0, "b":1, "target_deg":90.0, "tol_deg":0.5 }   // 两边缘垂直度
  ]
}
```

- **`build_frame`**：`lines` 两条线（命名输出或 emit 线标签）；`x_axis` 指定哪条作 +X（缺省第一条）；
  `handedness` = `right`(默认，Y=rot90ccw(X)) / `left`。X 的符号用 Y 轴线的方向**自动消歧**。
  输出 `frame{ox,oy,theta_deg,x_axis,y_axis,perp_deg,unit}`（mm 若已标定，否则 px）。
  两线平行 → NG。
- **`placement_pattern`**：`frame` 引用命名坐标系；`mode`：
  - `single`：`origin{x,y}`+`angle`（1 枚）；
  - `line`：`count`+`pitch`+`direction_deg`（相对 +X）；
  - `grid`：`rows`×`cols`+`pitch_x`/`pitch_y`+`snake`；
  - `list`：显式 `points:[{x,y,an},...]`（不规则）。
  逐片角度 = `angle + 第idx片 × dtheta`；坐标/节距单位与 frame 一致（mm 优先）。
  输出 `count`、`poses[]`（**机器坐标** `{x,y,an}`）、`pattern`（紧凑描述）。
- **点示教反解**：`"origin_in":"machine"` → `origin` 按**机器坐标**(mm) 解释，节点内部反解
  `local = R(-θ)·(origin − O)`。示教时把贴装头移到第 1 片应贴的位置、读出机器坐标填进去即可；
  输出 `pattern.origin` 一律存**局部坐标**（已反解），宿主展开无歧义。
- **2KB 兜底**：`poses[]` 仅当 **片数 ≤ 16** 时枚举；更多片只给 `pattern` 描述，宿主按
  `machine(i) = O + R(θ)·local(i)` 自行展开（避免超限被 `truncated`）。

**语义结果 JSON**（回包给宿主）：

```jsonc
{"algorithm":"build_frame","ok":1,
 "frame":{"ox":..,"oy":..,"theta_deg":..,"x_axis":[..],"y_axis":[..],"perp_deg":..,"unit":"mm"}},
{"algorithm":"placement_pattern","ok":1,"count":24,
 "frame":{"ox":..,"oy":..,"theta_deg":..,"unit":"mm"},
 "pattern":{"mode":"grid","rows":3,"cols":8,"pitch_x":12,"pitch_y":10,"origin":{"x":10,"y":8},
            "angle":0,"dtheta":5,"snake":false,"unit":"mm"},
 "poses":[{"x":..,"y":..,"an":..}, ...]}          // 片数 ≤16 时列出
```

> 宿主侧：`for i in 0..count-1: machine_pose(i) = Frame ∘ local(i)`，`local(i)` 由 `pattern`
> 展开（含第 i 片 `dtheta`）。基板换位后 `frame` 自动重算，整列贴装位姿随坐标系整体跟随。
> **三套宿主 SDK 均带展开助手**：QtHost `PlacementExpand.h`（`expandPlacement`/`findPlacement`）、
> WpfHost `PlacementExpand.cs`（`Expand`/`FindPlacement`）、Python `expand_placement`/`find_placement`；
> 结果里每个聚合含 `name` 便于按名定位。详见 [RECIPE_IPC_PROTOCOL.md](RECIPE_IPC_PROTOCOL.md) §8.4。

## 3. 支持的步骤算法

| 算法标识 | 说明 | 产出测量值 |
|---|---|---|
| `circle_fit` | 圆拟合 | cx, cy, radius |
| `line_fit` | 直线拟合 | cx, cy, **angle**(直线方向角, 0–180°), **line_x1, line_y1, line_x2, line_y2**(拟合线两端点) |
| `ellipse_fit` | 椭圆拟合 | cx, cy, radius(=a), width, height, angle |
| `rectangle_fit` | 矩形拟合 | cx, cy, width, height, angle |
| `template_match` | 模板匹配 | cx, cy, angle, match_score |
| `blob_analysis` | Blob 分析 | blob_count, blob_total_area, blob_first_area |
| `golden_template` | 黄金模板差分 | defect_count, defect_total_area |
| `scratch_detect` | 划痕检测 | defect_count, defect_total_area |
| `crack_detect` | 裂纹检测 | crack_count, crack_coverage |
| `ocr` | OCR 文字识别 | ocr_text, ocr_length |
| `barcode` | 条码 / QR | barcode_count, barcode_texts(数组), barcode_first_text |
| `data_matrix` | DataMatrix 码 | dm_count, dm_texts(数组), dm_first_text |

> 每个步骤执行后,其测量值写入步骤结果(`measurements`),供步骤间约束引用。
> `line_fit` 额外产出 `line_x1/line_y1/line_x2/line_y2` 拟合线端点,供 `line_distance` 约束引用。
> `golden_template` 步骤的模板图像路径以 `parameters.template_path` 存入配方;运行配方时按该路径加载,路径不存在则步骤判 NG。

### 3.1 Blob 分析:应用场景与调参策略

**Blob(连通域)分析**把图像里"连成一片的同色区域"逐个找出来,再按大小/形状/数量过滤。它输出
`blob_count` / `blob_total_area` / `blob_first_area`,并把**每个 blob 的质心**发布进点池
(见 §2.2),供聚合拟合与几何约束引用。适合"数块、找点、量面积"类场景;细长划痕/裂纹请走
`scratch_detect` / `crack_detect`。

#### 应用场景

| 场景 | 做法 | 判定手段 |
|---|---|---|
| **表面缺陷(污点/脏污/异物/孔洞)** | 暗前景找黑点,按面积过滤 | 面积超阈值 → NG |
| **数量核对(焊球/药片/颗粒/铆钉)** | 数连通域个数 | `min_count` / `max_count`(两边同值 = 必须正好 N 个) |
| **定位(目标质心)** | blob 质心发布为 `points` | 供聚合拟合 / `center_distance` / `equidistant` 引用 |
| **面积/尺寸判定** | 汇总面积或首个 blob 面积 | `blob_total_area` / `blob_first_area` 参与约束 |

典型配方片段(焊球缺失检测——必须正好 4 个圆球):

```jsonc
{ "algorithm": "blob_analysis", "parameters": {
    "roi": {"x": 20, "y": 30, "width": 200, "height": 150},
    "thresh_mode": "otsu", "dark_foreground": true,
    "min_area": 30, "max_area": 2000,
    "min_circularity": 0.6, "max_circularity": 1,
    "min_count": 4, "max_count": 4 } }
```

#### 调参策略(按处理流程分 4 组)

**① 二值化(决定哪些像素算前景)**

- 光照均匀 → `thresh_mode=otsu`(直方图自动找阈值);光照不均(打光渐变)→
  `adaptive_mean` / `adaptive_gauss`。
- `adaptive_block`(默认 31,一般 15–51):块越大越"局部",过大吞小缺陷,过小噪声多。
- `adaptive_c`(默认 5):从局部均值减去的常数,**越大越"挑"、前景越少**。
- `dark_foreground`:`true` = 找暗缺陷(亮背景黑点),`false` = 找亮目标。**极性选反会一个块都找不到**。

| 参数 | 默认 | 范围 |
|---|---|---|
| `thresh_mode` | otsu | otsu / manual / adaptive_mean / adaptive_gauss |
| `thresh_value` | 128 | 0–255,仅 manual 生效 |
| `adaptive_block` | 31 | 3–201(奇数) |
| `adaptive_c` | 5 | -50~50 |
| `dark_foreground` | false | 开 / 关 |

**② 连通域(怎么聚块)**

- `connectivity`:`8`(默认)= 对角也算一个(合并);`4` = 只认上下左右正交邻接
  (对角接触拆成两个)。想拆开对角粘连的目标用 `4`。

**③ 特征过滤(只留要的块)**

- 面积管"大小":抬 `min_area` 去噪点,压 `max_area` 去背景大块。
- 圆度 = 4π·面积/周长²,**圆=1,越不规则越小**;留圆目标设 `min_circularity` 0.6–0.8。
- 长宽比 = 最小外接椭圆 长轴/短轴,1≈圆/方,越大越细长;`-1` = 不参与过滤。

| 参数 | 默认 | 说明 |
|---|---|---|
| `min_area` / `max_area` | 10 / 1e9 | 像素面积范围 |
| `min_circularity` / `max_circularity` | -1 / 1 | 圆度下限 / 上限,-1 = 不限 |
| `min_aspect_ratio` / `max_aspect_ratio` | -1 / -1 | 长宽比下限 / 上限,-1 = 不限 |

**④ 数量判定(拍板 NG)**

- `min_count` / `max_count`,默认 `0` = **不判定**。两边设同值(=4)即"必须正好 4 个";
  只设一边即单边判。这是 blob 步骤在工艺链里 PASS/NG 的**最终门槛**。

#### 调试顺序建议

1. 先画 ROI(只处理关心区域,噪声大减)。
2. 选对 `dark_foreground` 极性,预览里能看到目标块。
3. 光照均匀用 `otsu`;不均切 `adaptive_mean`;结果脏调 `adaptive_c`。
4. 抬 `min_area` 去掉多余块,降 `min_area` 找回缺失块。
5. 需按形状筛 → 圆度(留圆)或长宽比(留 / 滤细长)。
6. 最后设 `min_count` / `max_count` 做成 NG 门槛。

## 4. 步骤间约束类型

约束在**所有步骤执行完毕后**统一评估。若某约束引用的步骤为 NG,该约束判为 NG(无法校验)。

| 类型 | 判定规则 | 配置字段 | 典型用途 |
|---|---|---|---|
| `center_distance` | `距离(A中心, B中心) ≤ max_px` 或 `≤ max_mm`(需标定) | `a, b, max_px` 或 `use_mm, max_mm` | 同心度、孔位位置度 |
| `radius_ratio` | `radius(A) / radius(B) ∈ [min, max]` | `a, b, min, max` | 壁厚、轴孔配合 |
| `size_ratio` | `field(A) / field(B) ∈ [min, max]` | `a, b, field(radius/width/height), min, max` | 内外尺寸比 |
| `angle_diff` | `\|angle(A) − angle(B)\| (mod 180, →[0,90]) ≈ target_deg ± tol_deg` | `a, b, target_deg(0=平行, 90=垂直), tol_deg` | 垂直度、平行度、旋转 |
| `equidistant` | `max\|d(k) − mean(d)\| ≤ max_px`(d(k)=相邻步骤中心距离, 需 ≥3 步骤) | `steps`(有序下标数组), `max_px` 或 `use_mm, max_mm` | 多孔等距、引脚间距一致性 |
| `line_distance` | 步骤 A 中心到参考线的垂直距离 ≤ `max_px` | `a, line_step`(取该步骤 line_x1..y2)或显式 `line_x1..line_y2`, `max_px` 或 `use_mm, max_mm` | 特征到基准边距离、对中 |
| `symmetry` | `\|镜像(A, 轴) − B\| ≤ max_px` | `a, b, axis_x1..axis_y2`(镜像轴), `max_px` 或 `use_mm, max_mm` | 对称度、左右对中 |
| `measure_range` | 步骤 A 的测量字段 `field ∈ [min, max]` | `a, field, min, max`(可选 `use_mm`) | 任意标量测量判定:距离/直径/角度/弧长/位置度/统计量 |

> `center_distance` / `equidistant` / `line_distance` / `symmetry` 均支持**以 px 或 mm 判定**:选择"毫米 mm"时,程序用当前相机标定 `px_per_mm` 把像素距离换算为 mm 后与 `max_mm` 比较;若未标定则判定为 NG(提示缺标定)。
>
> `measure_range` 直接读步骤（含聚合节点）的 `measurements[field]` 与 `[min, max]` 比较,
> 是判定**计算节点**产出的标量(如 `distance_px`、`diameter`、`angle`、`arc_length_px`、
> `deviation_px`、`value`)的通用手段。选 mm 时按 `px_per_mm` 把 `*_px` 字段换算为 mm 后
> 比较;未标定同样判 NG。步骤缺该字段时判 NG(而不是假 PASS)。

**等距约束 JSON 示例**:

```jsonc
{ "type": "equidistant", "steps": [0, 1, 2, 3], "max_px": 0.5 }
```

**参考线距离约束 JSON 示例**(显式参考线):

```jsonc
{
  "type": "line_distance",
  "a": 2,                      // 被测点步骤
  "line_step": -1,             // -1 = 用显式参考线; ≥0 = 引用该步骤的 line_x1..y2
  "line_x1": 10, "line_y1": 5, "line_x2": 10, "line_y2": 95,
  "max_px": 0.2
}
```

**对称度约束 JSON 示例**:

```jsonc
{
  "type": "symmetry", "a": 0, "b": 1,
  "axis_x1": 0, "axis_y1": 0, "axis_x2": 0, "axis_y2": 100,
  "max_px": 0.3
}
```

> **防呆**:引用步骤必须携带中心坐标(cx/cy)。OCR、条码、DataMatrix 等非定位步骤中心为 (0,0),被几何约束引用时判 NG("步骤缺少中心坐标"),不会产生假 PASS。

**整体判定**:`整体 = (步骤按 AND/OR 组合) AND (所有约束通过)`。

## 5. 实际场景

### 5.1 位置关系类(中心距)

| 场景 | 步骤 | 约束 | 检出缺陷 | 约束类型 |
|---|---|---|---|---|
| **轴承内外圈同心度** | ①外圈圆 ②内圈圆 | 两圆心距 ≤ 0.05mm | 滚道偏心、错位 | `center_distance` |
| **法兰/壳体孔位位置度** | ①定位基准孔 ②被测孔 | 孔心到基准中心距离 ≤ 公差 | 孔位偏移、钻孔超差 | `center_distance` |
| **多孔间距一致性** | ①..N 逐个孔 | 相邻孔间距在标称 ± 公差 | 引脚弯曲、孔距不均 | `center_distance`(序列) |
| **同轴安装(多段轴颈)** | ①轴段A外圆 ②轴段B外圆 | 两段圆心距 ≤ 同轴度 | 轴弯曲、台阶错位 | `center_distance` |
| **印刷/贴装套准** | ①主色定位块 ②次色定位块 | 两中心距 ≤ 套准公差 | 套印偏移、贴装错位 | `center_distance` |

### 5.2 尺寸/比例类(半径比、尺寸比)

| 场景 | 步骤 | 约束 | 检出缺陷 | 约束类型 |
|---|---|---|---|---|
| **环形垫片壁厚均匀性** | ①外圆 ②内圆 | R外/R内 ∈ 壁厚范围 | 壁厚不均、偏心 | `radius_ratio` |
| **轴-孔配合间隙** | ①轴圆 ②孔圆 | 轴/孔半径比 ∈ 配合区间 | 过盈过大、间隙超差 | `radius_ratio` |
| **内外矩形尺寸比(壳体留边)** | ①外轮廓矩形 ②内腔矩形 | 内宽/外宽、内高/外高 ∈ 范围 | 壁厚/留边不足 | `size_ratio`(field=width/height) |

### 5.3 角度/方向类(角度差)

| 场景 | 步骤 | 约束 | 检出缺陷 | 约束类型 |
|---|---|---|---|---|
| **外壳端面垂直度** | ①边线A ②边线B | 两线夹角 ∈ 90° ± tol | 端面不垂直 | `angle_diff`(target=90) |
| **导轨/双刃平行度** | ①边线A ②边线B | 两线夹角 ≤ tol(≈0°) | 导轨不平行、刃口偏斜 | `angle_diff`(target=0) |
| **元件贴装旋转** | ①基板基准线 ②元件边线 | 夹角 ∈ 0° ± tol | 元件旋转 | `angle_diff`(target=0) |

### 5.4 组合示例

**电机端盖总检**:① 外圆(基准)→ ② 内圆 → ③ 端面直线。
- 约束 A:`center_distance` ①↔②(内外圈同心)
- 约束 B:`angle_diff` ②↔③(内圆轴与端面垂直)
- 判定:`AND`(三步骤全过 + 两约束全过 → PASS)

### 5.5 测量计算节点类(距离 / 角度 / 弧 / 交点 / 统计 / 位置度)

下表每个场景都用 §2.3 的计算节点产出标量,再用 `measure_range`(或已有点/线约束)判定。

| 场景 | 步骤 (emit) | 计算节点 | 判定 | 检出缺陷 |
|---|---|---|---|---|
| **孔心距/孔位间距** | ①孔A ②孔B (圆拟合) | `distance_points` [A,B] | `measure_range` distance_px ∈ 标称±公差 | 孔距超差、钻孔偏移 |
| **槽宽/导轨间距/平行面厚度** | ①边A ②边B (直线拟合) | `distance_lines` [edge1,edge2] | `measure_range` distance_px | 槽宽超差、板厚不均 |
| **孔心距 / 圆孔净间隙(孔壁距)** | ①孔A ②孔B (圆拟合) | `distance_circles` [c1,c2] | `measure_range` distance_px(=圆心距) 或 gap_px(净距) | 孔位偏移；壁厚不足、装配干涉(gap_px<0) |
| **特征到基准边距离** | ①特征 ②基准边 | `distance_point_line` | `measure_range` | 到边距离超差、偏置 |
| **两边缘虚拟角点/尖角** | ①边1 ②边2 | `intersect_lines` → 再 `distance_points`/`fit_rect` | `measure_range` 或中心距 | 倒角/尖点偏移、虚拟角定位 |
| **拐角夹角 / 卡尺三边夹角** | ①三点(顶点+两臂) | `angle_three_points` | `measure_range` angle | 拐角角度超差、多边形畸变 |
| **有向旋转/平行四边形/反射角** | ①有向线1 ②有向线2 | `directed_angle_between_lines` | `measure_range` signed_angle | 旋向错误、反射(与锐角折叠无法区分的缺陷) |
| **同心度 / 偏心量** | ①外圆 ②内圆 | `concentricity` | `measure_range` distance_px | 偏心、内外圈错位 |
| **圆角/倒角半径·弧长·弦长·圆心角** | ①圆弧上三点(start,mid,end) | `arc_from_three_points` | `measure_range` radius/arc_length | 圆角过小/过大、弧面畸变 |
| **多点位一致性/均值(直径/角度)** | ①..N 同类测量(命名) | `aggregate_stat` field+stat | `measure_range` value / stddev | 一致性差、异常点(最大偏差) |
| **位置度(GD&T, 直径式)** | ①实测中心 | `true_position` nominal_x/y | `measure_range` deviation_px | 位置度超差 |
| **直线度 / 圆度** | ①直线/圆拟合 | `fit_line/circle_from_points` 的 `rms_error_px`/`max_error_px` | `measure_range` max_error_px | 弯曲、椭圆化、面形不良 |

**示例——导轨平行度(两条拟合线的间距一致性)**:

```jsonc
{
  "aggregates": [
    { "algorithm": "fit_line_from_points", "points": ["railA_pts"], "name": "railA" },
    { "algorithm": "fit_line_from_points", "points": ["railB_pts"], "name": "railB" },
    { "algorithm": "distance_lines", "lines": ["railA", "railB"], "name": "gap" }
  ],
  "constraints": [
    // 扁平序末尾 gap 的下标;间距须在 20±0.1px
    { "type": "measure_range", "a": 2, "field": "distance_px", "min": 19.9, "max": 20.1 },
    // 同时可加角度约束判平行:angle_diff 或 directed_angle
    { "type": "angle_diff", "a": 0, "b": 1, "target_deg": 0, "tol_deg": 0.5 }
  ]
}
```

## 6. UI 操作流程

1. **添加步骤**:选择算法并调好参数/ROI → 点"**+ 添加当前算法**",加入工艺步骤列表;可 上移/下移/编辑/移除。
2. **选择整体判定**:下拉框 `AND(全部通过)` / `OR(任一通过)`。
3. **添加约束**:点"**+ 添加约束**" → 选步骤 A/B、类型、填入阈值(中心距 / 半径比 / 尺寸比 / 角度差 / 多步骤等距(多选步骤) / 到参考线距离(选步骤线或自定义线) / 对称度(填镜像轴))→ 确定。距离与对称类可选择 px 或 mm 单位。
4. **多点位(shot)分组**:对某步骤点"编辑" → 弹出对话框,可选"拍照点位(Shot)"下标
   (1..当前数),或"**分组到新 Shot**"创建新点位;同点位下可设置**图像路径**(离线用,
   空 = 当前/推送图)、**位置 X/Y**(机械坐标,mm)、**图像槽**(IPC)、**Cross-FOV**
   勾选框 + **Cam↔machine (°)**(hand-eye 夹角,见 §2.4)、**Light program / intensity**
   (逐点位光源,见 §2.1)与 **emit 标签**(可选,用于聚合拟合点源)。
   步骤列表显示 `Shot k · n. 算法`。
5. **聚合拟合 / 测量节点**:左栏"聚合拟合"区点"**+ 聚合拟合**" → 选"Fit / measure type"
   (拟合矩形/直线/圆,或距离/交点/三点角/有向角/同心度/三点弧/统计/位置度),
   填**来源标签**(各步骤的 `emit` 标签,或更早聚合节点的 `name`,逗号分隔)→ 确定。
   部分类型有专属参数:统计选字段与统计量(`mean/min/max/stddev/range/sum`);位置度填理论
   中心 X/Y;4 边矩形与有向线可设方向。可移除。输出命名(`name`)后可被后续节点或
   `measure_range` 约束按名引用。
6. **运行配方**:点"**▶ 运行配方**"(后台线程执行)→ 多点位配方弹窗按 shot 分组显示
   **# / Shot / 算法 / 位姿 / 有无 / 缺陷 / 字符串 / 状态 / 耗时**(聚合拟合行 Shot 显示"聚合",
   缺陷列显示 shape 类型与尺寸),外加约束表与整体判定;单点位(无 shot 分组)沿用原单图流程。
7. **保存/加载**:输入名称 → "保存配方";多点位配方存为 **v4**(`shots` 数组),含聚合拟合时存
   **v4.1**(`aggregates` 数组),否则保持 **v3**(扁平 `steps`,旧消费者不受影响)。
   "加载配方"向后兼容 v1/v2/v3/v4/v4.1。

## 6.1 语义化结果 JSON

每个配方步骤完成后,可导出紧凑**语义结果**(供 UI 结果弹窗 / IPC 回包 / SQLite 落库):

```jsonc
{ "algorithm": "circle_fit", "ok": 1, "ms": 12.3,
  "pose": {"x": 100.5, "y": 50.2, "an": 12.3, "ms": 0.9},   // 位姿;cx/cy 全 0 省略
  "presence": {"ok": 1},                                    // 有无 = 步骤 ok
  "defect": {"count": 2, "area": 100, "has_defect": true},  // 缺陷类步骤才有
  "string": "QR123",                                        // 二维码/OCR/条码/DM
  "shape": {"type": "rect", "w": 180, "h": 180, "n": 4} }   // 聚合拟合才有
```

- `pose` 取 `cx/cy/angle/match_score`;`defect` 按 `defect_count` → `crack_count` → `blob_count`
  优先级取数;`string` 按 `ocr_text` → `barcode_first_text` → `dm_first_text` 优先级取数;
  `shape` 在聚合拟合/测量节点出现(`{type, r, w, h, n}` + 标量测量值 `v`)。
- 数值 ≤3 位小数,空字段省略。**配方级语义结果**在 shots 之外另含顶层 `aggregates` 数组
  (每个聚合拟合/测量节点的语义步骤)与 `strings` 汇总;完整 schema 见
  [RECIPE_IPC_PROTOCOL.md](RECIPE_IPC_PROTOCOL.md) §5。
- **输出字段全集**(所有通道、所有可能的键与取值:`shape.type` 8 种、约束 8 类、
  各算法原始 `measurements` 字段表)见
  [RECIPE_IPC_PROTOCOL.md](RECIPE_IPC_PROTOCOL.md) §5.1。

## 7. 局限与演进

- **无跨步骤条件组合**(如"步骤 A 通过 且 步骤 B 通过,但仅当步骤 A 满足时才检查 B")。
- **mm 判定依赖标定**:距离/对称类约束选 mm 时,若当前相机未标定(px_per_mm=0)则判为 NG。
- **几何约束引用非定位步骤**(OCR/条码等,中心为 0)会判 NG"步骤缺少中心坐标",属预期防呆行为。
- **模板/模型/图像路径为绝对路径**:配方换机复用需保证引用文件(模板图、TM 模型、OCR 模型)存在于目标机对应路径。
- **IPC 内联至多 4 shot**(图像槽上限);引用越界槽且无 `image` 路径的 shot 会清晰报错(不静默回退到槽 0);离线 `image` 路径不受限。
- **IPC 结果 ≤2KB**:超限输出 `truncated` 降级形态,详细值仍落 SQLite(见 RECIPE_IPC_PROTOCOL.md §7)。

## 8. 代码位置

| 功能 | 位置 |
|---|---|
| 步骤参数 JSON 序列化 | `InspectTab::buildAlgorithmConfig`(12 类分支) |
| 步骤参数反序列化 | `DetectionWorker::fillParamsFromJson`(12 类分支) |
| 工艺链运行(单图) | `DetectionWorker::runRecipeSteps`(薄包装 → `runStepsOnImage`) |
| 多点位运行(v4) | `DetectionWorker::runRecipeShots` / `parseRecipeShots` / `parseRecipeConstraints` |
| 跨视野坐标统一 | `DetectionWorker::finalizeRecipeShots` + `parseShotTransform` / `transformMeasurements`(shot `transform` / 配方 `coordinate_frame`) |
| 坐标点发布(缺口 A) | `computeOne`(blob/corner/line/caliper 分支写 `measurements["points"]`) |
| 聚合拟合(缺口 B) | `DetectionWorker::runAggregateStep` + `fitLine/rect/circleFromPoints`、`parseRecipeAggregates` |
| 坐标系 / 阵列贴装 | `DetectionWorker::runAggregateStep`(`build_frame`/`placement_pattern` 分支)+ `buildSemanticStepResult` 的 `frame`/`pattern`/`poses` 输出 |
| 调试图像留存(追溯/复现) | `DetectionWorker::saveDebugCapture` + `DetectionSnapshot.saveDebug/debugDir/debugTag`；UI `InspectTab`(自动保存调试图/仅NG/目录)；配方步骤与单张检测每步保存原图+结果图+参数JSON |
| 语义结果构建 | `DetectionWorker::buildSemanticStepResult` / `buildRecipeResultJson`(2KB 预算守卫,含 `aggregates`) |
| 约束评估 | `DetectionWorker::evaluateRecipeConstraints`(8 类:两两式 4 + 等距/线距/对称 3 + 通用 `measure_range`) |
| 步骤/约束/shot/聚合 UI 与保存加载 | `InspectTab.cpp`(onAddRecipeStep/onAddRecipeConstraint/onAddRecipeAggregate/onEditRecipeStep/onRunRecipe/onSaveRecipe/onLoadRecipe) |
| 结果落库(配方步骤行) | `InspectTab::recordRecipeRun`、`InspectionRecorder.{h,cpp}` |
| 配方引擎回归测试 | `tests/unit/test_recipe.cpp`(目标 `recipe_tests`) |
