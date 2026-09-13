# Plugin Integration Quick Reference

## 最常用的 6 个函数

```cpp
// 初始化
PluginHandle* plugin = Plugin_Create("HostPlugin_12345");

// 等待命令（主循环）
while (Plugin_WaitCommand(plugin, 100)) {
    
    // 获取图像
    ImageData img;
    Plugin_GetImage(plugin, &img);
    
    // 获取参数
    char params[1024];
    Plugin_GetParams(plugin, params, sizeof(params));
    
    // 你的算法处理...
    // ...
    
    // 发送结果
    Plugin_SendResult(plugin, resultJson);
}

// 清理
Plugin_Destroy(plugin);
```

---

## 数据结构

### ImageData

```cpp
struct ImageData {
    uint8_t* pixels;    // 像素数据指针
    int width;          // 宽度
    int height;         // 高度
    int channels;       // 1=灰度, 3=RGB/BGR
};
```

### 转换为 OpenCV Mat

```cpp
cv::Mat mat(img.height, img.width,
            img.channels == 1 ? CV_8UC1 : CV_8UC3,
            img.pixels);
```

---

## JSON 格式

### 接收参数

```json
{
    "algorithm": "circle_detect",
    "threshold": 50,
    "min_radius": 30,
    "max_radius": 100
}
```

### 发送结果

**成功：**
```json
{
    "status": "ok",
    "data": {
        "circles": [
            {"x": 100, "y": 150, "radius": 50}
        ],
        "time_ms": 25
    }
}
```

**失败：**
```json
{
    "status": "error",
    "error_message": "Invalid image"
}
```

---

## 编译 (CMake)

```cmake
find_package(OpenCV 4 REQUIRED)
find_package(PluginInterface REQUIRED)

add_executable(plugin main.cpp)
target_link_libraries(plugin
    PRIVATE OpenCV::opencv_core OpenCV::opencv_imgproc
    PRIVATE PluginInterface::PluginInterface)
```

---

## 常见模式

### 模式 1: 简单处理

```cpp
while (Plugin_WaitCommand(plugin, 100)) {
    ImageData img;
    Plugin_GetImage(plugin, &img);
    
    cv::Mat mat(img.height, img.width, CV_8UC1, img.pixels);
    
    // 处理...
    auto result = MyAlgorithm(mat);
    
    Plugin_SendResult(plugin, result.toJson());
}
```

### 模式 2: 带参数处理

```cpp
while (Plugin_WaitCommand(plugin, 100)) {
    ImageData img;
    Plugin_GetImage(plugin, &img);
    
    char params[1024];
    Plugin_GetParams(plugin, params, sizeof(params));
    
    // 解析 JSON 参数（使用你的 JSON 库）
    int threshold = ParseJson(params, "threshold");
    
    cv::Mat mat(img.height, img.width, CV_8UC1, img.pixels);
    auto result = MyAlgorithm(mat, threshold);
    
    Plugin_SendResult(plugin, result.toJson());
}
```

### 模式 3: 错误处理

```cpp
while (Plugin_WaitCommand(plugin, 100)) {
    try {
        ImageData img;
        if (!Plugin_GetImage(plugin, &img)) {
            throw std::runtime_error("Failed to get image");
        }
        
        // 处理...
        auto result = MyAlgorithm(img);
        Plugin_SendResult(plugin, result.toJson());
        
    } catch (const std::exception& e) {
        char errorJson[256];
        sprintf(errorJson, R"({"status":"error","error_message":"%s"})", 
                e.what());
        Plugin_SendResult(plugin, errorJson);
    }
}
```

---

## 调试技巧

### 输出调试信息

```cpp
printf("[MyPlugin] Processing started\n");
fprintf(stderr, "[MyPlugin] Error: %s\n", message);
```

### 检查版本

```cpp
printf("SDK Version: %s\n", Plugin_GetVersion());
printf("API Version: %d\n", Plugin_GetAPIVersion());
```

### 验证缓冲区大小

```cpp
if (result_size > 4096) {
    fprintf(stderr, "Result too large: %d bytes\n", result_size);
}
```

---

## 性能数据

| 操作 | 延迟 | 注意 |
|------|------|------|
| Plugin_WaitCommand | <1ms | 命令到达时立即返回 |
| Plugin_GetImage | <1ms | 只是映射内存指针 |
| Plugin_GetParams | <1ms | 只是复制数据 |
| Plugin_SendResult | <1ms | 写入共享内存 |
| 主机接收结果 | ~100ms | 取决于主机轮询速率 |
| **总往返延迟** | **~100-200ms** | 包括你的算法处理时间 |

---

## 限制

| 项目 | 限制 |
|------|------|
| 一次最大图像 | 16 MB |
| 参数 JSON 大小 | 3840 字节 |
| 结果 JSON 大小 | 1 MB |
| 并发插件数 | 1（每个宿主） |
| 同时主机数 | 1（每个插件） |

---

## 常见错误

| 错误 | 原因 | 解决 |
|------|------|------|
| Plugin_Create 返回 NULL | 宿主未初始化 | 确保宿主进程运行 |
| WaitCommand 一直超时 | 宿主死亡 | 检查宿主日志 |
| GetImage 失败 | 无待处理命令 | 仅在 WaitCommand 返回 true 之后调用 |
| SendResult 失败 | 连接丢失 | 重启宿主或插件 |
| 结果未显示 | JSON 格式错误 | 验证 JSON 有效性 |

---

## 命令行参数

宿主启动插件时会传递：

```
plugin.exe --shm-name=HostPlugin_12345 --parent-window=0x12AB34CD
```

**解析示例：**
```cpp
const char* shmName = nullptr;
const char* parentWindow = nullptr;

for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--shm-name=", 11) == 0) {
        shmName = argv[i] + 11;
    }
    else if (strncmp(argv[i], "--parent-window=", 16) == 0) {
        parentWindow = argv[i] + 16;
    }
}
```

---

## 链接库

在你的项目中链接：

```
- PluginInterface (or libPluginInterface.a / .lib)
- OpenCV core, imgproc
- Windows kernel32, user32 (自动)
```

---

## 相关文件

| 文件 | 用途 |
|------|------|
| PLUGIN_INTEGRATION_GUIDE.md | 完整文档 |
| minimal_plugin_example.cpp | 完整示例代码 |
| PluginInterface.h | API 头文件 |
| version.h | 版本信息 |

---

快速开始：见 PLUGIN_INTEGRATION_GUIDE.md 的 "Quick Start" 部分
