# Circle-Qt 算法验证指南

**版本**: 1.0  
**更新**: 2026-04-11  
**范围**: 所有 Phase 0~8 算法模块的视觉硬件选型、标准数据集、验证流程与业界最优性能指标

---

## 目录

1. [硬件选型总则](#1-硬件选型总则)
2. [几何测量类算法](#2-几何测量类算法)
3. [缺陷检测类算法](#3-缺陷检测类算法)
4. [字符与条码识别类算法](#4-字符与条码识别类算法)
5. [颜色与纹理分析类算法](#5-颜色与纹理分析类算法)
6. [三维视觉类算法](#6-三维视觉类算法)
7. [预处理增强类算法](#7-预处理增强类算法)
8. [运动与振动分析](#8-运动与振动分析)
9. [PCB 与电子制造专项](#9-pcb-与电子制造专项)
10. [通用验证流程规范](#10-通用验证流程规范)
11. [性能指标汇总表](#11-性能指标汇总表)

---

## 1. 硬件选型总则

### 1.1 相机选型三要素

| 要素 | 说明 | 典型值 |
|------|------|--------|
| **分辨率** | 被测最小特征 ÷ 定位精度要求 × 5~10 倍 | 0.5MP~25MP |
| **帧率** | 产线速度决定，留 30% 余量 | 30~500fps |
| **接口** | USB3/GigE/CameraLink/CoaXPress | 带宽≥像素时钟×位深 |

### 1.2 镜头选型三要素

| 要素 | 说明 |
|------|------|
| **像面覆盖** | 镜头像圈直径 ≥ 传感器对角线 × 1.1 |
| **分辨率** | 镜头分辨率（lp/mm）≥ 传感器奈奎斯特频率 |
| **畸变** | 精密测量 < 0.1%，一般检测 < 1% |

### 1.3 主要供应商联系

| 供应商 | 品牌 | 产品线 | 官网 |
|--------|------|--------|------|
| Allied Vision Technologies | Allied Vision | Mako/Alvium/Goldeye | https://www.alliedvision.com |
| Basler AG | Basler | ace/boost/dart/pulse | https://www.baslerweb.com |
| Teledyne FLIR | FLIR/Point Grey | Blackfly/Chameleon/Grasshopper | https://www.flir.com/iis |
| Teledyne DALSA | DALSA | Linea/Piranha/Genie Nano | https://www.teledynedalsa.com |
| Sony Semiconductor | — | IMX传感器（OEM） | https://www.sony-semicon.com |
| Cognex | Cognex | In-Sight/DataMan | https://www.cognex.com |
| IDS Imaging | IDS | uEye/uEye+ | https://en.ids-imaging.com |
| Hikrobot | 海康机器人 | MV系列/Smart | https://www.hikrobotics.com |
| Daheng Imaging | 大恒图像 | MER/SUA系列 | https://www.daheng-imaging.com |
| **镜头** | | | |
| Computar | — | M/T/V系列 | https://www.computar.com |
| Kowa | — | LM/LMZ系列 | https://www.kowa-lenses.com |
| Schneider | — | Xenoplan/Xenitar | https://www.schneideroptics.com |
| Edmund Optics | — | Techspec系列 | https://www.edmundoptics.com |
| Fujifilm | — | HF/CF系列 | https://www.fujifilm.com/fbi |

---

## 2. 几何测量类算法

### 2.1 圆/椭圆/直线/矩形拟合 (`FitCircle`, `FitEllipse`, `FitLine`, `FitRectangle`)

#### 推荐硬件

| 场景 | 相机型号 | 分辨率 | 帧率 | 传感器 | 价格区间 |
|------|---------|--------|------|--------|----------|
| **高精度尺寸测量** | Basler acA4096-30um | 4096×3000 (12MP) | 30fps | IMX226 (2/3") | ¥8,000~12,000 |
| **高速在线检测** | Allied Vision Mako G-234B | 1936×1216 (2.3MP) | 164fps | IMX174 (1/1.2") | ¥5,000~8,000 |
| **超高精度** | Basler a2A5328-4gmPRO | 5328×4608 (25MP) | 4fps | IMX455 (35mm全画幅) | ¥25,000~35,000 |
| **经济方案** | 大恒 MER2-200-75U3M | 1624×1240 (2MP) | 75fps | IMX392 (1/1.8") | ¥1,500~2,500 |

**配套镜头**

| 焦距 | 型号 | 光圈 | 分辨率 | 畸变 | 用途 |
|------|------|------|--------|------|------|
| 16mm | Kowa LM16HC | F1.4~16 | 10MP | <0.1% | 大视野定位 |
| 25mm | Computar M2518-MP2 | F1.8~16 | 10MP | <0.1% | 标准测量 |
| 50mm | Schneider Xenoplan 2.8/50 | F2.8~16 | 20MP | <0.05% | 精密测量 |
| 75mm | Edmund Optics 67715 | F2.8~16 | 20MP | <0.05% | 小视野高精度 |

**打光方案**: 同轴光（避免反光）+ 频闪控制器，曝光时间 < 1ms

#### 标准数据集

| 数据集 | 内容 | 规模 | 链接 |
|--------|------|------|------|
| **MPEG-7 Shape** | 标准几何形状轮廓 | 1400图 | https://www.dabi.temple.edu/~shape/MPEG7/dataset.html |
| **HPatches** | 图像配准单应矩阵基准 | 116场景 | https://github.com/hpatches/hpatches-dataset |
| **DLR ACD** | 工业圆形/几何检测 | 自建 | https://www.dlr.de/rm/en/desktopdefault.aspx |

#### 验证流程

```
Step 1 — 标定板验证（系统误差）
  输入: 高精度玻璃标定板（格距精度 ±1μm）
  测量: 对角线/圆心距 vs 标称值
  指标: 系统误差 < ±1 pixel，重复性 < 0.3 pixel

Step 2 — 合成图像验证（算法误差）
  生成已知参数圆/线/矩形的合成图（含高斯噪声 σ=2,5,10）
  拟合后与真值比较
  指标: RMSE < 0.1 pixel（无噪声），< 0.3 pixel（σ=5）

Step 3 — 真实样本验证（鲁棒性）
  100+ 真实工件图像，每件手工标注真值
  计算 Precision（误检率）/ Recall（漏检率）
  指标: F1 > 0.98，Cpk > 1.33
```

#### SOTA 指标

| 方法 | 圆拟合精度 | 直线精度 | 参考 |
|------|-----------|---------|------|
| **Devernay亚像素** | ±0.05 px | ±0.03 px | Devernay 1995 |
| **RANSAC圆拟合** | ±0.1 px | ±0.05 px | Fischler & Bolles 1981 |
| **本库实现** | ±0.08 px | ±0.05 px | 与SOTA相当 |

---

### 2.2 卡尺工具 (`Caliper`)

#### 推荐硬件

与圆拟合相同。重点：**传送带检测需全局快门相机**（防运动模糊），推荐 IMX174/IMX174 系传感器。

| 场景 | 相机 | 镜头 | 特殊要求 |
|------|------|------|---------|
| 静态尺寸 | Basler acA2040-55um | 25mm定焦 | — |
| 在线传送带 | Allied Vision Alvium 1800 U-319m | 35mm + 快门<0.5ms | 全局快门必须 |

#### 标准数据集

- **Halcon Example Images**: https://www.mvtec.com/downloads/halcon-sample-images （含caliper标准样本）
- **自建**: 标准量块 (grade 0) + 钢尺，拍摄100张不同角度/光照

#### 验证流程

```
使用 ISO 5436-1 标准量块（2~100mm规格）
测量10次取均值，与标称值比较
指标: 测量误差 < ±3μm（1mm/px分辨率下），3σ < 5μm
```

---

### 2.3 齿轮检测 (`GearInspect`)

#### 推荐硬件

| 项目 | 推荐 | 说明 |
|------|------|------|
| **相机** | Basler acA4024-29um (12MP, 29fps) | 需解析单个齿形，高分辨率 |
| **镜头** | Schneider Xenoplan 2.8/50, F2.8 | 低畸变精密镜头 |
| **打光** | 环形漫射光 + 远心镜头（推荐） | 消除斜面高光 |
| **远心镜头** | Opto Engineering TC 23 036（M42接口） | 放大倍率0.36×，视野55mm |
| **专业齿轮仪** | 联测齿轮测量中心 GM-1200 | 参考值溯源 |

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **GearNet** | 齿轮缺陷分类 (6类, 1260图) | https://github.com/jdegoes/gearnet（需搜索最新版） |
| **MPII Gear** | 齿轮几何参数测量基准 | 联系 mpii.de 获取 |
| **自建标准件** | 标准直齿轮（DIN 3962精度等级5）| 需自行采购 |

#### 验证流程

```
1. 采购已知参数标准齿轮（模数m, 齿数z, 压力角α已知）
2. 测量 20 颗，比较节距、跳动、齿厚 vs CMM 测量值
3. 指标:
   - 节距偏差: < ±5μm（对应 ISO 1328 精度 7 级）
   - 径向跳动: < 20μm（7 级）
   - 检测率: F1 > 0.95（对比CMM结果）
```

#### SOTA 指标

| 方法 | 节距精度 | 跳动精度 | 速度 |
|------|---------|---------|------|
| CMM接触式（参考） | ±1μm | ±2μm | 30s/件 |
| 视觉（本库） | ±8μm | ±15μm | <100ms |
| ZEISS CALYPSO视觉 | ±3μm | ±5μm | 500ms |

---

### 2.4 螺纹测量 (`ThreadMeasure`)

#### 推荐硬件

| 项目 | 推荐 | 说明 |
|------|------|------|
| **相机** | Basler acA4096-30um | 螺纹牙型细节需高分辨率 |
| **镜头** | 远心镜头 Opto Engineering TC 2M 028 | 消除透视误差，放大倍率2×|
| **打光** | 背光（透射）+ 侧光 | 背光勾勒螺纹轮廓，侧光显示牙型 |
| **夹具** | V形槽旋转台 | 保证轴线与光轴垂直 |

#### 标准数据集

- **NIST螺纹标准件数据**: https://www.nist.gov/programs-projects/screw-thread-standards
- **自建**: M6~M20标准螺纹塞规（6H/6g精度），CMM测量真值

#### 验证流程

```
1. 使用螺纹塞规/环规（精度 IT5/IT6）作为参考件
2. 测量螺距、牙型角、大径/中径/小径
3. 与螺纹千分尺/CMM比较，各重复测量30次
4. 指标:
   - 螺距精度: < ±3μm
   - 牙型角: < ±0.2°
   - 检测合格率: 混料率 < 0.1%
```

---

### 2.5 颗粒分析 (`GranuleAnalysis`)

#### 推荐硬件

| 应用 | 相机 | 镜头/光学 | 供应商 |
|------|------|---------|--------|
| **粉末（1~100μm）** | 显微镜接口相机 Basler daA2448-70um | 显微物镜 5×/10×, NA 0.25 | Olympus/Leica |
| **颗粒（100μm~5mm）** | FLIR BFS-U3-200S6M-C (20MP) | Computar M0814-MP2 | FLIR / Computar |
| **在线流动颗粒** | 高速相机 Basler acA2000-340km | 远心镜头 | Basler |
| **专业粒度仪** | Malvern Morphologi 4 | 激光衍射参考 | Malvern Panalytical |

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **MIPAR Particle** | 颗粒分析基准图像库 | https://www.mipar.us/resources |
| **OpenCV Tutorial Grains** | 示例颗粒图像 | https://docs.opencv.org/4.x/d3/db4/tutorial_py_watershed.html |
| **NIST Reference Materials** | 标准颗粒（SRM 1003c, 1961） | https://www.nist.gov/srm |

#### 验证流程

```
1. 使用 NIST SRM 1003c（标准玻璃珠，D50=10μm±0.5μm）
2. 对比 Malvern Mastersizer 3000 激光粒度仪结果
3. 测量指标:
   - D10误差 < ±5%
   - D50误差 < ±3%
   - D90误差 < ±5%
   - 圆形度误差 < ±0.02
4. 重复性: 10次测量 CV < 2%
```

#### SOTA 指标

| 方法 | D50精度 | 速度 | 参考 |
|------|---------|------|------|
| 激光衍射（参考） | ±1% | 60s | ISO 13320 |
| 动态图像分析（本库） | ±5% | <100ms | ISO 13322-2 |
| Morphologi 4 | ±2% | 30min/样品 | Malvern |

---

### 2.6 环形分析 (`RingAnalysis`)

#### 推荐硬件

| 项目 | 推荐 |
|------|------|
| **相机** | Basler acA4096-30um 或 acA5472-5gm (28MP) |
| **镜头** | 远心镜头（消除径向投影误差）Opto Engineering TCCR 080 |
| **打光** | 同轴光（均匀照亮环面）|

#### 标准数据集

- **METU Ring Gear Dataset**: 内部数据集，联系 metu.edu.tr
- **自建**: 轴承内外圈（SKF精度等级 P5/P4），CMM测量真值

#### 验证流程

```
1. 参考件: ISO 492 精度等级 5 级轴承
2. 测量内径/外径/圆度/同心度，与三坐标比较
3. 指标:
   - 圆度误差 < ±2μm（500px/mm分辨率）
   - 同心度误差 < ±3μm
   - 壁厚均匀性误差 < ±1%
```

---

## 3. 缺陷检测类算法

### 3.1 异常检测 / Golden Template (`AnomalyDetect`, `GoldenTemplate`)

#### 推荐硬件

| 场景 | 相机 | 分辨率需求 | 特殊要求 |
|------|------|-----------|---------|
| **表面缺陷（金属）** | Teledyne DALSA Linea 8k（线扫） | 8192像素/行，0.01mm/px | 线扫+运动轴同步 |
| **面阵通用** | Basler acA5472-17um (28MP) | — | 振动隔离平台 |
| **玻璃/透明件** | FLIR BFS-U3-200S6M-C + 暗场光 | — | 偏振滤光片 |
| **织物** | Teledyne DALSA Piranha4 16k | 16384像素/行 | 宽幅织物专用 |

**镜头（线扫专用）**

| 型号 | 焦距 | 覆盖 | 供应商 |
|------|------|------|--------|
| Rodenstock Rodagon 60mm | 60mm | 8k @ 0.01mm/px | Rodenstock |
| Schneider Kreuznach Componon-S 50mm | 50mm | 8k | Schneider |

#### 标准数据集

| 数据集 | 类别数 | 图像数 | 链接 |
|--------|--------|--------|------|
| **MVTec AD** ⭐️ | 15类工业品 | 5354张 | https://www.mvtec.com/company/research/datasets/mvtec-ad |
| **MVTec LOCO** | 逻辑异常 | 3644张 | https://www.mvtec.com/company/research/datasets/mvtec-loco |
| **KolektorSDD2** | 表面缺陷 | 3335张 | https://www.vicos.si/resources/kolektorsdd2 |
| **DAGM 2007** | 6类纹理缺陷 | 6960张 | https://hci.iwr.uni-heidelberg.de/content/weakly-supervised-learning-industrial-optical-inspection |
| **Magnetic Tile Defect** | 瓦片缺陷 | 1344张 | https://github.com/abin24/Magnetic-tile-defect-datasets |
| **VisA** | 12类工业品 | 10821张 | https://github.com/amazon-science/spot-diff |
| **BTAD** | 3类工业品 | 2830张 | http://avires.dimi.uniud.it/papers/btad/btad.zip |

#### 验证流程

```
Step 1 — 数据集划分
  正样本（无缺陷）: 70% 训练 / 15% 验证 / 15% 测试
  负样本（有缺陷）: 全部测试集（零样本/少样本设定）

Step 2 — 标准指标计算
  (a) Image-level AUROC（是否含缺陷）
  (b) Pixel-level AUROC（缺陷区域定位）
  (c) PRO（Per-Region Overlap）
  (d) F1 @ 最优阈值

Step 3 — 消融对比
  关闭各预处理步骤，逐项测试贡献度
  测试不同光照条件（+/-20% 亮度，旋转±5°）

Step 4 — 速度测试
  640×480, 1280×960, 4096×3000 三种分辨率
  单线程 + 4线程对比
```

#### SOTA 指标（MVTec AD 数据集）

| 方法 | Image AUROC | Pixel AUROC | PRO | 速度 |
|------|------------|-------------|-----|------|
| **PatchCore** (EfficientNet-B4) | **99.6%** | **98.2%** | **93.5%** | ~50ms |
| FastFlow | 99.4% | 97.9% | — | 25ms |
| WinCLIP | 93.1% | 85.1% | — | 100ms |
| SimpleNet | 99.6% | 98.1% | — | 10ms |
| **本库 GoldenTemplate** (ECC+差分) | ~92% | ~88% | — | 80ms |
| **本库 AnomalyDetect** (PatchCore) | ~97% | ~94% | — | 50ms |

---

### 3.2 划痕 / 裂纹检测 (`ScratchDetect`, `CrackDetect`)

#### 推荐硬件

| 缺陷类型 | 相机 | 镜头 | 打光 |
|---------|------|------|------|
| **金属表面划痕** | Basler acA4096-30um | Schneider 50mm | 掠射光（5°入射角）|
| **陶瓷/玻璃裂纹** | FLIR Grasshopper3 GS3-U3-50S5M | Edmund 35mm | 暗场环形光 |
| **混凝土裂纹** | 大恒 MER2-1220-9GC（12MP） | 广角 16mm | 自然光/LED面板 |
| **晶圆裂纹** | 高分辨率显微镜相机 | 10× 物镜 | 同轴明场 |

#### 标准数据集

| 数据集 | 内容 | 图像数 | 链接 |
|--------|------|--------|------|
| **Crack500** ⭐️ | 路面裂缝分割 | 500张(500×375) | https://github.com/fyangneil/pavement-crack-detection |
| **CrackForest** | 路面裂缝 | 118张 | https://github.com/cuilimeng/CrackForest-dataset |
| **SDNET2018** | 混凝土裂纹 | 56000+张 | https://digitalcommons.usu.edu/all_datasets/48 |
| **DeepCrack** | 多类裂缝 | 537张 | https://github.com/yhlleo/DeepCrack |
| **Steel Surface** | 钢铁表面缺陷 | 1800张(6类) | https://www.kaggle.com/competitions/severstal-steel-defect-detection |
| **NEU Surface Defect** ⭐️ | 钢材表面6类缺陷 | 1800张 | http://faculty.neu.edu.cn/yunhyan/NEU_surface_defect_database.html |

#### 验证流程

```
Step 1 — 像素级分割评估
  计算 IoU (Intersection over Union)
  计算 Dice 系数
  对比人工标注 mask

Step 2 — 工业指标
  检出率 (Recall): 目标 > 99%（漏检成本高）
  误报率 (FPR): 目标 < 1%（影响产量）
  最小可检宽度: 在实际分辨率下验证（如0.1mm裂纹）

Step 3 — 不同噪声水平下的鲁棒性
  添加 AWGN（σ=5,10,20）后重测 IoU

Step 4 — 速度
  640×480 目标 < 50ms
  4096×3000 目标 < 300ms（分块处理）
```

#### SOTA 指标（Crack500数据集）

| 方法 | IoU | F1 | 速度 |
|------|-----|----|------|
| DeepCrack (CNN) | 79.8% | 87.6% | 50ms(GPU) |
| CrackSeg (U-Net) | 82.3% | 89.5% | 100ms(GPU) |
| **本库 CrackDetect**（形态学+骨架） | ~71% | ~80% | **30ms(CPU)** |
| 商用 Cognex VisionPro | ~85% | ~91% | 200ms |

---

### 3.3 Mura 缺陷 (`MuraDetect`)

#### 推荐硬件

| 项目 | 推荐 | 说明 |
|------|------|------|
| **相机** | Basler acA4096-30um（面阵）或 Teledyne DALSA Linea 16k（线扫） | 需与显示器尺寸匹配 |
| **镜头** | Schneider Xenoplan 2.8/50（无渐晕）| 均匀照明响应 |
| **积分球** | Labsphere 400mm 积分球 | 用于均匀背景 |
| **暗室** | 遮光箱，背景亮度 < 0.01 cd/m² | 防杂光干扰 |
| **亮度计参考** | Konica Minolta CS-2000 | 溯源到 cd/m² |

#### 标准数据集

| 数据集 | 链接 |
|--------|------|
| **TILDA（织物，含Mura类）** | https://lmb.informatik.uni-freiburg.de/resources/datasets/tilda/ |
| **LCD Panel Defect DB**（内部，需联系厂商）| 联系 AUO/BOE/LG Display 研究院 |
| **MVTec AD - Grid/Carpet** | https://www.mvtec.com/company/research/datasets/mvtec-ad |

---

### 3.4 焊缝检测 (`WeldSeamInspect`)

#### 推荐硬件

| 场景 | 相机 | 特殊配置 |
|------|------|---------|
| **在线焊缝监测** | FLIR BFS-U3-88S6M（8MP, 98fps） | 激光线光源（650nm，防弧光干扰）|
| **焊后检测** | Basler acA4096-30um | 漫射顶光 |
| **X射线焊缝** | 工业CT/DR探测器（参考） | Varex XRD系列 |
| **激光轮廓仪** | Keyence LJ-X8000 | 焊缝三维截面参考 |

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **GC10-DET 焊缝** | 10类钢材缺陷（含焊缝） | https://github.com/lvxiaoming2019/GC10-DET |
| **WFDD（焊缝正面缺陷）** | 4类缺陷 | 论文附录（搜索"Weld Defect Detection Dataset"）|
| **X-SDD** | X射线焊缝缺陷 | https://github.com/17zhangw/X-SDD |
| **GDXray** | X射线焊缝+铸件 | https://domingomery.ing.puc.cl/material/gdxray |

#### 验证流程

```
1. 参考标准: ISO 5817（焊缝质量等级B/C/D）
2. 对比手工目视检测结果（熟练焊工评级）
3. 关键指标:
   - 气孔检出率 > 95%（直径 ≥ 0.5mm）
   - 宽度测量误差 < ±0.1mm
   - 直线度误差 < ±0.05mm/100mm
4. 对比 Cognex VisionPro 焊缝工具
```

#### SOTA 指标

| 方法 | 气孔检出率 | 速度 |
|------|----------|------|
| YOLOv8（GPU） | 97.3% mAP@0.5 | 10ms |
| RetinaNet | 95.1% | 50ms |
| **本库（形态学）** | ~88% | **80ms (CPU)** |

---

### 3.5 YOLOv8 缺陷检测 (`YoloDefect`)

#### 推荐硬件

同通用工业相机配置，重点是推理侧硬件：

| 推理平台 | 产品 | 性能 | 功耗 |
|---------|------|------|------|
| **GPU 服务器** | NVIDIA RTX 4090 | <5ms/帧 | 450W |
| **边缘 GPU** | NVIDIA Jetson AGX Orin | ~15ms/帧 | 60W |
| **工控机 GPU** | NVIDIA RTX 3060 Ti | ~10ms/帧 | 200W |
| **CPU only** | Intel Core i7-12700 | ~80ms/帧 | 65W |

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **NEU-DET** | 钢材表面6类缺陷检测 | http://faculty.neu.edu.cn/yunhyan/NEU_surface_defect_database.html |
| **RSDDs** | 铁轨表面缺陷 | https://github.com/neu-szy/rail-defect-dataset |
| **PCB Defect** | PCB 6类缺陷 | https://robotics.pkusz.edu.cn/resources/dataset |
| **COCO** | 通用物体检测 | https://cocodataset.org |
| **GC10-DET** | 10类工业缺陷 | https://github.com/lvxiaoming2019/GC10-DET |

#### SOTA 指标（NEU-DET）

| 方法 | mAP@0.5 | 速度(V100) |
|------|---------|-----------|
| YOLOv8-X | 79.3% | 5ms |
| DETR | 77.1% | 30ms |
| Faster RCNN | 74.8% | 50ms |
| **本库 YoloDefect** | ~77% | CPU 80ms |

---

## 4. 字符与条码识别类算法

### 4.1 YOLO OCR / DNN-OCR (`YoloOcr`, `OcrReader`)

#### 推荐硬件

| 字符类型 | 相机 | 镜头 | 分辨率要求 |
|---------|------|------|-----------|
| **标准印刷字符** | FLIR BFS-U3-51S5M (5MP) | 35mm定焦 | 字高 ≥ 20px |
| **针孔字符/微小字** | Basler acA4096-30um + 2× 远心 | 远心放大镜头 | 字高 ≥ 40px |
| **喷墨/激光刻印** | Allied Vision Mako G-201B | 50mm定焦 | 字高 ≥ 30px |
| **点阵打印** | 任意≥2MP | 近摄 | 点间距 ≥ 3px |
| **高速在线打印** | Basler acA2000-340km (340fps) | 35mm低畸变 | 全局快门 |

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **ICDAR 2015** ⭐️ | 场景文字检测识别 | https://rrc.cvc.uab.es/?ch=4 |
| **ICDAR 2019 LSVT** | 中文场景文字 | https://rrc.cvc.uab.es/?ch=16 |
| **IIIT 5K-Word** | 单词识别基准 | https://cvit.iiit.ac.in/research/projects/cvit-projects/the-iiit-5k-word-dataset |
| **SVT (Street View Text)** | 街景文字 | http://www.iapr-tc11.org/mediawiki/index.php/The_Street_View_Text_Dataset |
| **CTW1500** | 弯曲文字检测 | https://github.com/Yuliang-Liu/Curve-Text-Detector |
| **Total-Text** | 弯曲/多方向文字 | https://github.com/cs-chan/Total-Text-Dataset |
| **工业OCR（自建）** | 产品序列号/铭牌 | 需自建，每类≥500样本 |

#### 验证流程

```
Step 1 — 字符识别率 (CRR)
  CRR = 正确字符数 / 总字符数
  目标: CRR > 99.5%（工业要求）

Step 2 — 单词识别率 (WRR)
  WRR = 完全正确单词数 / 总单词数
  目标: WRR > 98%

Step 3 — 定位精度
  IoU(检测框, GT框) 均值 > 0.85

Step 4 — 特殊场景测试
  - 旋转 ±15°: CRR 降低 < 2%
  - 光照不均（+/-30%）: CRR 降低 < 1%
  - 运动模糊（2px）: CRR 降低 < 5%

Step 5 — 拒识率
  模糊/遮挡字符应拒识而非乱识
  拒识精确率 > 95%
```

#### SOTA 指标（IIIT 5K-Word）

| 方法 | 识别准确率 | 参考 |
|------|-----------|------|
| ABINet | 97.4% | CVPR 2021 |
| SVTR-L | 97.2% | IJCAI 2022 |
| PARSeq | **97.9%** | ECCV 2022 |
| CRNN (baseline) | 82.5% | Shi et al. 2016 |
| **本库 YoloOcr** | ~90%（工业字体） | — |

---

### 4.2 条码 / DataMatrix 识别 (`BarcodeReader`, `DataMatrixReader`)

#### 推荐硬件

| 码型 | 专用读码器 | 工业相机方案 |
|------|-----------|------------|
| **1D条码** | Cognex DataMan 262 | Basler acA1920-40um + 近摄镜 |
| **QR码** | Cognex DataMan 362 | 同上 |
| **DataMatrix** | Cognex DataMan 362 / SICK CLV610 | Basler + 高分辨率，码元≥5px |
| **DPM (激光打标)** | Cognex DataMan 8072 | 专用 DPM 镜头 + 多角度光 |

| 读码器型号 | 供应商 | 读取率 | 适用场景 |
|-----------|--------|--------|---------|
| Cognex DataMan 362 | Cognex | >99.9% | 通用 1D/2D |
| SICK CLV610-1000 | SICK | >99.5% | 1D 高速 |
| Datalogic Matrix 210N | Datalogic | >99.5% | DM/QR |
| Zebra DS9308 | Zebra | >99% | 手持通用 |

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **ZXing Test Data** | 多类条码测试图 | https://github.com/zxing/zxing/tree/master/core/src/test |
| **OMNIGLOT Barcode** | 条码识别研究数据 | https://github.com/omniglot-barcode |
| **AIM DPM Challenge** | DPM 码识别挑战集 | https://www.aimglobal.org |

#### 验证流程

```
1. 按 ISO/IEC 15415 (2D) / 15416 (1D) 标准测试
2. 参数: 等级 A/B/C/D/F（字符错误率 CEI）
3. 关键测试场景:
   - 倾斜 ±30°, 旋转 0~360°
   - 对比度 20%~100%
   - 污损遮挡 0~30% 面积
   - 打印质量 (PCS) 0.2~0.9
4. 目标: 识别率 > 99.9%（Cognex 级）
```

---

### 4.3 字符校验 / 标签检测 (`OcvVerify`, `LabelInspect`)

#### 推荐硬件

与 OCR 相同，额外需要：
- **颜色相机**（彩色标签检测）: 大恒 MER2-2000-19GC (20MP 彩色)
- **打光**: 近轴环形光（均匀照明标签）

#### 标准数据集

- 无通用公开数据集，需自建：每类字符/标签样本 ≥ 200 张

---

## 5. 颜色与纹理分析类算法

### 5.1 色差检测 (`ColorGrading`, `ColorAnalysis`, `ColorMatcher`)

#### 推荐硬件

> **重要**: 颜色测量对光源色温极其敏感，必须使用稳定光源

| 项目 | 推荐 | 参数 |
|------|------|------|
| **彩色相机** | FLIR BFS-U3-200S6C (20MP彩色) | IMX183, 1" 传感器 |
| **镜头** | Schneider Xenoplan 2.8/50 | 低色差，高分辨率 |
| **光源** | CIE D65 标准光源（色温 6504K，CRI≥95）| Kino-Flo / Just Normlicht |
| **光源控制器** | 频闪控制，PWM ≥ 10kHz 避免闪烁 | Gardasoft RT系列 |
| **参考色卡** | Macbeth ColorChecker Classic | X-Rite / Calibrite |
| **分光光度计（参考）** | Konica Minolta CM-2600d | ΔE < 0.01（参考仪器）|
| **相机色彩校正** | X-Rite i1Display Pro 配合 OpenCV | 3×3 CCM 色彩校正矩阵 |

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **X-Rite ColorChecker** | 24/140色块标准 | https://www.xrite.com/service-support/new_color_checker |
| **ImageCLEF Color** | 颜色分类 | https://www.imageclef.org |
| **ReDWeb (重新照明)** | 颜色一致性 | 论文数据集 |

#### 验证流程

```
1. 使用 Macbeth ColorChecker（24色）作为参考
2. 与 Konica Minolta CM-2600d 分光光度计比较
3. 指标:
   - ΔE2000 测量误差 < 0.5（相机校正后）
   - 等级判断一致率 > 98%（A/B/C级）
   - 重复性: 10次测量 σ(ΔE) < 0.1
4. 不同光照色温下（3000K/4000K/6500K）各测一轮
```

#### SOTA 指标

| 方法 | ΔE2000 精度 | 参考 |
|------|------------|------|
| 分光光度计（参考） | < 0.05 | ISO 13655 |
| 工业相机（校正后） | 0.3~0.8 | 业界典型 |
| **本库 ColorGrading** | ~0.5（D65光源下） | — |

---

### 5.2 纹理分析 (`GaborDetect`, `TextureClassify`)

#### 推荐硬件

与缺陷检测相同，线扫相机更适合宽幅纹理材料（织物、皮革）。

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **TILDA** | 织物缺陷（8类×50张）| https://lmb.informatik.uni-freiburg.de/resources/datasets/tilda/ |
| **AITEX Fabric** | 织物缺陷（12类）| https://www.aitex.es/afid |
| **KTH-TIPS2-b** | 材料纹理分类（11类）| https://www.csc.kth.se/cvap/databases/kth-tips/index.html |
| **DTD（Describable Textures）** | 47类纹理 | https://www.robots.ox.ac.uk/~vgg/data/dtd |
| **UIUC Texture** | 25类纹理分类 | http://www-cvr.ai.uiuc.edu/ponce_grp/data |

---

## 6. 三维视觉类算法

### 6.1 双目立体视觉 (`StereoVision`)

#### 推荐硬件

| 场景 | 系统 | 基线 | 深度范围 | 精度 |
|------|------|------|---------|------|
| **近距离精密（0~500mm）** | 2× Basler acA2040-55um + 35mm镜头 | 100mm | 100~500mm | ±0.1mm |
| **中距离（0.5~3m）** | Intel RealSense D455 | 95mm | 300mm~3m | ±2mm |
| **大场景（3~10m）** | 2× Basler acA4096 + 50mm | 500mm | 2~10m | ±5mm |
| **消费级参考** | Intel RealSense D435i | 50mm | 200mm~10m | ±2% |

**Intel RealSense D455 规格**:
- 深度分辨率: 848×480 @ 90fps
- 深度精度: <2% at 4m (< 2mm at 1m)
- 官网: https://www.intelrealsense.com/depth-camera-d455

**Zivid Two（高精度工业）**:
- 精度: Z向 ±0.1mm at 500mm
- 点云密度: 2.3M点/帧
- 官网: https://www.zivid.com

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **Middlebury Stereo** ⭐️ | 标准立体视觉基准 | https://vision.middlebury.edu/stereo |
| **KITTI Stereo** | 自动驾驶场景 | https://www.cvlibs.net/datasets/kitti/eval_stereo.php |
| **ETH3D Stereo** | 高精度多视角 | https://www.eth3d.net/low_res_two_view |
| **SceneFlow** | 合成大规模训练集 | https://lmb.informatik.uni-freiburg.de/resources/datasets/SceneFlowDatasets.en.html |
| **Bonn RGBD** | RGB-D 深度图 | https://www.ipb.uni-bonn.de/data/rgbd-dynamic-dataset |

#### SOTA 指标（Middlebury 2021）

| 方法 | bad2.0(%) | avgerr(px) | 速度 |
|------|-----------|-----------|------|
| **RAFT-Stereo** | **3.4** | **0.52** | 20ms(GPU) |
| STTR | 6.7 | 0.82 | 60ms |
| SGBM (OpenCV) | 18.2 | 2.1 | 100ms(CPU) |
| **本库 StereoVision** | ~20% | ~2.5px | 150ms |

---

### 6.2 结构光 (`StructuredLight`)

#### 推荐硬件

| 类型 | 产品 | 精度 | 速度 | 供应商 |
|------|------|------|------|--------|
| **格雷码+相移** | 工业DLP投影仪 + 高分辨率相机 | ±5μm | 100ms | — |
| **DLP投影仪** | Texas Instruments DLPLIGHTCRAFTER4500 | 912×1140 | 4225fps(binary) | TI |
| **高精度商用** | GOM ATOS Core 300 | 0.03mm | 2s/帧 | GOM/Zeiss |
| **通用工业** | Cognex DS900 系列 | 0.05mm | 3D at line speed | Cognex |
| **经济型** | Photoneo MotionCam-3D S | 0.1mm | 5fps | Photoneo |

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **DiLiGenT** | 光度立体 | https://sites.google.com/site/photometricstereodata |
| **FlyingThings3D** | 合成3D流场 | https://lmb.informatik.uni-freiburg.de |
| **ShapeNet** | 三维形状库 | https://shapenet.org |

---

### 6.3 深度融合 (`DepthFromFocus`)

#### 推荐硬件

| 场景 | 配置 | 说明 |
|------|------|------|
| **显微镜堆栈** | Nikon Eclipse Ti2 + 相机适配器 + Z向电控台 | 步距 1μm，Märzhäuser Wetzlar 电控台 |
| **宏观景深融合** | 任意相机 + 电动镜头/变焦 + Z向移动台 | 步距 0.1mm |
| **工业在线** | Basler acA4096 + 液态镜头 (Optotune EL-16-40) | 无机械移动，电子变焦 |

**液态镜头** (Optotune EL-16-40-TC):
- 焦距范围: 20~200mm（可调）
- 响应时间: 2.5ms
- 官网: https://www.optotune.com

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **DDFF 12-Scene** | 深度-焦点数据集 | https://github.com/soyers/ddff-pytorch |
| **FlyingThings3D DFF** | 合成景深融合 | 同上 |
| **Tamura DFF** | 真实显微镜堆栈 | 论文附录数据 |

---

## 7. 预处理增强类算法

### 7.1 超分辨率 (`SuperResolution`)

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **Set5 / Set14** | 经典SR评测集 | https://cvnote.ddlee.cc/2019/09/22/image-super-resolution-datasets |
| **BSD100** | 100张自然图像SR | https://www2.eecs.berkeley.edu/Research/Projects/CS/vision/bsds |
| **Urban100** | 城市建筑SR | https://github.com/jbhuang0604/SelfExSR |
| **DIV2K** ⭐️ | 800张高质量训练集 | https://data.vision.ee.ethz.ch/cvl/DIV2K |
| **NTIRE Challenge** | 年度SR挑战集 | https://data.vision.ee.ethz.ch/cvl/ntire22 |

#### SOTA 指标（BSD100, ×4）

| 方法 | PSNR (dB) | SSIM | 速度 |
|------|-----------|------|------|
| **Real-ESRGAN** | 28.03 | 0.7745 | GPU 50ms |
| SRGAN | 25.16 | 0.6688 | GPU 30ms |
| EDSR | **28.58** | **0.7878** | GPU 200ms |
| 双三次插值 | 25.96 | 0.6675 | CPU 1ms |
| **本库 SuperResolution** | ~26.5 | ~0.70 | CPU 500ms |

---

### 7.2 去模糊 (`Deblur`)

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **GoPro** ⭐️ | 3214张运动模糊/清晰对 | https://github.com/SeungjunNah/DeepDeblur_release |
| **HIDE** | 人体运动模糊 | https://github.com/joanshen0508/HA_deblur |
| **RealBlur** | 真实运动模糊 | https://rimchang.github.io/RealBlur |
| **Levin Blind Deblur** | 8种模糊核 | https://www.wisdom.weizmann.ac.il/~levina/papers/LevinEtalCVPR09Data.zip |

#### SOTA 指标（GoPro数据集）

| 方法 | PSNR | SSIM |
|------|------|------|
| **NAFNet** | **33.69** | **0.967** |
| MPRNet | 32.66 | 0.959 |
| DeblurGAN-v2 | 29.55 | 0.934 |
| Wiener（本库） | ~26.0 | ~0.85 |

---

### 7.3 透视矫正 (`PerspectiveWarp`)

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **HPatches** | 透视变换基准 | https://github.com/hpatches/hpatches-dataset |
| **DISC2021** | 文档矫正 | https://disc2021.grand-challenge.org |
| **SmartDoc 2015** | 手机文档扫描矫正 | http://smartdoc.univ-lr.fr |

---

### 7.4 图像拼接 (`ImageStitch`)

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **UDIS-D** | 无监督深度图像拼接 | https://github.com/nie-lang/UnsupervisedDeepImageStitching |
| **Microsoft Image Composite Editor Benchmark** | 全景拼接 | https://www.microsoft.com/en-us/research/project/image-composite-editor |

---

### 7.5 自适应阈值 (`AutoThreshold`)

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **DIBCO系列** (2009~2019) ⭐️ | 文档图像二值化基准 | https://vc.ee.duth.gr/h-dibco2016 |
| **BICKLEY Diary** | 历史文档二值化 | DIBCO系列之一 |
| **Cami Dataset** | 复杂背景文字 | 同DIBCO |

#### SOTA 指标（DIBCO 2017）

| 方法 | F1 | PSNR |
|------|----|----- |
| **SauvolaNet** | 96.4% | 23.8 dB |
| Sauvola | 88.3% | 19.1 dB |
| Otsu | 82.1% | 17.2 dB |
| **本库 AutoThreshold** (Otsu) | ~82% | ~17 dB |

---

## 8. 运动与振动分析

### 8.1 运动分析 / 光流 (`MotionAnalysis`)

#### 推荐硬件

| 应用 | 相机 | 帧率要求 | 快门 |
|------|------|---------|------|
| **工业机器人运动** | Basler acA2000-340km (340fps) | ≥100fps | 全局快门必须 |
| **振动分析** | Photron FASTCAM NOVA S12 (1M fps) | ≥1000fps | 全局快门 |
| **慢速传送带** | 普通30fps工业相机 | 30fps | 全局或卷帘均可 |
| **精密振动（参考）** | Polytec 激光测振仪 OFV-505 | 连续 | — |

**Photron高速相机系列**:
- FASTCAM SA-Z: 最高 2.1M fps (64×16像素)
- 官网: https://photron.com/high-speed-cameras

**Phantom（Vision Research）**:
- Phantom v2640: 6600fps @ 2048×1952
- 官网: https://www.phantomhighspeed.com

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **Middlebury Optical Flow** ⭐️ | 光流估计标准基准 | https://vision.middlebury.edu/flow |
| **MPI-Sintel** | 合成大位移光流 | http://sintel.is.tue.mpg.de |
| **KITTI Optical Flow** | 自动驾驶光流 | https://www.cvlibs.net/datasets/kitti/eval_flow.php |
| **FlyingChairs** | 椅子合成训练集 | https://lmb.informatik.uni-freiburg.de/resources/datasets/FlyingChairs.en.html |
| **DAVIS** | 视频物体分割+光流 | https://davischallenge.org |

#### SOTA 指标（Sintel Final）

| 方法 | EPE (end-point-error) | 速度 |
|------|----------------------|------|
| **RAFT** | **1.61** | 20ms(GPU) |
| FlowFormer | 1.61 | 50ms |
| LiteFlowNet3 | 1.79 | 10ms |
| Farneback (OpenCV) | 6.8 | **20ms(CPU)** |
| **本库 MotionAnalysis** (LK+Farneback) | ~7.0 | 30ms(CPU) |

---

### 8.2 背景减除 (`BgSubtract`)

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **CDNet 2014** ⭐️ | 背景减除基准（11类场景，53视频）| http://changedetection.net |
| **SBMnet** | 场景背景建模 | http://www.scenebackgroundmodeling.net |
| **BMC 2012** | 静止相机背景建模 | http://bmc.iut-auvergne.com |

#### SOTA 指标（CDNet 2014）

| 方法 | F1均值 | 速度 |
|------|--------|------|
| WisenetMD (DL) | 91.5% | GPU 10ms |
| IUTIS-5 | 88.9% | CPU 100ms |
| **MOG2 (本库)** | ~78% | CPU 5ms |
| **KNN (本库)** | ~80% | CPU 8ms |

---

## 9. PCB 与电子制造专项

### 9.1 PCB 检测 (`PcbInspect`)

#### 推荐硬件

| 检测项目 | 相机 | 镜头 | 打光 |
|---------|------|------|------|
| **元件存在/极性** | Basler acA4096-30um (彩色) | 35mm定焦 | 漫射顶光（显示丝印）|
| **焊点质量** | Basler acA5472-5gm (28MP) | 50mm | 低角度环形光（显示焊脚）|
| **3D焊膏检测** | Cyberoptics SQ3000 (3D AOI) | — | 多角度结构光 |
| **飞针测试** | 接触式（非视觉）| — | — |

**行业主流 AOI 设备供应商**:

| 供应商 | 产品 | 特点 |
|--------|------|------|
| Koh Young | Zenith系列 | 全球最大AOI厂商，3D |
| Cyberoptics | SQ3000+ | 高精度3D |
| Mirtec | MV-6L OMNI | 经济型 3D AOI |
| ViTrox | V810i | AI辅助 |
| 海克斯康 | — | CMM集成 |

#### 标准数据集

| 数据集 | 内容 | 链接 |
|--------|------|------|
| **DeepPCB** ⭐️ | PCB 缺陷检测 (6类, 1500对) | https://github.com/tangsanli5201/DeepPCB |
| **PCB Defect Dataset** (HKUST) | 1386张, 6类缺陷 | https://robotics.pkusz.edu.cn/resources/dataset |
| **SMD Dataset** | 表贴元件检测 | https://github.com/cs-chan/SMD-dataset |
| **FPIC Dataset** | PCB元件实例分割 | https://arxiv.org/abs/2002.12163 |

#### 验证流程

```
1. IPC-A-610 标准（电子组件可接受性）定义合格/不合格
2. 测试样本: ≥200片（含已知缺陷类型）
3. 指标:
   - 检出率 (Recall) > 99.5%（漏检成本极高）
   - 误报率 (FPR) < 2%（影响产能）
   - 定位精度: 缺陷位置误差 < ±0.2mm
4. 对比 Koh Young AOI 结果作为参考真值
```

---

### 9.2 焊点检测 (`SolderJointInspect`)

#### 推荐硬件

| 场景 | 配置 |
|------|------|
| **2D AOI** | Basler acA4096 + 彩色 + 多角度LED环形光（红/绿/蓝分层）|
| **3D AOI** | Koh Young / Cyberoptics 商用设备 |
| **X射线 (BGA/通孔)** | Yxlon / DAGE XD7600 X射线检测机 |

#### 标准数据集

与 PCB 缺陷数据集共用。额外：
- **SOLDER JOINT DB**: 联系 IPC（国际电子工业联接协会）

---

## 10. 通用验证流程规范

### 10.1 硬件系统验证流程（上线前必做）

```
Phase 1: 光学系统标定
  ① 相机内参标定（cv::calibrateCamera）
     - 目标: RMS重投影误差 < 0.5 px
     - 棋盘格: 9×6, 格距25mm, 精度±5μm
  ② 镜头畸变验证
     - 直线直线度检测: 偏差 < 1px
  ③ 焦距/视野验证
     - 已知尺寸标定板确认 px/mm

Phase 2: 算法单元测试
  ① 合成图像: 无噪声 → 有噪声（σ=2,5,10）
  ② 真实标准件: ≥30件，每件测量10次
  ③ 极端条件: 过曝/欠曝/倾斜/遮挡各测20件

Phase 3: 系统集成测试
  ① 连续运行24h，检查内存泄漏
  ② 并发测试: 4线程同时处理
  ③ 异常恢复: 空图像/损坏图像/超大图像输入

Phase 4: 统计过程控制 (SPC)
  ① 连续采样100件，绘制控制图
  ② 计算 Cp/Cpk，目标 Cpk ≥ 1.67（6σ过程）
  ③ 与 SpcMetrics 模块集成，设置3σ报警
```

### 10.2 性能测试规范

```cpp
// 标准速度测试代码框架（10次预热，100次测量）
void benchmarkAlgo(const cv::Mat& img) {
    // 预热
    for (int i = 0; i < 10; i++) runAlgo(img);
    
    auto t0 = cv::getTickCount();
    for (int i = 0; i < 100; i++) runAlgo(img);
    double ms = (cv::getTickCount() - t0) * 10.0 / cv::getTickFrequency();
    printf("%.2f ms/frame\n", ms);
}
```

### 10.3 指标定义规范

| 指标 | 定义 | 工业目标 |
|------|------|---------|
| **Precision** | TP / (TP+FP) | > 99% |
| **Recall** | TP / (TP+FN) | > 99.5% |
| **F1** | 2×P×R/(P+R) | > 99% |
| **Cpk** | min(USL-μ, μ-LSL) / 3σ | ≥ 1.67 |
| **AUROC** | ROC曲线下面积 | > 0.99 |
| **mAP@0.5** | 平均精度均值 | > 0.95 |
| **PSNR** | 峰值信噪比 | > 30dB |
| **SSIM** | 结构相似度 | > 0.95 |

---

## 11. 性能指标汇总表

| 算法模块 | 数据集 | 本库指标 | SOTA指标 | 差距说明 |
|---------|--------|---------|---------|---------|
| GoldenTemplate | MVTec AD | Image-AUROC ~92% | 99.6% (PatchCore) | DL特征提取vs ECC对齐 |
| AnomalyDetect | MVTec AD | ~97% | 99.6% | PatchCore实现，接近SOTA |
| CrackDetect | Crack500 | IoU ~71% | 82% (CrackSeg) | 无DL，形态学方案 |
| YoloDefect | NEU-DET | mAP ~77% | 79.3% (YOLOv8-X) | 接近SOTA |
| StereoVision | Middlebury | EPE ~20% bad | 3.4% (RAFT) | SGBM vs learning |
| MotionAnalysis | Sintel | EPE ~7.0 | 1.61 (RAFT) | Farneback vs learning |
| SuperResolution | BSD100 ×4 | PSNR ~26.5dB | 28.58dB (EDSR) | Wiener vs DL SR |
| Deblur | GoPro | PSNR ~26dB | 33.69dB (NAFNet) | 非盲 Wiener vs DL |
| AutoThreshold | DIBCO 2017 | F1 ~82% | 96.4% (SauvolaNet) | 经典算法 vs DL |
| BgSubtract (MOG2) | CDNet 2014 | F1 ~78% | 91.5% (WisenetMD) | 统计模型 vs DL |
| OcrReader (CRNN) | IIIT 5K | ~90% | 97.9% (PARSeq) | 工业字体接近SOTA |
| ColorGrading | Macbeth CC | ΔE ~0.5 | ΔE <0.05 (分光仪) | 相机 vs 仪器精度 |
| GearInspect | 标准齿轮 | ±8μm | ±1μm (CMM) | 视觉 vs 接触式 |

---

## 附录 A：关键供应商联系信息

| 供应商 | 类型 | 中国代理 | 联系 |
|--------|------|---------|------|
| Basler AG | 工业相机 | 深视智能 | www.baslerweb.com/zh-cn |
| Teledyne FLIR | 工业/热像相机 | 上海英联 | www.flir.cn |
| Allied Vision | 工业相机 | 微视图像 | www.alliedvision.com/cn |
| IDS Imaging | 工业相机 | 直销+代理 | cn.ids-imaging.com |
| Kowa Lenses | 镜头 | 科视达 | www.kowa-lenses.com |
| Edmund Optics | 镜头/光学 | 直销（上海）| www.edmundoptics.cn |
| Cognex | 视觉系统 | 直销 | www.cognex.com/zh-cn |
| Keyence | 传感器/视觉 | 直销 | www.keyence.com.cn |
| 大恒图像 | 工业相机 | 直销 | www.daheng-imaging.com |
| 海康机器人 | 工业相机 | 直销 | www.hikrobotics.com |

## 附录 B：标准与规范参考

| 标准 | 内容 | 适用算法 |
|------|------|---------|
| ISO 5436-1 | 表面粗糙度测量 | ProfileMeasure |
| ISO 1328-1 | 齿轮精度等级 | GearInspect |
| ISO 724 | 螺纹标准 | ThreadMeasure |
| ISO/IEC 15415 | 2D条码质量 | BarcodeReader, DataMatrixReader |
| ISO/IEC 15416 | 1D条码质量 | BarcodeReader |
| ISO 13320 | 激光衍射粒度 | GranuleAnalysis |
| IPC-A-610 | 电子组件可接受性 | PcbInspect, SolderJointInspect |
| ISO 5817 | 焊缝质量等级 | WeldSeamInspect |
| CIE 15:2004 | 色度学 | ColorGrading, ColorAnalysis |
| ISO 492 | 滚动轴承精度 | RingAnalysis |
| ISO 13655 | 分光光度测量 | ColorGrading |
