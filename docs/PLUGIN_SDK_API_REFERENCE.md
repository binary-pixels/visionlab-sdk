# Plugin SDK - API 参考速查表

## 📋 文档概览

| 层级 | 头文件 | 目的 | 目标用户 |
|------|--------|------|---------|
| **启动** | PluginHostLauncher.h | 一键启动/停止/嵌入 | 集成者（90%使用） |
| **通信** | PluginHostInterface.h | 数据传输和结果收集 | 高级集成者（8%使用） |
| **插件** | PluginInterface.h | 插件开发实现 | 插件开发者（2%使用） |

---

## 🚀 PluginHostLauncher.h（启动器API）

### 核心函数表

| 函数 | 参数 | 返回 | 说明 |
|------|------|------|------|
| **Create** | `pluginExePath` | `PluginHostLauncher*` | 创建启动器实例 |
| **Launch** | `launcher, timeoutMs` | `bool` | 启动插件进程 |
| **IsRunning** | `launcher` | `bool` | 检查插件是否运行 |
| **Stop** | `launcher, gracefulTimeoutMs` | `bool` | 优雅停止 |
| **Kill** | `launcher` | `bool` | 强制杀死进程 |
| **SetUIMode** | `launcher, mode` | `bool` | 设置UI模式 |
| **EmbedInto** | `launcher, parentHwnd, x, y, w, h` | `bool` | 嵌入到父窗口 |
| **GetHost** | `launcher` | `PluginHostHandle*` | 获得通信句柄 |
| **GetPID** | `launcher` | `int` | 获得进程ID |
| **GetWindowHandle** | `launcher` | `void*` (HWND) | 获得窗口句柄 |
| **GetLastError** | `launcher` | `const char*` | 获得错误信息 |
| **Destroy** | `launcher` | `void` | 销毁启动器 |

### 枚举：PluginUIMode

```c
typedef enum {
    PLUGIN_UI_STANDALONE = 0,   // 独立窗口（默认）
    PLUGIN_UI_EMBEDDED = 1,     // 嵌入到指定父窗口
    PLUGIN_UI_HEADLESS = 2      // 无UI（纯后台处理）
} PluginUIMode;
```

### 函数详解

#### ① Create - 创建启动器

```c
PluginHostLauncher* PluginHostLauncher_Create(const char* pluginExePath);
```

| 项 | 值 |
|----|-----|
| **参数** | `pluginExePath`: 插件执行文件路径（相对或绝对） |
| **返回** | 成功返回启动器指针，失败返回NULL |
| **错误** | 文件不存在、权限不足 |
| **超时** | 无（只是创建对象） |

**示例：**
```c
PluginHostLauncher* launcher = PluginHostLauncher_Create("circle_detect.exe");
if (!launcher) {
    printf("Failed to create launcher\n");
    return -1;
}
```

**常见错误：**
```
✗ File not found: circle_detect.exe
  → 检查插件路径是否正确

✗ Permission denied
  → 检查文件权限或杀毒软件是否拦截
```

---

#### ② Launch - 启动插件进程

```c
bool PluginHostLauncher_Launch(PluginHostLauncher* launcher, int timeoutMs);
```

| 项 | 值 |
|----|-----|
| **参数** | `launcher`: 启动器指针 |
| | `timeoutMs`: 等待插件连接超时（毫秒） |
| **返回** | 成功返回 true，超时/失败返回 false |
| **等待时间** | 一般 500-2000ms 足够 |
| **注意** | 自动生成共享内存名称、事件等 |

**示例：**
```c
if (!PluginHostLauncher_Launch(launcher, 5000)) {
    printf("Launch failed: %s\n",
           PluginHostLauncher_GetLastError(launcher));
    return -1;
}
printf("✓ Plugin launched (PID: %d)\n",
       PluginHostLauncher_GetPID(launcher));
```

**性能参考：**
```
启动耗时：
  冷启动（首次）：800-1500ms
  热启动（DLL缓存）：300-800ms
  等待超时推荐：5000ms
```

---

#### ③ IsRunning - 检查运行状态

```c
bool PluginHostLauncher_IsRunning(PluginHostLauncher* launcher);
```

**示例：**
```c
// 在循环中检查插件是否仍运行
while (PluginHostLauncher_IsRunning(launcher)) {
    // 继续处理
    ProcessImage(...);
}
printf("Plugin exited\n");
```

---

#### ④ Stop - 优雅停止

```c
bool PluginHostLauncher_Stop(PluginHostLauncher* launcher, 
                             int gracefulTimeoutMs);
```

| 项 | 值 |
|----|-----|
| **效果** | 发送关闭信号，等待插件清理资源 |
| **超时** | 若插件未在时间内退出，返回 false |
| **推荐** | 3000ms（3秒）足够大多数插件 |

**示例：**
```c
// 优先使用 Stop（给插件清理的机会）
if (!PluginHostLauncher_Stop(launcher, 3000)) {
    printf("Plugin didn't stop gracefully, force killing...\n");
    PluginHostLauncher_Kill(launcher);
}
```

---

#### ⑤ Kill - 强制杀死

```c
bool PluginHostLauncher_Kill(PluginHostLauncher* launcher);
```

**示例：**
```c
// 只在 Stop 失败或超时时使用
if (!PluginHostLauncher_Stop(launcher, 3000)) {
    PluginHostLauncher_Kill(launcher);
}
```

---

#### ⑥ SetUIMode - 设置UI模式

```c
bool PluginHostLauncher_SetUIMode(PluginHostLauncher* launcher,
                                   PluginUIMode mode);
```

| 模式 | 场景 |
|------|------|
| **PLUGIN_UI_STANDALONE** | 插件显示独立窗口（默认） |
| **PLUGIN_UI_EMBEDDED** | 嵌入到主应用窗口 |
| **PLUGIN_UI_HEADLESS** | 无UI，纯命令行/后台处理 |

**示例：**
```c
// Qt 应用中嵌入
PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);

// 批处理，无需UI
PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_HEADLESS);

// 独立运行（跳过此函数即为默认）
```

---

#### ⑦ EmbedInto - 嵌入到父窗口

```c
bool PluginHostLauncher_EmbedInto(PluginHostLauncher* launcher,
                                  void* parentHwnd,
                                  int x, int y, int width, int height);
```

| 参数 | 说明 |
|------|------|
| **parentHwnd** | 父窗口句柄（HWND转为void*） |
| **x, y** | 相对父窗口的位置 |
| **width, height** | 嵌入子窗口的尺寸 |

**示例：**

*Qt中：*
```cpp
HWND parentHwnd = (HWND)this->winId();  // Qt窗口转HWND
PluginHostLauncher_EmbedInto(launcher, parentHwnd, 
                            100, 100, 400, 400);
```

*Win32中：*
```c
HWND hParent = GetDlgItem(hDlg, IDC_PLUGIN_CONTAINER);
PluginHostLauncher_EmbedInto(launcher, (void*)hParent,
                            0, 0, 400, 300);
```

---

#### ⑧ GetHost - 获得通信句柄

```c
PluginHostHandle* PluginHostLauncher_GetHost(PluginHostLauncher* launcher);
```

**重要：** 使用此句柄调用 `PluginHost_*` 系列函数

**示例：**
```c
PluginHostHandle* host = PluginHostLauncher_GetHost(launcher);
if (!host) {
    printf("Failed to get host handle\n");
    return -1;
}

// 现在可以发送命令
PluginHost_SendAndWait(host, &image, params, &result, timeout);
```

---

#### ⑨ GetPID - 获得进程ID

```c
int PluginHostLauncher_GetPID(PluginHostLauncher* launcher);
```

**用途：** 日志、监控、调试

**示例：**
```c
printf("Plugin running as PID: %d\n",
       PluginHostLauncher_GetPID(launcher));
```

---

#### ⑩ GetWindowHandle - 获得窗口句柄

```c
void* PluginHostLauncher_GetWindowHandle(PluginHostLauncher* launcher);
```

**返回：** 窗口HWND（void*格式）或NULL

**用途：** 高级窗口操作（ShowWindow, SetFocus等）

---

#### ⑪ GetLastError - 错误信息

```c
const char* PluginHostLauncher_GetLastError(PluginHostLauncher* launcher);
```

**示例：**
```c
if (!PluginHostLauncher_Launch(launcher, 5000)) {
    printf("Error: %s\n",
           PluginHostLauncher_GetLastError(launcher));
}
```

**常见错误消息：**
```
"File not found"            → 插件可执行文件不存在
"Permission denied"         → 权限不足或被拦截
"Timeout waiting for plugin" → 插件启动太慢，增加超时
"Plugin crashed"            → 插件异常退出
"IPC initialization failed" → 共享内存创建失败
```

---

#### ⑫ Destroy - 销毁启动器

```c
void PluginHostLauncher_Destroy(PluginHostLauncher* launcher);
```

**重要：** 必须在程序退出前调用，否则会泄漏资源

**示例：**
```c
// 清理
PluginHostLauncher_Stop(launcher, 3000);
PluginHostLauncher_Destroy(launcher);
launcher = NULL;
```

---

## 💬 PluginHostInterface.h（通信API）

### 数据结构

#### PluginHostImage - 图像数据

```c
typedef struct {
    unsigned char* pixels;  // 像素数据指针（灰度：1通道，彩色：3通道BGR）
    int width;              // 宽度（像素）
    int height;             // 高度（像素）
    int channels;           // 通道数（1或3）
} PluginHostImage;
```

**内存注意：** `pixels` 必须有效，大小 = `width * height * channels`

**示例：**
```c
// 灰度图
cv::Mat gray = cv::imread("image.jpg", cv::IMREAD_GRAYSCALE);
PluginHostImage img = {
    gray.data,
    gray.cols,
    gray.rows,
    1  // 灰度
};

// 彩色图
cv::Mat color = cv::imread("image.jpg");
PluginHostImage img = {
    color.data,
    color.cols,
    color.rows,
    3  // BGR彩色
};
```

#### PluginHostResult - 结果数据

```c
typedef struct {
    int request_id;                  // 异步模式下的请求ID
    char status[64];                 // "ok" 或 "error"
    char data[4096];                 // 结果数据（JSON或文本）
    char error_message[512];         // 错误信息
    int error_code;                  // 错误代码
} PluginHostResult;
```

**检查结果：**
```c
PluginHostResult result;
if (PluginHost_SendAndWait(host, &img, params, &result, timeout)) {
    if (strcmp(result.status, "ok") == 0) {
        // 成功
        printf("Result: %s\n", result.data);
    } else {
        // 失败
        printf("Error: %s (code=%d)\n",
               result.error_message, result.error_code);
    }
}
```

#### PluginHostAsyncRequest - 异步请求

```c
typedef struct {
    int request_id;                  // 请求标识（0-65535）
    PluginHostImage image;           // 输入图像
    const char* params_json;         // 参数JSON
} PluginHostAsyncRequest;
```

**示例：**
```c
PluginHostAsyncRequest req = {
    .request_id = 42,
    .image = {pixels, width, height, channels},
    .params_json = R"({"threshold": 50})"
};

PluginHost_SendAsyncCommand(host, &req);
```

### 核心函数表

| 函数 | 模式 | 用途 |
|------|------|------|
| **SendAndWait** | 同步 | 发送+等待单个请求 |
| **SendAsyncCommand** | 异步 | 快速提交请求（不等待） |
| **PollResult** | 异步 | 轮询单个结果 |
| **GetPendingResultCount** | 异步 | 检查待处理结果数量 |
| **Create** | 初始化 | 创建通信句柄 |
| **Destroy** | 清理 | 销毁通信句柄 |
| **IsPluginConnected** | 检查 | 检查连接状态 |
| **GetShmName** | 信息 | 获得共享内存名称 |
| **GetLastError** | 错误 | 获得错误信息 |
| **GetStats** | 统计 | 获得性能统计 |

### 函数详解

#### SendAndWait - 同步模式（最常用）

```c
bool PluginHost_SendAndWait(PluginHostHandle* host,
                            const PluginHostImage* image,
                            const char* params_json,
                            PluginHostResult* result,
                            int timeoutMs);
```

| 项 | 值 |
|----|-----|
| **模式** | 阻塞式，发送+等待 |
| **优点** | 简单易用，结果肯定有序 |
| **缺点** | 对于多请求较慢（N个请求 = N倍等待时间） |
| **性能** | 单个请求：50-200ms |

**示例：**
```c
cv::Mat image = cv::imread("test.jpg", cv::IMREAD_GRAYSCALE);
PluginHostImage img = {image.data, image.cols, image.rows, 1};

PluginHostResult result;
bool ok = PluginHost_SendAndWait(
    host,
    &img,
    R"({"threshold": 50})",
    &result,
    5000  // 5秒超时
);

if (ok && strcmp(result.status, "ok") == 0) {
    printf("✓ Result: %s\n", result.data);
} else {
    printf("✗ Error: %s\n", result.error_message);
}
```

---

#### SendAsyncCommand - 异步快速提交

```c
bool PluginHost_SendAsyncCommand(PluginHostHandle* host,
                                 const PluginHostAsyncRequest* request);
```

| 项 | 值 |
|----|-----|
| **耗时** | <1ms（直接写入共享内存） |
| **返回** | 立即返回 |
| **用途** | 快速批量提交 |

**示例：**
```c
// 快速提交 10 个请求
for (int i = 0; i < 10; i++) {
    cv::Mat image = images[i];
    PluginHostAsyncRequest req = {
        .request_id = i,
        .image = {image.data, image.cols, image.rows, 1},
        .params_json = R"({"threshold": 50})"
    };
    PluginHost_SendAsyncCommand(host, &req);
}

// 总耗时：<10ms（比同步快 50+ 倍）
```

---

#### PollResult - 异步结果轮询

```c
bool PluginHost_PollResult(PluginHostHandle* host,
                           PluginHostResult* result,
                           int timeoutMs);
```

| 项 | 值 |
|----|-----|
| **返回** | 有结果返回true，超时返回false |
| **耗时** | <1ms |
| **用法** | 在循环中调用，不断收集结果 |

**示例：**
```c
int completed = 0;
while (completed < 10) {
    PluginHostResult result;
    
    // 尝试收结果，最多等待100ms
    if (PluginHost_PollResult(host, &result, 100)) {
        printf("✓ Request %d done: %s\n",
               result.request_id, result.data);
        completed++;
    } else {
        // 超时（可能还在处理），继续轮询
        printf("  (还在处理...)\n");
    }
}
```

---

#### GetPendingResultCount - 待处理数量

```c
int PluginHost_GetPendingResultCount(PluginHostHandle* host);
```

**用途：** 检查还有多少结果未收取

**示例：**
```c
// 再等等，还有待处理的
if (PluginHost_GetPendingResultCount(host) > 0) {
    printf("Still processing %d requests\n",
           PluginHost_GetPendingResultCount(host));
}
```

---

## 🔌 PluginInterface.h（插件开发API）

### 核心数据结构

#### ImageData - 图像数据（插件侧）

```c
typedef struct {
    unsigned char* pixels;   // 像素缓冲
    int width;               // 宽度
    int height;              // 高度
    int channels;            // 通道数（1或3，BGR排列）
} ImageData;
```

#### 错误码枚举

```c
typedef enum {
    PLUGIN_OK = 0,
    PLUGIN_ERROR_INVALID_IMAGE = 1,
    PLUGIN_ERROR_INVALID_PARAMS = 2,
    PLUGIN_ERROR_TIMEOUT = 3,
    PLUGIN_ERROR_INTERNAL = 4,
    PLUGIN_ERROR_OUT_OF_MEMORY = 5
} PluginErrorCode;
```

### 6个核心函数

| 函数 | 必需 | 说明 |
|------|------|------|
| **Plugin_Create** | ✓ | 初始化插件，连接IPC |
| **Plugin_WaitCommand** | ✓ | 阻塞等待命令 |
| **Plugin_GetImage** | ✓ | 提取图像数据 |
| **Plugin_GetParams** | ✓ | 提取参数JSON |
| **Plugin_SendResult** | ✓ | 发送结果回主程序 |
| **Plugin_Destroy** | ✓ | 清理资源 |

### 函数详解

#### Plugin_Create - 初始化

```c
PluginHandle Plugin_Create(const char* shmName);
```

| 项 | 值 |
|----|-----|
| **参数** | `shmName`: 共享内存名称（由启动器提供） |
| **返回** | 有效句柄或NULL |
| **典型** | 从命令行参数中获取 |

**示例：**
```c
// main.cpp
int main(int argc, char* argv[]) {
    // 从 --shm-name=xxx 参数获取
    const char* shmName = NULL;
    for (int i = 0; i < argc; i++) {
        if (strncmp(argv[i], "--shm-name=", 11) == 0) {
            shmName = argv[i] + 11;
            break;
        }
    }

    if (!shmName) {
        printf("Usage: plugin.exe --shm-name=<name>\n");
        return 1;
    }

    PluginHandle handle = Plugin_Create(shmName);
    if (!handle) {
        printf("Failed to initialize\n");
        return 1;
    }

    // 现在可以接收命令
    //...
    Plugin_Destroy(handle);
    return 0;
}
```

---

#### Plugin_WaitCommand - 等待命令

```c
bool Plugin_WaitCommand(PluginHandle handle, int timeoutMs);
```

| 项 | 值 |
|----|-----|
| **作用** | 阻塞直到收到新命令 |
| **返回** | 收到命令返回true，超时返回false |
| **超时** | 可用于定期检查（如收到退出信号） |

**示例：**
```c
while (true) {
    // 等待下一个命令（超时3秒）
    if (!Plugin_WaitCommand(handle, 3000)) {
        printf("Timeout, checking shutdown flag...\n");
        if (should_exit) break;
        continue;
    }

    // 有新命令，开始处理
    ImageData image;
    if (!Plugin_GetImage(handle, &image)) {
        printf("Failed to get image\n");
        continue;
    }

    // 处理图像...
}
```

---

#### Plugin_GetImage / Plugin_GetParams

```c
bool Plugin_GetImage(PluginHandle handle, ImageData* image);
bool Plugin_GetParams(PluginHandle handle, char* jsonBuffer, int maxLen);
```

**示例：**
```c
ImageData image;
if (!Plugin_GetImage(handle, &image)) {
    fprintf(stderr, "Failed to get image\n");
    // 发送错误
    Plugin_SendResult(handle, "error", 
                     "Failed to extract image data");
    continue;
}

// 参数
char params[512];
if (!Plugin_GetParams(handle, params, sizeof(params))) {
    fprintf(stderr, "Failed to get params\n");
    Plugin_SendResult(handle, "error", "Failed to get parameters");
    continue;
}

// 解析JSON参数（使用json库）
int threshold = 50;
sscanf(params, R"({"threshold": %d})", &threshold);

// 处理图像...
cv::Mat mat(image.height, image.width, CV_8U, image.pixels);
std::vector<cv::Vec3f> circles;
cv::HoughCircles(mat, circles, cv::HOUGH_GRADIENT, 1,
                 mat.rows/8, 100, threshold);

// 生成结果JSON
char result[512];
sprintf(result, R"({"circles": %d})", (int)circles.size());

// 发送回去
Plugin_SendResult(handle, "ok", result);
```

---

#### Plugin_SendResult - 发送结果

```c
bool Plugin_SendResult(PluginHandle handle,
                      const char* status,
                      const char* data_or_error);
```

| 参数 | 说明 |
|------|------|
| **status** | "ok" 或 "error" |
| **data_or_error** | 成功时为结果JSON，失败时为错误信息 |

**示例：**
```c
// 成功
if (circles.size() > 0) {
    char result[512];
    sprintf(result, R"({"circles": %d, "found": true})",
           (int)circles.size());
    Plugin_SendResult(handle, "ok", result);
}

// 失败
if (image.pixels == NULL) {
    Plugin_SendResult(handle, "error", 
                     "Image data is null");
}
```

---

#### Plugin_Destroy - 清理

```c
void Plugin_Destroy(PluginHandle handle);
```

**示例：**
```c
// 退出循环前清理
Plugin_Destroy(handle);
printf("Plugin cleaned up\n");
return 0;
```

---

## 📊 性能对比表

| 操作 | 同步模式 | 异步模式 | 改进 |
|------|---------|---------|------|
| 单个请求 | 50ms | 50ms | 无差异 |
| 10个请求 | 500ms | 55ms | **9.1倍** |
| 100个请求 | 5000ms | 550ms | **9.1倍** |
| UI响应 | 阻塞 | 非阻塞 | 显著 |

---

## ⚡ 快速参考速查

### 最常用的4个函数

1. **启动插件**
   ```c
   launcher = PluginHostLauncher_Create("plugin.exe");
   PluginHostLauncher_Launch(launcher, 5000);
   host = PluginHostLauncher_GetHost(launcher);
   ```

2. **单个图像处理**
   ```c
   PluginHostImage img = {...};
   PluginHostResult result;
   PluginHost_SendAndWait(host, &img, params, &result, timeout);
   ```

3. **批量异步处理**
   ```c
   for (...) PluginHost_SendAsyncCommand(host, &req);
   while (!done) PluginHost_PollResult(host, &result, timeout);
   ```

4. **清理**
   ```c
   PluginHostLauncher_Stop(launcher, 3000);
   PluginHostLauncher_Destroy(launcher);
   ```

### 错误检查模板

```c
// 启动阶段
if (!launcher || !PluginHostLauncher_Launch(launcher, 5000)) {
    error: %s\n", PluginHostLauncher_GetLastError(launcher));
    return 1;
}

// 通信阶段
if (!PluginHost_SendAndWait(host, &img, params, &result, timeout)) {
    printf("Error: %s (code=%d)\n",
           result.error_message, result.error_code);
    return 1;
}

// 清理阶段
if (PluginHostLauncher_IsRunning(launcher)) {
    PluginHostLauncher_Stop(launcher, 3000);
}
PluginHostLauncher_Destroy(launcher);
```

---

## 🔗 完整文档链接

- [快速开始（5分钟）](PLUGIN_SDK_QUICK_START.md) - 新手必读
- [Qt集成示例](PLUGIN_SDK_QT_EXAMPLE.md) - 完整Qt应用
- [Win32集成示例](PLUGIN_SDK_WIN32_EXAMPLE.md) - 原生Windows
- [FAQ常见问题](PLUGIN_SDK_FAQ.md) - 问题排查
- [性能优化指南](PLUGIN_SDK_PERFORMANCE.md) - 进阶配置
