# Plugin SDK — Delivery & Packaging

How the **out-of-process host SDK** is delivered and how a customer integrates it. The SDK is
a **pure static library** (no third-party dependencies), so it links into your executable —
nothing extra to ship.

## SDK layout

The SDK is provided **as source** in this repository; its `CMakeLists.txt` defines the
`PluginSDK` static-library target:

```
shared/plugin_sdk/
├── CMakeLists.txt            # defines target `PluginSDK` (static library)
├── PluginHostLauncher.h/.cpp # host: process management (launch / embed / track the runtime)
├── PluginHostInterface.h/.cpp# host: IPC interface (send image + params, get the result)
├── PluginHostInterfaceExt.h  # host: multi-camera slot API
├── PluginInterface.h/.cpp    # plugin side (for writing your own runtime-side plugin)
└── SharedMemLayout.h         # V3 protocol layout (self-contained; no internal includes)
```

> The SDK **builds from source** — there is no prebuilt `.lib` in the repository. Adding it as
> a CMake subdirectory builds `PluginSDK` for you.

## Integrate (3 steps)

### 1. Add the SDK to your build

```cmake
add_subdirectory(path/to/shared/plugin_sdk)   # defines the `PluginSDK` target
target_link_libraries(MyApp PRIVATE PluginSDK)
```

(`PluginSDK` is a `STATIC` library with `PUBLIC` include dirs; on Windows it links
`user32` / `kernel32` itself.)

### 2. Include the headers

```cpp
#include "PluginHostLauncher.h"
#include "PluginHostInterface.h"
```

### 3. Minimal code

```cpp
// 1. Create the launcher (pass the runtime exe path)
PluginHostLauncher* launcher = PluginHostLauncher_Create("C:/tools/VisionLab.exe");

// 2. Launch and wait for the runtime to connect (5 s timeout)
if (!PluginHostLauncher_Launch(launcher, 5000)) {
    printf("launch failed: %s\n", PluginHostLauncher_GetLastError(launcher));
    return;
}

// 3. Get the IPC handle
PluginHostHandle* host = PluginHostLauncher_GetHost(launcher);

// 4. Send an image and wait for the result
PluginHostImage img = { pixels, width, height, channels };
PluginHostResult result{};
if (PluginHost_SendAndWait(host, &img, R"({"threshold":50})", &result, 5000))
    printf("result: %s\n", result.data);

// 5. Clean up on exit (closes the runtime process)
PluginHostLauncher_Destroy(launcher);
```

## Embedded UI mode (optional)

To embed the runtime window inside your own Qt UI container, configure it before launching:

```cpp
PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);
PluginHostLauncher_EmbedInto(launcher, (void*)containerWidget->winId(),
                             0, 0, containerWidget->width(), containerWidget->height());

// keep the window in sync with the container (e.g. a 100 ms timer)
connect(pollTimer, &QTimer::timeout, this, [this]() {
    HWND pluginHwnd = /* the runtime HWND */;
    if (!pluginHwnd) return;
    HWND container = (HWND)containerWidget->winId();
    if (GetParent(pluginHwnd) != container) SetParent(pluginHwnd, container);
    QRect r = containerWidget->rect();
    SetWindowPos(pluginHwnd, nullptr, 0, 0, r.width(), r.height(),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    pollTimer->stop();
});
pollTimer->start(100);
```

A complete reference host is in [`PLUGIN_SDK_QT_EXAMPLE.md`](PLUGIN_SDK_QT_EXAMPLE.md) and the
`Master/` sample hosts in this repository.

## Runtime deployment

| File | Purpose |
|---|---|
| `VisionLab.exe` | the runtime |
| `opencv_world*.dll` | OpenCV runtime |
| `Qt6*.dll` + plugins | Qt runtime |
| `MyApp.exe` | your app (PluginSDK is linked in) |

`PluginSDK` is a static library linked into `MyApp.exe`, so it needs no separate deployment.

### Locating the runtime exe

1. **Absolute path** (not recommended): hard-coded.
2. **Relative to your exe** (recommended):
   ```cpp
   QString pluginPath = QCoreApplication::applicationDirPath() + "/VisionLab.exe";
   PluginHostLauncher_Create(pluginPath.toUtf8().constData());
   ```
3. **User-selected** via a file dialog.

Put the runtime exe in the same directory as your host exe in production.

## FAQ

- **Platform dependencies?** Win32 only (`user32`, `kernel32`); no third-party libs. C++11+.
- **Affects my Qt version?** No — the SDK doesn't use Qt; use any (or none).
- **Detect a runtime crash?** Poll `PluginHostLauncher_IsRunning(launcher)`; `false` means it
  exited.
- **Multiple plugin instances?** Yes — each `PluginHostLauncher_Create()` uses a unique
  shared-memory name.
- **Does the runtime exit when my app closes?** `PluginHostLauncher_Destroy()` sends Shutdown
  and waits up to 500 ms, then force-terminates. Call it from `closeEvent`/`atexit` so an
  abnormal exit doesn't leave the process behind.
