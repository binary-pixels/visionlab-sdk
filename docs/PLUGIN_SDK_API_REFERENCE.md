# Plugin SDK — API Reference

Three headers, three audiences:

| Layer | Header | Purpose | Who |
|---|---|---|---|
| **Launch** | `PluginHostLauncher.h` | start / stop / embed the runtime | integrators (~90%) |
| **IPC** | `PluginHostInterface.h` | send image + params, collect results | advanced integrators (~8%) |
| **Plugin** | `PluginInterface.h` | implement the plugin side | plugin developers (~2%) |

> All handles are **pointers** (`PluginHostLauncher*`, `PluginHostHandle*`,
> `PluginHandle*`). String pointers returned in structs are valid only until the next
> `PluginHost_*` / `Plugin_*` call — copy them if you keep them.

---

## `PluginHostLauncher.h` — launcher

| Function | Args | Returns |
|---|---|---|
| `PluginHostLauncher_Create` | `pluginExePath` | `PluginHostLauncher*` (NULL on error) |
| `PluginHostLauncher_Launch` | `launcher, timeoutMs` | `bool` |
| `PluginHostLauncher_IsRunning` | `launcher` | `bool` |
| `PluginHostLauncher_Stop` | `launcher, gracefulTimeoutMs` | `bool` |
| `PluginHostLauncher_Kill` | `launcher` | `bool` |
| `PluginHostLauncher_SetUIMode` | `launcher, mode` | `bool` |
| `PluginHostLauncher_EmbedInto` | `launcher, parentHwnd, x, y, w, h` | `bool` |
| `PluginHostLauncher_GetHost` | `launcher` | `PluginHostHandle*` |
| `PluginHostLauncher_GetPID` | `launcher` | `int` |
| `PluginHostLauncher_GetWindowHandle` | `launcher` | `void*` (HWND) |
| `PluginHostLauncher_GetLastError` | `launcher` | `const char*` |
| `PluginHostLauncher_Destroy` | `launcher` | `void` |

```c
typedef enum {
    PLUGIN_UI_STANDALONE = 0,   /* own top-level window (default) */
    PLUGIN_UI_EMBEDDED   = 1,   /* child of a host window */
    PLUGIN_UI_HEADLESS   = 2    /* no UI (background processing) */
} PluginUIMode;
```

```c
/* Create */
PluginHostLauncher* launcher = PluginHostLauncher_Create("VisionLab.exe");
if (!launcher) { /* file not found / access denied */ }

/* Launch — waits while the runtime connects to shared memory */
if (!PluginHostLauncher_Launch(launcher, 5000)) {
    printf("launch failed: %s\n", PluginHostLauncher_GetLastError(launcher));
    return 1;
}

/* Get the IPC handle */
PluginHostHandle* host = PluginHostLauncher_GetHost(launcher);

/* Stop (graceful, may Kill on timeout) */
if (!PluginHostLauncher_Stop(launcher, 3000))
    PluginHostLauncher_Kill(launcher);

/* Destroy (must be called before exit) */
PluginHostLauncher_Destroy(launcher);
```

**Embed** (before Launch):

```cpp
HWND parentHwnd = (HWND)containerWidget->winId();
PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);
PluginHostLauncher_EmbedInto(launcher, parentHwnd, 100, 100, 400, 400);
```

**Typical launch errors:** `File not found`, `Permission denied`,
`Timeout waiting for plugin` (raise the timeout), `Plugin crashed`,
`IPC initialization failed`.

---

## `PluginHostInterface.h` — IPC

### Structs

```c
typedef struct {              /* input image */
    unsigned char* pixels;    /* width*height*channels bytes (gray: 1, BGR: 3) */
    int width;
    int height;
    int channels;
} PluginHostImage;

typedef struct {              /* result; pointers valid until the next call */
    int         request_id;   /* matches the request (async correlation) */
    const char* status;       /* "ok" | "error" */
    const char* data;         /* result JSON (parse with your JSON lib) */
    const char* error_message;
    int         error_code;
} PluginHostResult;

typedef struct {              /* async request */
    int              request_id;   /* 0..65535 */
    PluginHostImage  image;
    const char*      params_json;
} PluginHostAsyncRequest;
```

### Function table

| Function | Mode | Purpose |
|---|---|---|
| `PluginHost_SendAndWait` | sync | send one request and wait |
| `PluginHost_SendAsyncCommand` | async | submit without waiting |
| `PluginHost_PollResult` | async | poll one result |
| `PluginHost_GetPendingResultCount` | async | how many results are pending |
| `PluginHost_Create` / `Destroy` | init | create / destroy the handle |
| `PluginHost_IsPluginConnected` | check | connection state |
| `PluginHost_GetShmName` | info | shared-memory name |
| `PluginHost_GetLastError` | error | last error |
| `PluginHost_GetStats` | stats | performance counters |

### Sync — `SendAndWait` (most common)

```c
PluginHostImage img = { image.data, image.cols, image.rows, 1 };
PluginHostResult result;
bool ok = PluginHost_SendAndWait(host, &img, R"({"threshold":50})", &result, 5000);
if (ok && result.status && strcmp(result.status, "ok") == 0)
    printf("result: %s\n", result.data);
else
    printf("error: %s (%d)\n", result.error_message, result.error_code);
```

Single request: ~50–200 ms. Good for interactive use; for many requests the async path is
much faster.

### Async — `SendAsyncCommand` + `PollResult`

```c
for (int i = 0; i < 10; i++) {
    PluginHostAsyncRequest req = { i, { images[i].data, images[i].cols, images[i].rows, 1 },
                                   R"({"threshold":50})" };
    PluginHost_SendAsyncCommand(host, &req);      /* <1 ms each */
}
int done = 0;
while (done < 10) {
    PluginHostResult result;
    if (PluginHost_PollResult(host, &result, 100)) {
        printf("req %d done: %s\n", result.request_id, result.data);
        done++;
    }
}
```

---

## `PluginInterface.h` — plugin side

```c
typedef struct {              /* plugin-side image (borrowed) */
    unsigned char* pixels;
    int width, height, channels;   /* channels: 1 gray, 3 BGR */
} ImageData;

typedef enum {
    PLUGIN_OK = 0,
    PLUGIN_ERROR_INVALID_IMAGE = 1,
    PLUGIN_ERROR_INVALID_PARAMS = 2,
    PLUGIN_ERROR_TIMEOUT = 3,
    PLUGIN_ERROR_INTERNAL = 4,
    PLUGIN_ERROR_OUT_OF_MEMORY = 5
} PluginErrorCode;
```

Six required functions:

| Function | Signature |
|---|---|
| `Plugin_Create` | `PluginHandle* Plugin_Create(const char* shmName);` |
| `Plugin_WaitCommand` | `bool Plugin_WaitCommand(PluginHandle* handle, int timeoutMs);` |
| `Plugin_GetImage` | `bool Plugin_GetImage(PluginHandle* handle, ImageData* out);` |
| `Plugin_GetParams` | `bool Plugin_GetParams(PluginHandle* handle, char* json, int maxLen);` |
| `Plugin_SendResult` | `bool Plugin_SendResult(PluginHandle* handle, const char* resultJson);` |
| `Plugin_Destroy` | `void Plugin_Destroy(PluginHandle* handle);` |

`Plugin_SendResult` takes a **single JSON string** (the result body). Serialize the whole
result (status/measurements/overlay) into it.

```c
int main(int argc, char* argv[]) {
    const char* shmName = nullptr;
    for (int i = 1; i < argc; i++)
        if (strncmp(argv[i], "--shm-name=", 11) == 0) shmName = argv[i] + 11;
    if (!shmName) return 1;

    PluginHandle* handle = Plugin_Create(shmName);
    if (!handle) return 1;

    while (true) {
        if (!Plugin_WaitCommand(handle, 3000)) { if (should_exit) break; continue; }

        ImageData image;
        if (!Plugin_GetImage(handle, &image)) continue;

        char params[2048];
        if (!Plugin_GetParams(handle, params, sizeof(params))) continue;

        cv::Mat mat(image.height, image.width, CV_8UC1, image.pixels);
        // ... your algorithm ...
        Plugin_SendResult(handle, R"({"ok":true,"measurements":{...}})");
    }

    Plugin_Destroy(handle);
    return 0;
}
```

> Multi-camera builds also expose `Plugin_WaitCommandSlot` / `Plugin_GetImageSlot` /
> `Plugin_GetParamsSlot` / `Plugin_SendResultSlot` (one command queue per camera slot).

---

## Sync vs async

| Operation | Sync | Async | Speedup |
|---|---|---|---|
| 1 request | ~50 ms | ~50 ms | — |
| 10 requests | ~500 ms | ~55 ms | ~9× |
| 100 requests | ~5000 ms | ~550 ms | ~9× |
| UI responsiveness | blocks | non-blocking | — |

## Quick reference

```c
/* start */
launcher = PluginHostLauncher_Create("VisionLab.exe");
PluginHostLauncher_Launch(launcher, 5000);
host = PluginHostLauncher_GetHost(launcher);

/* one image */
PluginHostImage img = { gray.data, gray.cols, gray.rows, 1 };
PluginHostResult result;
PluginHost_SendAndWait(host, &img, params, &result, 5000);

/* batch */
for (...) PluginHost_SendAsyncCommand(host, &req);
while (!done) PluginHost_PollResult(host, &result, 100);

/* stop */
PluginHostLauncher_Stop(launcher, 3000);
PluginHostLauncher_Destroy(launcher);
```

## See also

- [Quick Start](PLUGIN_SDK_QUICK_START.md)
- [Integration Guide](PLUGIN_INTEGRATION_GUIDE.md)
- [Qt example](PLUGIN_SDK_QT_EXAMPLE.md) · [Win32 example](PLUGIN_SDK_WIN32_EXAMPLE.md)
- [Wire protocol](RECIPE_IPC_PROTOCOL.md)
