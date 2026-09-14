# Plugin Host Developer Guide

Scope: `shared/plugin_sdk/` · host clients in `Master/`. Build a host application that
launches the VisionLab runtime, optionally embeds its window, and exchanges images/results
over shared-memory IPC.

## Contents
1. Architecture
2. SDK layout
3. Plugin UI modes (embedded / standalone)
4. Window embedding
5. Fast shutdown
6. Async launch
7. IPC flow
8. Qt integration notes
9. Known limitations

---

## 1. Architecture

```
Host process                                   Plugin (runtime) process
──────────────────────────────                 ──────────────────────────
  PluginHostLauncher
    ├── CreateProcess()                →   spawn
    ├── EmbedInto() [optional]         →   pass --parent-hwnd
    └── PluginHostLauncher_Launch()

  PluginHostInterface (shared-memory IPC)
    ├── PluginHost_SetCamCount()       →   MultiCamHeader.cam_count
    ├── PluginHost_SendCommandSlot()   →   CamSlot[i].cmd_type
    ├── PluginHost_PollResultSlot()    ←   CamSlot[i].result_data
    └── PluginHost_SendShutdown()      →   CamSlot[0].cmd_type = Shutdown

  Win32 window layer (embedded mode only)
    └── SetParent(pluginHwnd, containerHwnd); SetWindowPos(...)
```

| Layer | Tech | Purpose |
|---|---|---|
| Control | Named shared memory (20 KB: `MultiCamHeader` + `CamSlot[4]`) | send commands, receive results |
| Image | Named shared memory (128 MB: `ImageSlot[4]` × 32 MB) | transfer frames (up to 4 cameras) |
| Events | Named auto-reset events (`Event_H2P/P2H_<name>_0..3`) | per-camera notification |
| Window | Win32 `SetParent` / `SetWindowPos` | UI embedding |

## 2. SDK layout

```
shared/plugin_sdk/
├── SharedMemLayout.h          # V3 protocol (MultiCamHeader / CamSlot / ImageSlot)
├── PluginHostLauncher.h/.cpp  # host: process management, embedding, shutdown
├── PluginHostInterface.h/.cpp # host: IPC core
├── PluginHostInterfaceExt.h   # host: multi-camera slot API
└── PluginInterface.h/.cpp     # plugin side: register + receive commands
```

**The SDK is self-contained.** `SharedMemLayout.h` must ship with the SDK and must not
depend on any internal source path. Its include dir is the SDK's own directory.

## 3. Plugin UI modes

Set before launching with `PluginHostLauncher_SetUIMode()`.

**Embedded** — the runtime window becomes a child of your container via `SetParent`:

```cpp
PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);
PluginHostLauncher_EmbedInto(launcher, (void*)container->winId(),
                             0, 0, container->width(), container->height());
PluginHostLauncher_Launch(launcher, 5000);
```

The launcher passes `--shm-name=... --parent-hwnd=0x... --embed-x/y/w/h` to the runtime,
which calls `SetParent(self, parentHwnd)` at startup.

**Standalone** — the runtime runs as its own top-level window; the host only manages the
process and IPC:

```cpp
PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_STANDALONE);
PluginHostLauncher_Launch(launcher, 5000);
```

## 4. Window embedding

The launcher searches for the plugin window by **child windows first**, then top-level
windows (`EnumChildWindows` then `EnumWindows`) — because once the runtime calls `SetParent`
it is a *child* window and `EnumWindows` alone would miss it and wait out the full timeout.

On the host side, don't rely on a cached HWND; use a short poll timer that finds the window,
re-parents it, and sizes it to the container:

```cpp
void HostWindow::onRepositionPlugin() {
    HWND pluginHwnd = findPluginHwnd();      // children first, then top-level
    if (!pluginHwnd) return;
    HWND containerHwnd = (HWND)m_pluginContainer->winId();
    if (GetParent(pluginHwnd) != containerHwnd) SetParent(pluginHwnd, containerHwnd);
    QRect r = m_pluginContainer->rect();
    SetWindowPos(pluginHwnd, nullptr, 0, 0, r.width(), r.height(),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    m_pollTimer->stop();                     // stop once positioned
}
```

Track container resizes (not the main-window resize) with a Qt event filter on the container
widget and reposition on `QEvent::Resize`.

## 5. Fast shutdown — "kill first"

A serial shutdown (`sendShutdown → wait 5s → join 5s → destroy 3s`) can take ~13 s. Killing
the process first unblocks everything: `TerminateProcess()` makes all handles to that process
signaled, so every `WaitForSingleObject` returns immediately.

```cpp
void stopPlugin() {
    if (m_pollTimer) m_pollTimer->stop();
    if (m_launcher) PluginHostLauncher_Kill(m_launcher);   // 1. unblock waits
    if (m_poller) { m_poller->requestStop(); m_poller->wait(300); delete m_poller; }  // 2.
    if (m_launcher) { PluginHostLauncher_Destroy(m_launcher); m_launcher = nullptr; } // 3.
}
```

The SDK's `Stop` auto-kills on timeout; `Destroy` uses a short grace period. Net effect:
shutdown < ~500 ms.

## 6. Async launch

`PluginHostLauncher_Launch()` blocks up to the timeout while the runtime connects, so call it
off the UI thread (e.g. a `QThread` worker) and signal back on completion. On success, start
the poll timer (embedded) or show a placeholder (standalone).

## 7. IPC flow

```cpp
#include "PluginHostInterfaceExt.h"

PluginHost_SetCamCount(host, 1);            // configure before Launch

PluginHostImage img { gray.data, gray.cols, gray.rows, 1 };
PluginHostSlotRequest req;
req.slot_index  = 0;
req.request_id  = ++m_requestId;
req.image       = img;
req.params_json = R"({"algorithm":"circle_fit","parameters":{...}})"
PluginHost_SendCommandSlot(host, &req);      // non-blocking: write + signal

PluginHostResult result{};
if (PluginHost_PollResultSlot(host, 0, &result, 100))
    QString json = QString::fromUtf8(result.data);
```

For N cameras, call `PluginHost_SetCamCount(host, N)` (1..4) before launch and send/poll each
slot independently. Keep the session alive by periodically calling
`PluginHost_UpdateHostHeartbeat(host)` (e.g. every 500 ms).

## 8. Qt integration notes

```cpp
#include <windows.h>                 // SetParent, SetWindowPos, ...
#include "PluginHostLauncher.h"
#include "PluginHostInterface.h"
#include "PluginHostInterfaceExt.h"
```

`<windows.h>` must come before Qt headers (or be guarded by `#ifdef _WIN32`) to avoid macro
clashes. In CMake, add the SDK as a subdirectory — `add_subdirectory(shared/plugin_sdk)`
defines the `PluginSDK` static-library target; then `target_link_libraries(MyApp PRIVATE
PluginSDK)`. The SDK builds from source.

The container widget must be a native window with a valid `winId()` and a stable on-screen
position; prefer a plain `QWidget` (not a bordered `QFrame`, which shifts by 1 px).

## 9. Known limitations

| Limitation | Cause | Mitigation |
|---|---|---|
| Windows only | Win32 API | port to X11/Cocoa for other platforms |
| Runtime must be a Win32 GUI app | `SetParent` needs an HWND | embedded mode needs a GUI child, not a console app |
| Single plugin instance per shmName | name is PID-based | use distinct names for multiple instances |
| ≤ 32 MB per image | `IMG_SLOT_SIZE` | downscale or tile larger images |
| ≤ 4 concurrent cameras | `MAX_CAM_SLOTS = 4` | change the layout header and rebuild both sides |
| Cannot un-embed | Win32 limitation | stop, then relaunch in standalone mode |
