# Plugin Host 开发者指南

**版本**: 3.0  
**最后更新**: 2026-04-10  
**适用范围**: `shared/plugin_sdk/` · `Master/QtHost/` · `examples/qt_host_minimal/`

---

## 目录

1. [架构概览](#1-架构概览)
2. [SDK 目录结构](#2-sdk-目录结构)
3. [插件嵌入模式](#3-插件嵌入模式)
4. [窗口嵌入原理](#4-窗口嵌入原理)
5. [快速关闭实现](#5-快速关闭实现)
6. [异步启动模式](#6-异步启动模式)
7. [IPC 通信流程](#7-ipc-通信流程)
8. [Qt 集成要点](#8-qt-集成要点)
9. [已知限制](#9-已知限制)

---

## 1. 架构概览

```
宿主进程 (Host)                          插件进程 (Plugin)
─────────────────────────────────        ──────────────────────────
  PluginHostLauncher                         PluginInterface
    ├── CreateProcess()           →  创建进程
    ├── EmbedInto() [可选]        →  传递 --parent-hwnd 参数
    └── PluginHostLauncher_Launch()

  PluginHostInterface (共享内存 IPC)
    ├── PluginHost_SetCamCount()  →  写入 MultiCamHeader.cam_count
    ├── PluginHost_SendCommandSlot() → CamSlot[i].cmd_type = PushAndSearch
    ├── PluginHost_PollResultSlot()  ← CamSlot[i].result_data
    └── PluginHost_SendShutdown() →  CamSlot[0].cmd_type = Shutdown

  Win32 窗口层 (仅 Embedded 模式)
    └── SetParent(pluginHwnd, containerHwnd)
        SetWindowPos(...)
```

### 进程间通信机制

| 层 | 技术 | 用途 |
|----|------|------|
| 控制 | Named Shared Memory (20 KB: `MultiCamHeader` + `CamSlot[4]`) | 发命令、收结果 |
| 图像 | Named Shared Memory (128 MB: `ImageSlot[4]` × 32 MB) | 传输图像帧（最多 4 路） |
| 事件 | Named Auto-Reset Events (`Event_H2P_<name>_0..3` / `Event_P2H_<name>_0..3`) | 每路独立通知 |
| 窗口 | Win32 `SetParent` / `SetWindowPos` | 嵌入 UI |

---

## 2. SDK 目录结构

```
shared/plugin_sdk/
├── CMakeLists.txt            # 静态库构建配置，自包含
├── SharedMemLayout.h         # ⭐ V3 协议定义（MultiCamHeader / CamSlot / ImageSlot）
├── PluginHostLauncher.h/.cpp # 宿主侧：进程管理、窗口嵌入、关闭
├── PluginHostInterface.h/.cpp# 宿主侧：IPC 核心（单槽兼容 API）
├── PluginHostInterfaceExt.h  # 宿主侧：多相机槽 API（SetCamCount, SendCommandSlot, PollResultSlot…）
└── PluginInterface.h/.cpp    # 插件侧：注册到宿主、接收命令
```

### 重要：SDK 自包含原则


```cmake
# ✅ 正确（当前实现）
target_include_directories(PluginSDK PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ❌ 错误（旧版，依赖主项目路径，客户无法编译）
```

---

## 3. 插件嵌入模式

SDK 支持两种 UI 模式，通过 `PluginHostLauncher_SetUIMode()` 在 Launch 前设置：

### Embedded 模式（嵌入到宿主窗口）

插件窗口通过 Win32 `SetParent` 成为宿主容器的子窗口，完全嵌入宿主 UI。

```cpp
PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);
PluginHostLauncher_EmbedInto(launcher,
    (void*)container->winId(),  // 容器 HWND
    0, 0, container->width(), container->height());
PluginHostLauncher_Launch(launcher, 5000);
```

**命令行参数（自动传递给插件进程）：**
```
plugin.exe --shm-name=HostPlugin_1234 --parent-hwnd=0x1A2B3C --embed-x=0 --embed-y=0 --embed-w=800 --embed-h=600
```

插件收到 `--parent-hwnd` 后，在启动时调用 `SetParent(self, parentHwnd)`，使自身成为子窗口。

### Standalone 模式（独立窗口）

插件作为独立顶层窗口运行，宿主仅负责进程管理和 IPC 通信。

```cpp
PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_STANDALONE);
PluginHostLauncher_Launch(launcher, 5000);
// 插件显示为单独窗口，宿主容器显示占位提示
```

### 模式切换（UI 层）

```cpp
// Qt 示例（QtHostMinimal）
void onLaunch() {
    if (isEmbedded()) {
        PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);
        PluginHostLauncher_EmbedInto(launcher, containerHwnd, 0, 0, w, h);
    } else {
        PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_STANDALONE);
    }
    // 启动后：Embedded 模式启动 pollTimer，Standalone 显示占位文字
}
```

---

## 4. 窗口嵌入原理

### 问题背景：3 秒延迟

旧版实现中，`PluginHostLauncher_Launch()` 的窗口查找逻辑使用 `EnumWindows`（仅枚举顶层窗口）。但插件收到 `--parent-hwnd` 参数后会立即调用 `SetParent`，使自身成为**子窗口**。`EnumWindows` 找不到子窗口，导致每次都等待完整的 3 秒超时。

### 修复方案：优先搜索子窗口

```cpp
// PluginHostLauncher.cpp — 窗口搜索顺序
// 1. 先在容器的已有子窗口中找（插件已 SetParent 时命中）
EnumChildWindows(l->parentHwnd, FindChildByPid, &ctx);

// 2. 找不到再搜顶层窗口（Standalone 模式或尚未 SetParent 时）
if (!ctx.hwnd)
    EnumWindows(FindWindowByPidCb, &ctx);
```

### Poll Timer 模式（宿主侧）

宿主不依赖 `PluginHostLauncher_GetWindowHandle()` 的缓存 HWND（该函数只查顶层窗口）。改用 100ms 定时器，在宿主侧自行枚举：

```cpp
// 定时器每 100ms 触发一次
void HostWindow::onRepositionPlugin() {
    HWND pluginHwnd = findPluginHwnd();  // 先子窗口，再顶层
    if (!pluginHwnd) return;

    HWND containerHwnd = (HWND)m_pluginContainer->winId();
    if (GetParent(pluginHwnd) != containerHwnd)
        SetParent(pluginHwnd, containerHwnd);

    QRect r = m_pluginContainer->rect();
    SetWindowPos(pluginHwnd, nullptr, 0, 0, r.width(), r.height(),
                 SWP_NOZORDER | SWP_NOACTIVATE);

    m_pollTimer->stop();  // 成功后停止，避免持续消耗
}
```

**效果**：窗口嵌入时间从 3s → ~100ms。

### 容器 Resize 处理

宿主需要监听容器尺寸变化（不是主窗口 resize），使用 Qt 事件过滤器：

```cpp
m_pluginContainer->installEventFilter(this);

bool eventFilter(QObject* obj, QEvent* e) override {
    if (obj == m_pluginContainer && e->type() == QEvent::Resize)
        if (m_isPluginRunning && isEmbedded())
            QTimer::singleShot(0, this, &HostWindow::repositionPlugin);
    return QMainWindow::eventFilter(obj, e);
}
```

---

## 5. 快速关闭实现

### 问题背景：慢关闭

旧版关闭流程是串行等待，最坏情况下耗时 13 秒：

```
sendShutdown() → wait(5000) → IPC thread join(5000) → Destroy(3000) = 13s
```

根本原因：
- `ResultPoller` 线程在 `WaitForSingleObject(event, 100ms)` 中阻塞
- `PluginHostLauncher_Stop()` 等待进程退出（最多 3s）
- `PluginHostLauncher_Destroy()` 内部再调一次 `Stop(3000)`

### 修复方案：Kill-First 策略

```
Kill() → [所有等待句柄立即返回] → wait threads(300ms) → Destroy(500ms)
```

**核心洞察**：`TerminateProcess()` 会使所有指向该进程的内核对象（进程句柄、事件句柄）立即变为 signaled 状态，导致所有 `WaitForSingleObject` 调用立即返回。

```cpp
void stopPlugin() {
    if (m_pollTimer) m_pollTimer->stop();

    // 1. Kill 优先 — 解除所有阻塞等待
    if (m_launcher)
        PluginHostLauncher_Kill(m_launcher);

    // 2. 等待工作线程退出（此时已无阻塞，很快）
    if (m_poller) {
        m_poller->requestStop();
        m_poller->wait(300);   // 实际 <50ms
        delete m_poller;
        m_poller = nullptr;
    }

    // 3. 清理 SDK 资源
    if (m_launcher) {
        PluginHostLauncher_Destroy(m_launcher);  // 内部 Stop(500ms)，进程已死，立即返回
        m_launcher = nullptr;
    }
}
```

### SDK 层优化

```cpp
// PluginHostLauncher_Stop：超时后自动 Kill，不返回失败
bool PluginHostLauncher_Stop(PluginHostLauncher* l, int timeoutMs) {
    PluginHost_SendShutdown(l->host);
    if (WaitForSingleObject(l->pi.hProcess, timeoutMs) != WAIT_OBJECT_0)
        TerminateProcess(l->pi.hProcess, 0);  // 超时则强杀
    return true;  // 总是成功
}

// PluginHostLauncher_Destroy：grace period 从 3000ms 缩短到 500ms
void PluginHostLauncher_Destroy(PluginHostLauncher* l) {
    if (l->launched)
        PluginHostLauncher_Stop(l, 500);  // 500ms，已 Kill 的进程立即返回
    // ... 释放句柄 ...
}
```

**效果**：关闭时间从最坏 13s → 实际 < 500ms。

---

## 6. 异步启动模式

`PluginHostLauncher_Launch()` 内部会阻塞等待插件进程连接共享内存（最长 5s）。为保持 Qt UI 响应，应在后台线程中调用：

```cpp
// LaunchWorker 运行在 QThread 中
class LaunchWorker : public QObject {
    Q_OBJECT
public slots:
    void run() {
        bool ok = PluginHostLauncher_Launch(m_launcher, 5000);
        emit done(ok, ok ? "" : PluginHostLauncher_GetLastError(m_launcher));
    }
signals:
    void done(bool ok, QString err);
};

// 主线程创建并启动
auto* worker = new LaunchWorker(m_launcher);
auto* thread = new QThread(this);
worker->moveToThread(thread);
connect(thread, &QThread::started, worker, &LaunchWorker::run);
connect(worker, &LaunchWorker::done, this, &HostWindow::onLaunchFinished);
thread->start();
```

启动成功后（`onLaunchFinished`）：
- **Embedded 模式**：启动 `m_pollTimer`，等待窗口出现
- **Standalone 模式**：显示占位提示文字

---

## 7. IPC 通信流程

### 单相机（单槽）发送搜索

```cpp
#include "PluginHostInterfaceExt.h"

// 配置 1 路相机（必须在 Launch 之前设置）
PluginHost_SetCamCount(host, 1);

// 准备图像
PluginHostImage img;
img.pixels   = gray.data;
img.width    = gray.cols;
img.height   = gray.rows;
img.channels = 1;

// 构造槽请求
PluginHostSlotRequest req;
req.slot_index  = 0;
req.request_id  = ++m_requestId;
req.image       = img;
req.params_json = R"({"algorithm":"circle_fit","parameters":{...}})";

// 发送命令（非阻塞，写入共享内存 + 触发事件）
PluginHost_SendCommandSlot(host, &req);

// 在后台线程（ResultPoller）中轮询结果
PluginHostResult result{};
if (PluginHost_PollResultSlot(host, 0, &result, 100)) {
    QString json = QString::fromUtf8(result.data);
}
```

### 多相机并发（N 路槽）

```cpp
// 启动前配置 N 路相机
PluginHost_SetCamCount(host, camCount);  // 1~4
PluginHostLauncher_Launch(launcher, 5000);

// 每路独立发送（可在同一线程顺序发，也可多线程并发）
for (int slot = 0; slot < camCount; slot++) {
    PluginHostSlotRequest req;
    req.slot_index  = slot;
    req.request_id  = nextId++;
    req.image       = images[slot];
    req.params_json = params[slot];
    PluginHost_SendCommandSlot(host, &req);
}

// ResultPoller 线程：轮询所有槽（round-robin）
for (int slot = 0; slot < camCount; slot++) {
    PluginHostResult result{};
    if (PluginHost_PollResultSlot(host, slot, &result, 20)) {
        emit resultReady(slot, QString::fromUtf8(result.data));
    }
}
```

### V3 共享内存状态机（每路槽独立）

```
host writes CamSlot[i]:            plugin reads/writes CamSlot[i]:
  cmd_type  = PushAndSearch  →       processes image[i]
  cmd_status = Pending        →       sets cmd_status = Done
  Event_H2P_<name>_i signaled →       plugin wakes
                               ←     Event_P2H_<name>_i signaled
host reads result_data         ←     host wakes
```

### 心跳保活

```cpp
// ResultPoller 线程每 500ms 更新一次
if (++ticks % 5 == 0)   // 每 5 次 × 100ms = 500ms
    PluginHost_UpdateHostHeartbeat(host);
```

---

## 8. Qt 集成要点

### 必须包含的头文件

```cpp
#include <windows.h>       // SetParent, SetWindowPos, EnumWindows 等
#include "PluginHostLauncher.h"
#include "PluginHostInterface.h"
#include "PluginHostInterfaceExt.h"
```

> ⚠️ `<windows.h>` 必须在 Qt 头文件**之前**或有条件包含（`#ifdef _WIN32`），否则部分宏可能冲突。

### CMakeLists 集成

```cmake
# 方式1：以子目录集成（推荐，SDK 源码方式）
if(NOT TARGET PluginSDK)
    add_subdirectory(path/to/shared/plugin_sdk ${CMAKE_BINARY_DIR}/plugin_sdk)
endif()
target_link_libraries(MyApp PRIVATE PluginSDK)

# 方式2：预编译库（交付给不需要改 SDK 的客户）
target_include_directories(MyApp PRIVATE path/to/sdk/include)
target_link_libraries(MyApp PRIVATE path/to/sdk/lib/PluginSDK.lib)
```

### 容器控件要求

嵌入模式下，插件窗口通过 Win32 `SetParent` 成为容器的子窗口。容器控件需满足：
- 是原生 Win32 窗口（`winId()` 返回有效 HWND）
- 有固定的屏幕位置（避免 Qt 布局引起位移）
- 不使用透明背景（否则插件窗口可能闪烁）

推荐使用 `QWidget`（不要用 `QFrame` 带边框，会偏移 1px）。

---

## 9. 已知限制

| 限制 | 原因 | 缓解方案 |
|------|------|----------|
| 仅支持 Windows | Win32 API（SetParent/CreateProcess）| 跨平台需替换为 X11/Cocoa |
| 插件必须是 Win32 GUI 程序 | 需要 HWND 才能 SetParent | Console 子进程不支持 Embedded 模式 |
| 单插件实例 | 共享内存名称基于 PID | 多实例需不同 shmName |
| 每路图像最大 32 MB（128 MB 共享块 ÷ 4 槽） | `IMG_SLOT_SIZE = 0x02000000` | 超大图像需先缩放或分块 |
| 最多 4 路并发相机 | `MAX_CAM_SLOTS = 4` | 如需更多路，修改 SharedMemLayout.h 并重新编译双端 |
| 嵌入后无法取消嵌入 | Win32 限制 | Stop → 重新以 Standalone 启动 |
