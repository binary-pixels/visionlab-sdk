# Plugin SDK — Delivery & Packaging

How the **out-of-process host SDK** is packaged and how a customer integrates it. The SDK is
a **pure static library** (no third-party dependencies), so it links into your executable —
nothing extra to ship.

## SDK package layout

```
plugin_sdk/
├── include/
│   ├── PluginHostLauncher.h     # process management (launch/embed/track the runtime)
│   ├── PluginHostInterface.h    # IPC interface (send image + params, get result)
│   └── PluginHostInterfaceExt.h # extended interface
└── lib/
    ├── PluginSDK.lib            # Release static library
    └── PluginSDKd.lib           # Debug static library (optional)
```

The SDK is delivered as **headers + a prebuilt static library only** — its source is not
included.

## Integrate (3 steps)

### 1. Add the SDK to your build

Copy `plugin_sdk/` into your project and, in `CMakeLists.txt`:

```cmake
add_library(PluginSDK STATIC IMPORTED)
set_target_properties(PluginSDK PROPERTIES
    IMPORTED_LOCATION         "${CMAKE_CURRENT_SOURCE_DIR}/plugin_sdk/lib/PluginSDK.lib"
    IMPORTED_LOCATION_DEBUG   "${CMAKE_CURRENT_SOURCE_DIR}/plugin_sdk/lib/PluginSDKd.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/plugin_sdk/include"
)
target_link_libraries(MyApp PRIVATE PluginSDK)
```

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

A complete reference is in the `qt_host_minimal` example.

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
