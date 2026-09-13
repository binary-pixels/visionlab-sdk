# 视觉标定系统设计文档

**适用系统**: 龙门式机器视觉抓取平台  
**标定片类型**: 圆形点阵（Circular Dot Array）  
**最后更新**: 2026-04-09

---

## 目录

1. [系统硬件结构](#1-系统硬件结构)
2. [坐标系定义](#2-坐标系定义)
3. [标定体系全景](#3-标定体系全景)
4. [标定参数数据模型](#4-标定参数数据模型)
5. [圆点阵检测算法](#5-圆点阵检测算法)
6. [L1 相机内参标定](#6-l1-相机内参标定)
7. [L2 上视相机世界坐标标定](#7-l2-上视相机世界坐标标定)
8. [L3 吸嘴–相机偏移标定](#8-l3-吸嘴相机偏移标定)
9. [L4 工位原点标定](#9-l4-工位原点标定)
10. [运行时坐标变换](#10-运行时坐标变换)
11. [标定质量评估](#11-标定质量评估)
12. [C++ 数据结构](#12-c-数据结构)
13. [标定文件 JSON 格式](#13-标定文件-json-格式)
14. [视觉工具功能需求](#14-视觉工具功能需求)
15. [精度分析](#15-精度分析)
16. [常见问题排查](#16-常见问题排查)

---

## 1. 系统硬件结构

### 1.1 龙门机械结构

```
龙门组件（可移动）
├── X 轴：左右运动（正方向向右）
├── Y 轴：前后运动（正方向视机器定义，标定时确认）
├── Z 轴：上下运动（正方向向上）
├── R 轴：吸嘴旋转（正方向逆时针）
├── 下视相机（固定在龙门，不可单独旋转）
│     - 传感器平面水平
│     - 安装存在微小角度误差（不可避免）
│     - 约 500 万像素，1× 远心镜头
│     - 视野约 7.1mm × 8.4mm
└── 圆形吸嘴（可绕 Z 轴旋转）
      - 吸嘴中心与相机中心距离固定
      - 连线方向设计上平行于机器 X 轴
      - 实际存在微小偏差（由标定测量）

固定组件（不随龙门移动）
└── 上视相机（固定安装，镜头朝上）
      - 用于标定吸嘴轮廓和位置
      - 已知物理安装位置（需标定精确世界坐标）
```

### 1.2 相机规格参数

| 参数 | 下视相机 | 上视相机 |
|------|----------|----------|
| 像素规格 | ~2592 × 1944 (5MP) | 视需求选型 |
| 镜头类型 | 1× 远心镜头 | 远心或定焦 |
| 视野 | 7.1mm × 8.4mm | 根据吸嘴尺寸 |
| 像素尺寸 | ~2.74 μm/px | 实测标定 |
| 安装方式 | 镜头朝下 | 镜头朝上（固定） |
| 传感器平面 | 水平 | 水平 |

---

## 2. 坐标系定义

### 2.1 涉及的坐标系

```
┌────────────────────────────────────────────────────────┐
│ 坐标系名称       原点          X 轴    Y 轴    单位    │
├────────────────────────────────────────────────────────┤
│ 世界坐标系 (W)  机器零点       向右    向前     mm      │
│ 图像坐标系 (I)  图像左上角     向右    向下     px      │
│ 相机物理坐标(C) 图像中心       向右    向上     mm      │
│ 点阵坐标系 (D)  点阵参考角点   列方向  行方向   mm      │
└────────────────────────────────────────────────────────┘
```

> **注意**：图像坐标系 Y 轴朝下，相机物理坐标系 Y 轴朝上（已翻转），
> 与世界坐标系 Y 轴方向的关系需在标定时通过实验确认符号。

### 2.2 坐标系关系示意

```
世界坐标系 (W)
        Y（前）
        ↑
        │      相机安装角 θ_c（夸大示意）
        │      图像 u 轴 ─────► （实际与 X 轴有微小夹角 θ_c）
        │
        └─────────────► X（右）

图像坐标系 (I)
  (0,0)──────────────► u (px)
    │
    │
    ▼
    v (px)

图像与世界的关系（中心偏移量）：
  (cx_world, cy_world)  ←龙门当前世界坐标
        ↕  ↕
  图像中心 (W/2, H/2)
        ↕  ↕
  像素偏移 (du, dv) → 经 s, θ_c 变换 → 世界偏移 (dx_w, dy_w)
```

### 2.3 关键变量命名约定

| 变量 | 含义 | 单位 |
|------|------|------|
| `s_u`, `s_v` | 像素物理尺寸（u 方向，v 方向） | mm/px |
| `θ_c` | 相机安装角（图像 u 轴与机器 X 轴夹角） | 度 |
| `cx`, `cy` | 主点（图像中心像素坐标） | px |
| `k1`, `k2` | 径向畸变系数 | 无量纲 |
| `Δx_nc`, `Δy_nc` | 吸嘴中心相对相机中心的偏移（世界坐标系） | mm |
| `θ_n0` | 吸嘴旋转轴零点偏差 | 度 |
| `(x_uc, y_uc)` | 上视相机中心世界坐标 | mm |

---

## 3. 标定体系全景

### 3.1 层级依赖关系

```
圆点阵（物理真值）
    │
    ├──→ L1-A: 下视相机内参
    │          s_u, s_v, θ_c, k1, k2
    │               │
    │               ├──→ L4: 各工位原点坐标
    │               │        station.origin (x, y, z)
    │               │
    │               └──→ L3: 吸嘴–相机偏移
    │                        Δx_nc, Δy_nc, θ_n0
    │                              ↑
    └──→ L1-B: 上视相机内参
               s_u, s_v, θ_c (up)
                    │
                    └──→ L2: 上视相机世界坐标
                              x_uc, y_uc, z_uc
                                    │
                                    └──────────→ L3 ↑
```

**标定执行顺序**：L1-A → L1-B → L2 → L3 → L4

每次更换相机、重新安装镜头、或机器大修后，必须重新执行对应层级及其所有下游层级的标定。

### 3.2 标定参数汇总

| 标定项 | 参数数量 | 影响 | 重标触发条件 |
|--------|----------|------|--------------|
| 下视相机内参 | 6（su, sv, θ_c, cx, cy, k1） | 所有视觉测量精度 | 换相机/镜头/重装 |
| 上视相机内参 | 6 | 吸嘴偏移精度 | 换相机/镜头/重装 |
| 上视相机世界坐标 | 3（x, y, z） | 吸嘴偏移精度 | 上视相机移位 |
| 吸嘴–相机偏移 | 3（Δx, Δy, θ_n0） | 抓取定位精度 | 吸嘴/龙门机械调整 |
| 工位原点 | 3（x, y, z）× 工位数 | 该工位定位精度 | 工位夹具调整 |

---

## 4. 标定参数数据模型

### 4.1 标定片配置

标定片参数为已知量，在使用前录入系统，不通过标定获得。

```json
"dot_array": {
  "pitch_mm": 2.0,
  "dot_diameter_mm": 0.8,
  "cols": 11,
  "rows": 9,
  "reference_corner": "top_left",
  "asymmetry_marker": "missing_dot_at_row1_col1"
}
```

### 4.2 下视相机参数

```json
"cameras": {
  "cam_down_1": {
    "type": "downward",
    "description": "龙门下视相机1",
    "image_width": 2592,
    "image_height": 1944,
    "pixel_scale_u": 0.002739,
    "pixel_scale_v": 0.002741,
    "install_angle_deg": 0.152,
    "principal_cx": 1296.0,
    "principal_cy": 972.0,
    "distortion_k1": 0.00012,
    "distortion_k2": 0.0,
    "reprojection_error_px": 0.048,
    "calibrated": true,
    "calibrated_at": "2026-04-09T10:00:00Z"
  }
}
```

**参数说明**：

| 参数 | 含义 | 典型值 | 合格阈值 |
|------|------|--------|----------|
| `pixel_scale_u/v` | 水平/垂直方向像素尺寸 (mm/px) | 0.00274 | 误差 < 0.1% |
| `install_angle_deg` | 相机安装角（u 轴与机器 X 轴夹角） | < ±1° | 无要求，准确即可 |
| `principal_cx/cy` | 主点坐标（理想值 = 图像中心） | W/2, H/2 | 偏差 < 20px |
| `distortion_k1` | 一阶径向畸变 | < 0.001（远心） | < 0.005 |
| `reprojection_error_px` | 标定重投影误差 | < 0.05px | < 0.1px |

### 4.3 上视相机参数

```json
"upward_cameras": {
  "cam_up_1": {
    "type": "upward_fixed",
    "description": "固定上视相机（吸嘴标定站）",
    "image_width": 2592,
    "image_height": 1944,
    "pixel_scale_u": 0.003125,
    "pixel_scale_v": 0.003127,
    "install_angle_deg": -0.088,
    "principal_cx": 1296.0,
    "principal_cy": 972.0,
    "distortion_k1": 0.00008,
    "distortion_k2": 0.0,
    "world_x_mm": 152.340,
    "world_y_mm": 83.760,
    "world_z_mm": 0.0,
    "reprojection_error_px": 0.061,
    "calibrated": true,
    "calibrated_at": "2026-04-09T10:30:00Z"
  }
}
```

上视相机特有参数：

| 参数 | 含义 |
|------|------|
| `world_x_mm`, `world_y_mm` | 上视相机光轴（图像中心）在世界坐标系中的 XY 坐标 |
| `world_z_mm` | 一般定义为 0（参考平面），或实测安装高度 |

### 4.4 吸嘴参数

```json
"nozzles": {
  "nozzle_1": {
    "description": "1号吸嘴",
    "associated_camera": "cam_down_1",
    "offset_x_mm": 35.412,
    "offset_y_mm": 0.018,
    "angle_zero_offset_deg": 0.28,
    "calibrated": true,
    "calibrated_at": "2026-04-09T11:00:00Z"
  }
}
```

| 参数 | 含义 | 说明 |
|------|------|------|
| `offset_x_mm` | 从相机中心到吸嘴中心的 X 偏移（世界坐标系） | 即 Δx_nc |
| `offset_y_mm` | Y 偏移 | 理想为 0，实际有装配误差 |
| `angle_zero_offset_deg` | 吸嘴 R 轴编码器零点对应的实际角度偏差 | θ_n0 |

### 4.5 工位参数

```json
"stations": {
  "feed_tray_1": {
    "description": "进料托盘工位1",
    "camera": "cam_down_1",
    "origin_x_mm": 98.220,
    "origin_y_mm": 51.340,
    "origin_z_mm": -24.500,
    "search_radius_mm": 5.0,
    "calibrated": true
  },
  "place_board_1": {
    "description": "放料基板工位1",
    "camera": "cam_down_1",
    "origin_x_mm": 205.110,
    "origin_y_mm": 51.380,
    "origin_z_mm": -27.800,
    "search_radius_mm": 3.0,
    "calibrated": true
  }
}
```

`origin_x/y_mm`：龙门移到此坐标拍照，目标件应出现在视野中央附近。  
`search_radius_mm`：允许目标偏离 origin 的最大距离（超出则报 NG）。  
`origin_z_mm`：拍照时的工作 z 高度，与焦距匹配。

---

## 5. 圆点阵检测算法

### 5.1 输入/输出

```
输入：
  img          — 灰度图像 (cv::Mat)
  dot_array    — 点阵配置（pitch, diameter, cols, rows）
  expected_su  — 预估像素尺寸（用于计算期望 blob 面积范围）

输出：
  dot_centers  — 检测到的点中心列表，每个元素包含：
                   pixel_u, pixel_v  — 亚像素图像坐标 (px)
                   grid_col, grid_row — 在点阵中的列行索引
  H            — 点阵坐标系到图像坐标系的单应矩阵 (3×3)
  detect_count — 成功检测点数
  missing_ids  — 未检测到的点列表
```

### 5.2 完整算法步骤

#### Step 1：预处理

```cpp
// 高斯模糊，减少传感器噪声
cv::GaussianBlur(img, blurred, cv::Size(5,5), 1.0);

// Otsu 自动阈值二值化（适用于高对比度点阵）
cv::threshold(blurred, binary, 0, 255,
              cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
// THRESH_BINARY_INV：点阵白底黑点 → 黑底白点，便于连通域分析
// 若点阵为黑底白点，去掉 _INV
```

#### Step 2：Blob 检测与过滤

```cpp
// 期望的 blob 面积（像素²）
double dot_r_px = (dot_diameter_mm / 2.0) / expected_su;
double area_expect = M_PI * dot_r_px * dot_r_px;
double area_min = area_expect * 0.4;
double area_max = area_expect * 2.5;

std::vector<std::vector<cv::Point>> contours;
cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

std::vector<cv::Point2f> rough_centers;
for (auto& c : contours) {
    double area = cv::contourArea(c);
    if (area < area_min || area > area_max) continue;

    // 圆度过滤：4π·A / P² > 0.80
    double perimeter = cv::arcLength(c, true);
    double circularity = 4.0 * M_PI * area / (perimeter * perimeter);
    if (circularity < 0.80) continue;

    // 初步质心
    cv::Moments m = cv::moments(c);
    rough_centers.push_back({(float)(m.m10/m.m00), (float)(m.m01/m.m00)});
}
```

#### Step 3：亚像素精化

对每个粗略中心，在局部窗口内用图像矩精化：

```cpp
cv::Point2f subpixelCentroid(const cv::Mat& gray, cv::Point2f rough, int win_r) {
    int x0 = std::max(0, (int)rough.x - win_r);
    int y0 = std::max(0, (int)rough.y - win_r);
    int x1 = std::min(gray.cols-1, (int)rough.x + win_r);
    int y1 = std::min(gray.rows-1, (int)rough.y + win_r);

    double sum_w=0, sum_wx=0, sum_wy=0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            double w = gray.at<uchar>(y,x);
            // 若白底黑点，用 (255 - pixel) 作权重
            sum_w  += w;
            sum_wx += w * x;
            sum_wy += w * y;
        }
    }
    return {(float)(sum_wx/sum_w), (float)(sum_wy/sum_w)};
}
```

> 此方法精度约 **0.05px**，对应 ~0.14μm（@2.74μm/px）。

#### Step 4：拓扑重建（识别网格索引）

```
目标：将每个 blob 的像素坐标 (u_i, v_i) 赋予网格坐标 (col_i, row_i)

算法：
1. 估算像素间距：
   - 计算所有点对距离，取最小非零距离聚类的众数 → pitch_px

2. 构建邻域图（k-NN 图）：
   - 对每个点，搜索距离 < 1.4 × pitch_px 的邻居
   - 理论上每个内部点有 4 个邻居（上下左右）

3. 确定坐标轴方向：
   - 统计所有邻居向量的角度分布
   - 找到两个主方向（约相差 90°）→ 对应 u_axis 和 v_axis

4. BFS 传播网格坐标：
   - 选取左上角参考点（或不对称缺点旁的特定点）作为 (0,0)
   - 沿 u_axis 方向传播：col + 1
   - 沿 v_axis 方向传播：row + 1

5. 方向歧义消解：
   - 若点阵有缺点（asymmetry marker），检测缺点位置 → 唯一确定方向
   - 否则需要用户确认或依赖机械约束
```

#### Step 5：单应矩阵求解

```cpp
// 收集配对点
std::vector<cv::Point2f> img_pts;   // 像素坐标
std::vector<cv::Point2f> world_pts; // 点阵物理坐标 (mm)

for (auto& dot : detected_dots) {
    img_pts.push_back({dot.pixel_u, dot.pixel_v});
    world_pts.push_back({dot.grid_col * pitch_mm, dot.grid_row * pitch_mm});
}

// 最小二乘求解单应矩阵
// H: [u; v; 1] ~ H × [X; Y; 1]
cv::Mat H = cv::findHomography(world_pts, img_pts, cv::RANSAC, 2.0);
```

#### Step 6：从单应矩阵提取标定参数

```
H 的结构（忽略畸变和主点偏移时）：
    H ≈ [ s_u·cos(θ)   -s_v·sin(θ)   tx ]
        [ s_u·sin(θ)    s_v·cos(θ)   ty ]
        [     0              0         1  ]

提取：
  s_u = √(H[0,0]² + H[1,0]²)        // 第0列向量模长 → u 方向尺度
  s_v = √(H[0,1]² + H[1,1]²)        // 第1列向量模长 → v 方向尺度
  θ_c = atan2(H[1,0], H[0,0])        // 旋转角（弧度转度）
  tx  = H[0,2]                        // 平移（相机中心在点阵中的位置）
  ty  = H[1,2]
```

---

## 6. L1 相机内参标定

### 6.1 下视相机内参标定（L1-A）

#### 准备条件

- 圆点阵标定片放置在龙门工作台面上，水平放置
- Z 轴移到正常工作高度（与生产时一致）
- 光源开启，亮度调整使点阵图像对比清晰（点为深色，背景浅色，或相反）

#### 操作步骤

```
1. 龙门移动到点阵正上方，使点阵完整出现在图像视野内
   （点阵应覆盖图像面积的 60% 以上，且尽量居中）

2. 触发拍照，获得标定图像

3. 运行圆点阵检测算法（Section 5），获得：
   (u_i, v_i) ↔ (X_i, Y_i) 点对，N ≥ 20（越多越好）

4. 求解单应矩阵 H

5. 提取标定参数：s_u, s_v, θ_c

6. （可选）若对精度要求高，补充多角度/多位置图像，
   用 cv::calibrateCamera 求解完整畸变模型

7. 计算重投影误差，验收（< 0.1px）

8. 保存到 calibration.json 的 cameras 节点
```

#### 多图像策略（提高精度）

```
若需要更精确的畸变标定：
- 采集 5～15 张图像
- 每张图像：龙门平移，使点阵出现在图像不同区域
             （左上、左下、右上、右下、中心各1张以上）
- 用 cv::calibrateCamera 同时求解内参矩阵 K 和畸变系数 D
- 重投影误差目标 < 0.1px
```

### 6.2 上视相机内参标定（L1-B）

#### 准备条件

- 圆点阵标定片**悬放**在上视相机正上方，与正常使用工作距离一致
- 可用夹具固定，保持水平

#### 操作步骤

与 L1-A 完全相同，区别仅在于：
- 图像中点为亮色（吸嘴/物件通常为浅色），背景较暗
- 点阵方向可能不同，注意参考角识别

---

## 7. L2 上视相机世界坐标标定

### 7.1 目标

确定上视相机图像中心（光轴）在机器世界坐标系中的位置 `(x_uc, y_uc, z_uc)`。

### 7.2 方法：吸嘴-上视相机互引导法

此方法利用吸嘴作为"移动基准点"，精度高，不依赖额外工具。

#### 前提

- L1-A（下视相机内参）已完成
- L1-B（上视相机内参）已完成
- 已知吸嘴的设计偏移量 `Δx_design`（用于粗对准，精确值尚未标定）

#### 操作步骤

```
步骤1：粗对准
  龙门移动，使吸嘴尖端大约进入上视相机视野中心
  （使用设计偏移量估算龙门坐标：x_g = x_uc_rough - Δx_design）

步骤2：上视相机拍照，检测吸嘴轮廓
  - 检测吸嘴外圆（圆拟合，FitCircle）
  - 获得吸嘴中心像素坐标 (u_noz, v_noz)
  - 计算偏离图像中心的像素偏移：
        du = u_noz - cx_up
        dv = v_noz - cy_up

步骤3：将偏移量转换到世界坐标
  - 经上视相机安装角 θ_c_up 旋转变换：
        dx_up = (du × s_u_up × cos θ_c_up) - (dv × s_v_up × sin θ_c_up)
        dy_up = (du × s_u_up × sin θ_c_up) + (dv × s_v_up × cos θ_c_up)
        注意 dv 转世界 Y 时需检查符号（见 Section 2）

步骤4：计算吸嘴世界坐标
  - 吸嘴在世界坐标系中的位置：
        x_nozzle = x_g + Δx_design + dx_up  ← 龙门坐标 + 设计偏移 + 上视相机测量偏差
        （注：此时 Δx_design 仍是近似值，但此步骤只需要吸嘴的绝对位置）

步骤5：计算上视相机中心世界坐标
  - 上视相机中心 = 吸嘴世界坐标 - 上视相机测量的吸嘴偏移量
        x_uc = x_nozzle - dx_up
        y_uc = y_nozzle - dy_up

步骤6：重复 3～5 次，取均值
  - 每次轻微平移龙门，重新引导，测量
  - 均值作为最终 x_uc, y_uc

步骤7：保存到 calibration.json 的 upward_cameras 节点
```

### 7.3 验证方法

```
1. 龙门移到 (x_g2, y_g2)，使下视相机对准台面已知标记点 P
2. 下视相机测量 P 的世界坐标 → x_P, y_P

3. 龙门移到 (x_g3, y_g3)，使吸嘴正好对准 P
   上视相机同时应看到吸嘴在其视野内，并能计算出 P 的坐标

4. 对比两种方式计算出的 P 坐标，差值应 < 0.02mm
```

---

## 8. L3 吸嘴–相机偏移标定

### 8.1 目标参数

```
Δx_nc：从下视相机中心到吸嘴中心的 X 方向偏移（世界坐标系，mm）
Δy_nc：Y 方向偏移（理想为 0，实际装配存在偏差）
θ_n0 ：吸嘴 R 轴编码器值为 0 时，吸嘴实际朝向与机器 X 轴的夹角（度）
```

### 8.2 方法：上视相机双步引导法

#### 前提

- L1-A、L1-B、L2 均已完成

#### 步骤 A：测量吸嘴世界坐标（多次采样）

```
1. 龙门移动，使吸嘴进入上视相机视野
2. 上视相机拍照，圆拟合检测吸嘴外圆中心 (u_n, v_n)
3. 计算吸嘴相对上视相机中心的偏移（世界坐标）：
      Δx_noc = (u_n - cx_up) × s_u_up × cos(θ_c_up) - ...（完整旋转）
      Δy_noc = ...
4. 吸嘴世界坐标：
      x_nozzle_world = x_uc + Δx_noc
      y_nozzle_world = y_uc + Δy_noc
5. 同时记录此时龙门位置 (x_g_A, y_g_A)
```

#### 步骤 B：测量下视相机中心世界坐标

```
注：龙门坐标 = 下视相机中心世界坐标（定义上）
即：x_cam_world = x_g_A
    y_cam_world = y_g_A

但要确认：当龙门报告 (x_g, y_g) 时，下视相机中心恰好在 (x_g, y_g)
这需要在机器控制器中确认坐标系定义
```

#### 步骤 C：计算偏移

```
Δx_nc = x_nozzle_world - x_cam_world = x_nozzle_world - x_g_A
Δy_nc = y_nozzle_world - y_cam_world = y_nozzle_world - y_g_A
```

#### 步骤 D：吸嘴角度零点标定（θ_n0）

```
方法：在上视相机下，令 R 轴编码器 = 0
      上视相机检测吸嘴上某个方向特征（如凹槽、标记线）的角度
      或：吸嘴吸住有方向的矩形标定片，上视相机检测矩形角度

      θ_n0 = 上视相机检测到的角度（相对世界 X 轴）
             当 R 轴编码器 = 0 时

步骤：
1. R 轴归零
2. 吸嘴吸住标准矩形标定片（长边平行于 X 轴放置）
3. 龙门移到上视相机上方
4. 上视相机拍照，检测矩形角度 r_measured（相对世界 X 轴）
5. θ_n0 = r_measured
   （实际抓取时，旋转指令 = 目标角度 - θ_n0）
```

### 8.3 多次采样策略

偏移标定应重复采样 5 次以上，取均值：

```
采样1：正常位置
采样2：龙门 X + 5mm 后重新引导
采样3：龙门 X - 5mm 后重新引导
采样4：龙门 Y + 5mm 后重新引导
采样5：龙门 Y - 5mm 后重新引导

均值：Δx_nc = mean(Δx_nc_1..5)
      Δy_nc = mean(Δy_nc_1..5)
标准差应 < 0.005mm，否则说明系统不稳定
```

### 8.4 验证方法

```
1. 将矩形靶标放置在工作台已知位置
2. 下视相机拍照，检测矩形中心 → 计算目标世界坐标 (x_t, y_t)
3. 利用 Δx_nc, Δy_nc 计算龙门移动目标 (x_pick, y_pick)
4. 龙门移到 (x_pick, y_pick)，吸嘴下降，执行吸取
5. 用视觉确认吸取位置偏差（上视相机或下视相机重拍）
   目标：偏差 < 0.05mm
```

---

## 9. L4 工位原点标定

### 9.1 目标

每个工位记录一个"参考原点"世界坐标：
- **用途**：龙门第一次到达工位时的搜索起始坐标
- **含义**：在此坐标拍照，目标件应出现在视野中央附近（偏差 < `search_radius_mm`）

### 9.2 精标定方法（推荐）

```
前提：工位上有固定的特征标记（如圆点、孔位、刻线）

步骤：
1. 操作员手动驱动龙门到该工位，使特征标记进入视野
2. 下视相机拍照，检测特征标记的像素坐标 (u_f, v_f)
3. 将像素偏移转换为世界坐标偏移（使用 L1-A 标定参数）：
      du = u_f - cx,  dv = v_f - cy
      dx_w, dy_w = 经 s, θ_c 变换
4. 特征标记世界坐标：
      x_ref = x_g_current + dx_w
      y_ref = y_g_current + dy_w
5. 工位原点 = 特征标记世界坐标（或从特征点推算的工位中心）
6. 工作高度 z：由 Z 轴编码器当前值读取（焦距清晰时的高度）
```

### 9.3 粗标定方法（快速）

```
直接手动操作龙门到工位中心位置，读取编码器坐标即为 origin_x/y/z
精度取决于操作员，约 ±0.5mm，适用于精度要求不高的工位
```

### 9.4 批量工位标定

```
若有多个相同规格的工位（如进料托盘有 4 个格位）：
1. 精标定第1个工位
2. 其余工位坐标 = 第1工位 + 设计节距 × 偏移量
3. 用视觉验证每个工位，微调修正节距误差
```

---

## 10. 运行时坐标变换

### 10.1 完整计算流程

**输入**：
```
龙门拍照坐标：(x_w, y_w, z_w)     ← 机器控制器反馈
图像检测结果：(x_p, y_p, r_p)     ← 矩形中心像素坐标 + 角度（°）
标定参数：    s_u, s_v, θ_c, cx, cy, Δx_nc, Δy_nc, θ_n0
```

**计算步骤**：

```
Step 1：像素偏移（以图像中心为原点）
  du = x_p - cx
  dv = y_p - cy

Step 2：转为相机物理坐标（mm）
  dx_c =  du × s_u
  dy_c = -dv × s_v       ← 图像 v 轴向下，世界 Y 轴向前（符号依机器定义确认）

Step 3：旋转到机器世界坐标（补偿相机安装角）
  dx_w = dx_c × cos(θ_c) - dy_c × sin(θ_c)
  dy_w = dx_c × sin(θ_c) + dy_c × cos(θ_c)

Step 4：目标中心世界坐标
  x_target = x_w + dx_w
  y_target = y_w + dy_w
  z_target = z_w         ← 工件在同一 z 平面时，或单独测量工件高度

Step 5：吸嘴落点（补偿吸嘴–相机偏移）
  x_pick = x_target - Δx_nc
  y_pick = y_target - Δy_nc
  z_pick = z_target - Δz_pick     ← Δz_pick 为吸取高度偏移（固定值）

Step 6：吸嘴旋转角（矫正工件方向）
  r_world = r_p + θ_c              ← 工件在世界坐标系中相对 X 轴的角度
  r_cmd   = r_world - θ_n0         ← 发给 R 轴的编码器指令值

  若工件有对称性（矩形 90° 对称）：
  r_cmd = r_cmd mod 90.0           ← 取绝对值最小的旋转量
  若 |r_cmd| > 45.0: r_cmd -= 90.0 × sign(r_cmd)

输出：(x_pick, y_pick, z_pick, r_cmd) → 发给运动控制器
```

### 10.2 坐标变换矩阵形式

```
完整变换（含安装角旋转）：

[ dx_w ]   [ s_u·cos(θ_c)    s_v·sin(θ_c) ] [  du  ]
[ dy_w ] = [ s_u·sin(θ_c)   -s_v·cos(θ_c) ] [ -dv  ]

注：第二列符号依赖图像 v 轴与世界 Y 轴的方向关系，必须通过实验验证
验证方法：龙门沿 +Y 移动 Δy，观察图像中固定点沿哪个方向移动
```

### 10.3 特殊情况处理

**工件高度变化（Z 轴补偿）**：

远心镜头在 DOF 范围内放大倍率不变，s_u/s_v 与 z 无关，无需补偿。  
超出 DOF 时图像模糊，直接报 NG。

**图像质量判断（拍照前检查）**：

```
- 平均亮度：[30, 220]（8-bit），过曝/欠曝报警
- 对比度（标准差）：> 20，偏低说明光源故障
- 清晰度（Laplacian 方差）：> 阈值，偏低说明离焦
```

**多目标情况**：

若视野内有多个矩形，按以下优先级选择：
1. 最靠近图像中心的
2. 面积最接近期望值的
3. 置信度（拟合残差）最小的

---

## 11. 标定质量评估

### 11.1 重投影误差

```
定义：已知物理坐标的点，经标定模型正变换到图像坐标，
      与实际检测像素坐标的差值（px）

计算：
  err_i = sqrt((u_pred_i - u_meas_i)² + (v_pred_i - v_meas_i)²)
  mean_err = mean(err_i for all i)

合格标准：
  mean_err < 0.1px  → 合格
  mean_err < 0.05px → 优秀
  mean_err > 0.2px  → 不合格，需重标
```

### 11.2 吸嘴偏移标定精度

```
多次采样标准差：std(Δx_nc_samples) < 0.005mm
多次验证偏差：  < 0.02mm
```

### 11.3 工位原点验证

```
在工位不同位置放置靶标，统计计算坐标与实际坐标之差：
  单次误差 < 0.02mm（系统误差）
  重复误差 < 0.01mm（随机误差）
```

### 11.4 标定有效性监控

每次生产前可执行"快速验证"：
```
1. 龙门移到上视相机上方，吸嘴进入视野
2. 上视相机检测吸嘴位置
3. 对比与上次标定值的偏差：
   偏差 > 0.05mm → 警告，建议重标
   偏差 > 0.1mm  → 停机，强制重标
```

---

## 12. C++ 数据结构

```cpp
struct DotArrayConfig {
    double pitch_mm = 2.0;
    double dot_diameter_mm = 0.8;
    int cols = 11;
    int rows = 9;
    QString reference_corner = "top_left";
};

struct CameraCalibParams {
    QString id;
    QString description;
    int img_w = 2592, img_h = 1944;
    double su = 0.0, sv = 0.0;          // mm/px
    double install_angle_deg = 0.0;      // θ_c
    double cx = 0.0, cy = 0.0;          // 主点 (px)
    double k1 = 0.0, k2 = 0.0;         // 畸变
    double reproj_error_px = 0.0;
    bool calibrated = false;
    QString calibrated_at;

    // 像素偏移 → 世界偏移（统一变换接口）
    void pixelToWorld(double du, double dv,
                      double& dx_w, double& dy_w) const;
};

struct UpwardCameraCalibParams : public CameraCalibParams {
    double world_x = 0.0;
    double world_y = 0.0;
    double world_z = 0.0;

    // 图像坐标 → 世界坐标（绝对位置）
    void imageToWorld(double u, double v,
                      double& x_w, double& y_w) const;
};

struct NozzleCalibParams {
    QString id;
    QString description;
    QString associated_camera_id;
    double offset_x_mm = 0.0;     // Δx_nc
    double offset_y_mm = 0.0;     // Δy_nc
    double angle_zero_deg = 0.0;  // θ_n0
    bool calibrated = false;
    QString calibrated_at;
};

struct StationCalibParams {
    QString id;
    QString description;
    QString camera_id;
    double origin_x_mm = 0.0;
    double origin_y_mm = 0.0;
    double origin_z_mm = 0.0;
    double search_radius_mm = 5.0;
    bool calibrated = false;
    QString calibrated_at;
};

struct PickResult {
    double x_pick, y_pick, z_pick;  // 吸嘴落点世界坐标 (mm)
    double r_cmd;                    // 吸嘴旋转指令 (°)
    bool valid = false;
    QString error_msg;
};

struct CalibrationModel {
    QString version = "1.0";
    DotArrayConfig dot_array;
    QMap<QString, CameraCalibParams>        cameras;
    QMap<QString, UpwardCameraCalibParams>  upward_cameras;
    QMap<QString, NozzleCalibParams>        nozzles;
    QMap<QString, StationCalibParams>       stations;

    // 运行时主接口
    PickResult calcPickPose(
        const QString& camera_id,
        const QString& nozzle_id,
        double x_w, double y_w, double z_w,   // 拍照世界坐标
        double x_p, double y_p, double r_p    // 检测结果（px, px, °）
    ) const;

    bool isFullyCalibrated() const;
    QString calibrationStatus() const;

    void saveToJson(const QString& path) const;
    bool loadFromJson(const QString& path);
};
```

---

## 13. 标定文件 JSON 格式

完整示例：

```json
{
  "version": "1.0",
  "calibrated_at": "2026-04-09T10:00:00Z",

  "dot_array": {
    "pitch_mm": 2.0,
    "dot_diameter_mm": 0.8,
    "cols": 11,
    "rows": 9,
    "reference_corner": "top_left"
  },

  "cameras": {
    "cam_down_1": {
      "type": "downward",
      "description": "龙门下视相机1",
      "image_width": 2592,
      "image_height": 1944,
      "pixel_scale_u": 0.002739,
      "pixel_scale_v": 0.002741,
      "install_angle_deg": 0.152,
      "principal_cx": 1296.0,
      "principal_cy": 972.0,
      "distortion_k1": 0.00012,
      "distortion_k2": 0.0,
      "reprojection_error_px": 0.048,
      "calibrated": true,
      "calibrated_at": "2026-04-09T10:00:00Z"
    }
  },

  "upward_cameras": {
    "cam_up_1": {
      "type": "upward_fixed",
      "description": "固定上视相机（吸嘴标定站）",
      "image_width": 2592,
      "image_height": 1944,
      "pixel_scale_u": 0.003125,
      "pixel_scale_v": 0.003127,
      "install_angle_deg": -0.088,
      "principal_cx": 1296.0,
      "principal_cy": 972.0,
      "distortion_k1": 0.00008,
      "distortion_k2": 0.0,
      "world_x_mm": 152.340,
      "world_y_mm": 83.760,
      "world_z_mm": 0.0,
      "reprojection_error_px": 0.061,
      "calibrated": true,
      "calibrated_at": "2026-04-09T10:30:00Z"
    }
  },

  "nozzles": {
    "nozzle_1": {
      "description": "1号吸嘴",
      "associated_camera": "cam_down_1",
      "offset_x_mm": 35.412,
      "offset_y_mm": 0.018,
      "angle_zero_offset_deg": 0.28,
      "calibrated": true,
      "calibrated_at": "2026-04-09T11:00:00Z"
    }
  },

  "stations": {
    "feed_tray_1": {
      "description": "进料托盘工位1",
      "camera": "cam_down_1",
      "origin_x_mm": 98.220,
      "origin_y_mm": 51.340,
      "origin_z_mm": -24.500,
      "search_radius_mm": 5.0,
      "calibrated": true,
      "calibrated_at": "2026-04-09T11:30:00Z"
    },
    "place_board_1": {
      "description": "放料基板工位1",
      "camera": "cam_down_1",
      "origin_x_mm": 205.110,
      "origin_y_mm": 51.380,
      "origin_z_mm": -27.800,
      "search_radius_mm": 3.0,
      "calibrated": true,
      "calibrated_at": "2026-04-09T11:45:00Z"
    }
  }
}
```

---

## 14. 视觉工具功能需求

### 14.1 标定工具模块

| 功能 | 说明 | 依赖 |
|------|------|------|
| 圆点阵检测器 | 亚像素圆心检测、拓扑重建、参考点识别、可视化叠加 | — |
| 相机内参标定向导 | 拍照采集、单应矩阵求解、参数提取、误差报告 | 圆点阵检测器 |
| 上视相机世界坐标标定 | 引导式向导、多次采样均值计算 | 内参标定 |
| 吸嘴偏移标定向导 | 上视相机引导对准、差值计算、多采样统计 | 上视相机世界坐标 |
| 角度零点标定 | 上视相机检测吸嘴特征、角度计算 | 吸嘴偏移 |
| 工位原点标定 | 工位特征检测、坐标记录、批量支持 | 相机内参 |
| 标定文件管理 | JSON 存取、历史版本管理、参数对比 | — |
| 标定质量报告 | 重投影误差、采样统计、合格/不合格判定 | — |

### 14.2 运行时模块

| 功能 | 说明 |
|------|------|
| 矩形检测（含角度） | 亚像素精度，输出中心 + 角度 + 置信度 |
| 坐标变换引擎 | 像素→世界，支持多相机/多吸嘴配置 |
| 图像质量自检 | 亮度、对比度、清晰度自动评估 |
| NG 处理 | 检测失败/超出 search_radius 时输出 NG 信号 |
| 结果输出 | (x_pick, y_pick, z_pick, r_cmd) → PLC/运动控制器 |
| 日志记录 | 每次检测：时间戳、输入坐标、检测结果、输出坐标 |

### 14.3 调试/验证工具

| 功能 | 说明 |
|------|------|
| 覆盖层可视化 | 检测结果、坐标轴方向、ROI 范围叠加显示 |
| 标定验证模式 | 移动到计算坐标后重拍确认偏差 |
| 精度统计 | 连续 N 次抓取的重复定位精度（Cpk/Ppk） |
| 参数敏感性分析 | 各标定参数误差对最终精度的影响量 |

---

## 15. 精度分析

### 15.1 误差来源与量级

| 误差来源 | 量级 | 影响 |
|----------|------|------|
| 像素检测（亚像素） | 0.05px ≈ 0.14μm | 算法层，可忽略 |
| 像素尺度标定误差 | 0.01% → 0.7μm @7mm 视野 | 可接受 |
| 相机安装角标定误差 | 0.01° → 0.9μm @5mm 偏心 | 可接受 |
| 吸嘴偏移标定误差 | 0.005mm | 主要误差之一 |
| 机械重复定位精度 | ±5～10μm | **系统瓶颈** |
| 工件高度变化（非远心） | 无（远心镜头免疫） | 不适用 |

### 15.2 误差传递

```
综合视觉系统误差（不含机械）：
  σ_total = √(σ_pixel² + σ_scale² + σ_angle² + σ_nozzle²)
          ≈ √(0.14² + 0.7² + 0.9² + 5²) μm
          ≈ 5.1 μm

机械重复定位精度（典型值）：±5～10 μm

最终综合精度：±10～15 μm（3σ）
对于大多数工业抓取场景：合格（要求通常 ±0.05mm）
```

### 15.3 提高精度的措施

```
1. 提高标定点数量（N > 50 个点阵点）
2. 重复标定 3 次取均值
3. 在实际工作温度下标定（避免热膨胀误差）
4. 定期验证，检测标定漂移
5. 对机械精度要求高时，引入在线闭环补偿（二次视觉校正）
```

---

## 16. 常见问题排查

### 16.1 圆点阵检测失败

| 症状 | 可能原因 | 解决方法 |
|------|----------|----------|
| 检测到的点数少于预期 | 图像过曝/欠曝 | 调整光源亮度或相机曝光 |
| 检测到很多噪声点 | 阈值不当 | 调整圆度和面积过滤参数 |
| 拓扑重建失败 | 点间距估算错误 | 检查 pitch_mm 配置是否与实物一致 |
| 方向识别错误 | 参考点未检测到 | 确保不对称标记在视野内 |

### 16.2 标定后重投影误差偏大

| 症状 | 可能原因 | 解决方法 |
|------|----------|----------|
| 误差 > 0.2px | 畸变未建模 | 增加畸变系数拟合 |
| 误差 > 0.2px | 标定时 z 高度与生产不一致 | 重新在生产工作高度标定 |
| 误差 > 0.1px | 采集点分布不均匀 | 确保点阵覆盖图像各区域 |
| 边缘误差大 | 镜头边缘畸变 | 增加边缘区域采集样本 |

### 16.3 吸取偏差大

| 症状 | 可能原因 | 解决方法 |
|------|----------|----------|
| 偏差固定方向 | Δx_nc 或 Δy_nc 标定误差 | 重新标定吸嘴偏移 |
| 偏差随位置变化 | 相机安装角 θ_c 误差 | 重新标定相机内参 |
| 偏差随角度变化 | θ_n0 标定误差 | 重新标定角度零点 |
| 偏差不重复 | 机械松动 | 检查吸嘴/相机安装紧固 |

### 16.4 坐标系符号问题

```
问题：Y 轴方向不确定时，偏差沿 Y 方向翻倍。

排查方法：
1. 龙门沿 +Y 移动已知量 Δy（如 1mm）
2. 观察图像中固定点的移动方向（向上还是向下）
3. 如果图像中点向下移动（dv > 0），则：
     世界 +Y 对应图像 +v → dy_c = dv × s_v（不翻转）
4. 如果图像中点向上移动（dv < 0），则：
     世界 +Y 对应图像 -v → dy_c = -dv × s_v（翻转，即当前公式）
5. 确认后修正 Step 2 中 dy_c 的符号
```

---

**文档维护说明**  
本文档描述标定体系的设计规范，应与实际代码实现同步更新。  
标定参数文件（`calibration.json`）位于 `deploy/` 目录，不进 git 版本控制。
