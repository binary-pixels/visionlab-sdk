# Plugin SDK 客户交付指南

**版本**: 1.0  
**最后更新**: 2026-04-08

---

## 概述

本指南说明如何将视觉工具 Plugin SDK 打包并交付给客户，使客户能够在自己的应用程序中集成视觉算法工具。

---

## 打包交付步骤（内部操作）

在每次发布前，执行以下步骤将 SDK 编译并打包成客户可用的交付件。

### 第一步：编译 SDK 静态库

在项目根目录执行（需要 Visual Studio 2022 构建工具）：

```cmd
cd C:\Users\difoh\source\repos\circle-qt

cmake -B build\plugin_sdk -S shared\plugin_sdk
cmake --build build\plugin_sdk --config Release
```

编译完成后产物在：
```
build\plugin_sdk\Release\PluginSDK.lib
```

> Debug 库可选：`cmake --build build\plugin_sdk --config Debug` → `build\plugin_sdk\Debug\PluginSDKd.lib`

### 第二步：组装交付包结构

```cmd
:: 创建目录
mkdir deploy\plugin_sdk\include
mkdir deploy\plugin_sdk\lib

:: 复制头文件（三个公开头文件，不含内部实现）
copy shared\plugin_sdk\PluginHostLauncher.h     deploy\plugin_sdk\include\
copy shared\plugin_sdk\PluginHostInterface.h    deploy\plugin_sdk\include\
copy shared\plugin_sdk\PluginHostInterfaceExt.h deploy\plugin_sdk\include\

:: 复制静态库（Release 必须，Debug 可选）
copy build\plugin_sdk\Release\PluginSDK.lib     deploy\plugin_sdk\lib\
:: copy build\plugin_sdk\Debug\PluginSDKd.lib   deploy\plugin_sdk\lib\  （可选）
```

### 第三步：验证交付包（模拟客户集成）

用示例工程以预编译模式构建，完整验证客户收到交付包后的集成流程：

```cmd
:: 把 SDK 包复制到示例工程目录（模拟客户的工程结构）
xcopy /E /I deploy\plugin_sdk examples\qt_host_minimal\plugin_sdk\

:: 清除旧缓存
rmdir /s /q examples\qt_host_minimal\build

:: 以预编译模式配置（PLUGIN_SDK_PREBUILT=ON）
cmake -B examples\qt_host_minimal\build -S examples\qt_host_minimal -DPLUGIN_SDK_PREBUILT=ON -DCMAKE_PREFIX_PATH=C:\qt6\6.9.1\msvc2022_64 -DOpenCV_DIR=C:\opencv\build

:: 编译
cmake --build examples\qt_host_minimal\build --config Release
```

编译成功 = 交付包头文件和 lib 完整可用。

### 第四步：清理验证用临时文件

```cmd
rmdir /s /q examples\qt_host_minimal\plugin_sdk
rmdir /s /q examples\qt_host_minimal\build
```

> 注意：`examples\qt_host_minimal\plugin_sdk\` 和 `build\` 已加入 `.gitignore`，不会误提交。

---

## 交付件结构

```
delivery/
├── plugin_sdk/                    ← SDK 预编译包（头文件 + 静态库）
│   ├── include/
│   │   ├── PluginHostLauncher.h   ← 进程管理接口
│   │   ├── PluginHostInterface.h  ← IPC 通信接口
│   │   └── PluginHostInterfaceExt.h ← 扩展接口
│   └── lib/
│       ├── PluginSDK.lib          ← Release 静态库
│       └── PluginSDKd.lib         ← Debug 静态库
│
├── examples/
│   └── qt_host_minimal/           ← 完整可编译的 Qt 集成示例
│       ├── main.cpp
│       └── CMakeLists.txt
│
├── bin/
│   └── CircleFitTester.exe        ← 视觉工具插件本体
│
└── docs/
    ├── QUICKSTART.md              ← 5 分钟快速上手
    └── PLUGIN_HOST_DEV_GUIDE.md   ← 详细开发文档
```

---

## 客户集成步骤

### 步骤 1：将 SDK 加入项目

将 `plugin_sdk/` 目录（含 `include/` 和 `lib/`）复制到客户项目中，然后在 `CMakeLists.txt` 中：

```cmake
# 导入预编译静态库（推荐）
add_library(PluginSDK STATIC IMPORTED)
set_target_properties(PluginSDK PROPERTIES
    IMPORTED_LOCATION         "${CMAKE_CURRENT_SOURCE_DIR}/plugin_sdk/lib/PluginSDK.lib"
    IMPORTED_LOCATION_DEBUG   "${CMAKE_CURRENT_SOURCE_DIR}/plugin_sdk/lib/PluginSDKd.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/plugin_sdk/include"
)

target_link_libraries(MyApp PRIVATE PluginSDK)
```

SDK 是**纯静态库**，编译后融入客户 exe，无需额外分发 DLL。

### 步骤 2：包含头文件

```cpp
#include "PluginHostLauncher.h"
#include "PluginHostInterface.h"
```

> **注意**：SDK 源码不随交付包提供，仅提供头文件和预编译静态库。

### 步骤 3：最简集成代码

```cpp
// 1. 创建启动器（传入视觉工具 exe 路径）
PluginHostLauncher* launcher =
    PluginHostLauncher_Create("C:/tools/CircleFitTester.exe");

// 2. 启动（等待插件连接，超时 5 秒）
if (!PluginHostLauncher_Launch(launcher, 5000)) {
    printf("启动失败: %s\n", PluginHostLauncher_GetLastError(launcher));
    return;
}

// 3. 获得 IPC 通信句柄
PluginHostHandle* host = PluginHostLauncher_GetHost(launcher);

// 4. 发送图像并等待结果
PluginHostImage img = { pixels, width, height, channels };
PluginHostResult result{};
if (PluginHost_SendAndWait(host, &img, R"({"threshold":50})", &result, 5000))
    printf("结果: %s\n", result.data);

// 5. 程序退出时清理（自动关闭插件进程）
PluginHostLauncher_Destroy(launcher);
```

### 步骤 4：Qt 嵌入模式（可选）

如果客户希望将视觉工具窗口嵌入自己的 UI 容器：

```cpp
// 在 Launch 前配置嵌入
PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);
PluginHostLauncher_EmbedInto(launcher,
    (void*)containerWidget->winId(),   // 容器窗口句柄
    0, 0,
    containerWidget->width(),
    containerWidget->height());

// 启动后用 100ms 定时器保持窗口与容器同步
connect(pollTimer, &QTimer::timeout, this, [this]() {
    HWND pluginHwnd = /* 枚举找到插件 HWND */;
    if (!pluginHwnd) return;
    HWND container = (HWND)containerWidget->winId();
    if (GetParent(pluginHwnd) != container)
        SetParent(pluginHwnd, container);
    QRect r = containerWidget->rect();
    SetWindowPos(pluginHwnd, nullptr, 0, 0, r.width(), r.height(),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    pollTimer->stop();
});
pollTimer->start(100);
```

完整参考实现见 `examples/qt_host_minimal/main.cpp`。

---

## 运行时部署

### 最小运行依赖

客户在生产环境部署时，需要确保以下文件可用：

| 文件 | 说明 |
|------|------|
| `CircleFitTester.exe` | 视觉工具插件本体 |
| `opencv_worldXXX.dll` | OpenCV 运行库（插件依赖） |
| `Qt6Core.dll` 等 | Qt 运行库（如插件是 Qt 程序） |
| 客户自己的 `MyApp.exe` | 集成了 PluginSDK 静态库 |

PluginSDK 本身编译为**静态库**，已融入 `MyApp.exe`，不需要单独部署。

### 插件路径配置

有三种方式指定视觉工具 exe 路径：

**方式 1：硬编码绝对路径（不推荐）**
```cpp
PluginHostLauncher_Create("C:/Program Files/Vision/CircleFitTester.exe");
```

**方式 2：相对于自身 exe 目录（推荐）**
```cpp
QString appDir = QCoreApplication::applicationDirPath();
QString pluginPath = appDir + "/CircleFitTester.exe";
PluginHostLauncher_Create(pluginPath.toUtf8().constData());
```

**方式 3：用户通过 UI 手动选择**
```cpp
QString path = QFileDialog::getOpenFileName(this, "选择视觉工具",
    QCoreApplication::applicationDirPath(), "*.exe");
```

推荐在生产环境将插件 exe 放在与宿主 exe **同一目录**，或使用配置文件指定路径。

---

## 常见问题

**Q: PluginSDK 有哪些平台依赖？**  
A: 仅依赖 Win32 API（`user32.dll`、`kernel32.dll`），无第三方库依赖。需要 C++11 及以上编译器。

**Q: SDK 会影响客户程序的 Qt 版本吗？**  
A: 不会。SDK 本身不依赖 Qt，客户可以用任意版本的 Qt（或不用 Qt）。

**Q: 插件进程崩溃后宿主程序如何感知？**  
A: 可以用定时器监控 `PluginHostLauncher_IsRunning(launcher)`，返回 `false` 表示插件进程已退出。

**Q: 可以同时运行多个插件实例吗？**  
A: 可以，每次 `PluginHostLauncher_Create()` 生成唯一的共享内存名称，互不干扰。

**Q: 关闭宿主程序时插件进程会自动退出吗？**  
A: `PluginHostLauncher_Destroy()` 会发送 Shutdown 信号并等待插件退出（最多 500ms），超时则强制终止。程序异常退出时插件进程可能残留，建议在程序 `atexit` 或 `closeEvent` 中调用 `Destroy()`。
