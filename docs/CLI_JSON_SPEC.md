# CLI JSON 配置文件规范

## 概述
UI和CLI共享的参数配置格式，用于记录算法测试参数和复现测试结果。

## 通用格式

```json
{
  "version": "1.0",
  "algorithm": "circle_fit",
  "image_path": "path/to/image.bmp",
  "output_path": "path/to/result.png",
  "timestamp": "2026-04-03T13:26:47",
  "parameters": {
    // 算法特定参数
  }
}
```

## 字段说明

### 顶层字段
- `version` (string): 配置文件格式版本，当前为 "1.0"
- `algorithm` (string): 算法类型
  - `"circle_fit"` - 圆拟合
  - `"line_fit"` - 直线拟合
  - `"ellipse_fit"` - 椭圆拟合（未来）
- `image_path` (string): 输入图像的绝对路径或相对路径
- `output_path` (string, optional): 输出结果图像路径
- `timestamp` (string, optional): 参数生成时间（ISO 8601格式）
- `parameters` (object): 算法特定参数

---

## 圆拟合算法 (circle_fit)

### 完整示例

```json
{
  "version": "1.0",
  "algorithm": "circle_fit",
  "image_path": "C:/Users/difoh/source/repos/circle-qt/tests/images/test.bmp",
  "output_path": "C:/Users/difoh/source/repos/circle-qt/tests/images/result.png",
  "timestamp": "2026-04-03T13:26:47",
  "parameters": {
    "roi": {
      "center": {
        "x": 640.0,
        "y": 480.0
      },
      "inner_radius": 200.0,
      "outer_radius": 300.0
    },
    "radius_range": {
      "min": 220.0,
      "max": 280.0
    },
    "filter": {
      "radius_tolerance": 20.0,
      "angle_gap_deg": 30.0
    },
    "ransac": {
      "iterations": 1000,
      "inlier_distance": 3.0,
      "min_inliers": 10
    },
    "edge_detection": {
      "method": "devernay",
      "canny_low": 50,
      "canny_high": 150,
      "sobel_ksize": 3,
      "subpixel_step": 0.5
    }
  }
}
```

### 参数详解

#### roi (Region of Interest)
- `center.x` (float): ROI中心X坐标（像素）
- `center.y` (float): ROI中心Y坐标（像素）
- `inner_radius` (float): ROI内半径（像素）
- `outer_radius` (float): ROI外半径（像素）

#### radius_range
- `min` (float): 期望圆半径最小值（像素）
- `max` (float): 期望圆半径最大值（像素）

#### filter
- `radius_tolerance` (float): 半径容差（像素），范围 [0.1, 100.0]
- `angle_gap_deg` (float): 角度间隙阈值（度），范围 [1.0, 90.0]

#### ransac
- `iterations` (int): RANSAC迭代次数，范围 [10, 5000]
- `inlier_distance` (float): 内点距离阈值（像素），范围 [0.01, 50.0]
- `min_inliers` (int): 最小内点数，范围 [3, 1000]

#### edge_detection
- `method` (string): 边缘检测方法
  - `"devernay"` - Devernay亚像素边缘检测（推荐）
  - `"canny"` - Canny边缘检测
- `canny_low` (int): Canny低阈值，范围 [1, 500]
- `canny_high` (int): Canny高阈值，范围 [1, 500]
- `sobel_ksize` (int): Sobel核大小，必须为奇数 [1, 3, 5, 7]
- `subpixel_step` (float): 亚像素采样步长，范围 [0.05, 2.0]

---

## 直线拟合算法 (line_fit)

### 完整示例

```json
{
  "version": "1.0",
  "algorithm": "line_fit",
  "image_path": "C:/Users/difoh/source/repos/circle-qt/tests/images/test.bmp",
  "output_path": "C:/Users/difoh/source/repos/circle-qt/tests/images/result.png",
  "timestamp": "2026-04-03T13:26:47",
  "parameters": {
    "roi": {
      "type": "rotated_rect",
      "center": {
        "x": 640.0,
        "y": 480.0
      },
      "width": 400.0,
      "height": 100.0,
      "angle": 0.0
    },
    "line_range": {
      "min_length": 100.0,
      "max_length": 500.0
    },
    "filter": {
      "distance_tolerance": 5.0,
      "angle_tolerance": 10.0
    },
    "ransac": {
      "iterations": 1000,
      "inlier_distance": 2.0,
      "min_inliers": 10
    },
    "edge_detection": {
      "method": "devernay",
      "canny_low": 50,
      "canny_high": 150,
      "sobel_ksize": 3,
      "subpixel_step": 0.5
    }
  }
}
```

### 参数详解

#### roi (旋转矩形)
- `type` (string): ROI类型，固定为 "rotated_rect"
- `center.x` (float): 矩形中心X坐标（像素）
- `center.y` (float): 矩形中心Y坐标（像素）
- `width` (float): 矩形宽度（像素）
- `height` (float): 矩形高度（像素）
- `angle` (float): 旋转角度（度），范围 [-180, 180]

#### line_range
- `min_length` (float): 期望直线最小长度（像素）
- `max_length` (float): 期望直线最大长度（像素）

#### filter
- `distance_tolerance` (float): 点到直线距离容差（像素）
- `angle_tolerance` (float): 角度容差（度）

#### ransac
- `iterations` (int): RANSAC迭代次数
- `inlier_distance` (float): 内点距离阈值（像素）
- `min_inliers` (int): 最小内点数

#### edge_detection
（与圆拟合相同）

---

## CLI 使用方法

### 圆拟合
```bash
# 基本用法
circle_fit_cli config.json

# 指定输出路径（覆盖JSON中的output_path）
circle_fit_cli config.json result.png

# 向后兼容：直接指定图像
circle_fit_cli image.bmp
circle_fit_cli image.bmp config.json
circle_fit_cli image.bmp config.json result.png
```

### 直线拟合
```bash
# 基本用法
line_fit_cli config.json

# 指定输出路径
line_fit_cli config.json result.png

# 向后兼容：直接指定图像
line_fit_cli image.bmp
line_fit_cli image.bmp result.png
```

---

## UI 导出功能

### 导出时机
1. 用户点击"搜索"按钮并成功拟合后
2. 用户点击"导出配置"按钮（可选功能）

### 导出位置
建议保存在图像同目录下，文件名格式：
- `<image_name>_<algorithm>_config.json`
- 例如：`test_circle_fit_config.json`

### UI实现要点
```cpp
// 伪代码示例
void MainWindow::onSearch() {
    // 执行算法...

    if (success) {
        // 导出配置
        QString configPath = getImageDir() + "/" +
                            getImageBaseName() + "_" +
                            getAlgorithmName() + "_config.json";
        exportConfig(configPath);
    }
}

void MainWindow::exportConfig(const QString& path) {
    QJsonObject root;
    root["version"] = "1.0";
    root["algorithm"] = getCurrentAlgorithm(); // "circle_fit" or "line_fit"
    root["image_path"] = m_currentImagePath;
    root["output_path"] = ""; // 可选
    root["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonObject params;
    // 根据算法类型填充参数...
    root["parameters"] = params;

    // 保存文件
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
```

---

## 版本兼容性

### v1.0 (当前版本)
- 支持圆拟合和直线拟合
- 完整的参数记录

### 未来扩展
- v1.1: 添加椭圆拟合支持
- v1.2: 添加批量处理配置
- v2.0: 添加预处理参数（去噪、增强等）

---

## 注意事项

1. **路径格式**：建议使用绝对路径，或相对于配置文件的相对路径
2. **浮点精度**：保留1位小数即可，避免过高精度
3. **参数验证**：CLI工具应验证参数范围，超出范围时使用默认值并警告
4. **向后兼容**：CLI工具应同时支持旧的命令行参数格式
5. **错误处理**：JSON解析失败时，应给出清晰的错误提示
