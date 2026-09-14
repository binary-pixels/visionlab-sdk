# Plugin Integration — Quick Reference

A one-page cheat sheet for the **out-of-process** plugin model (your process, communicating
with the runtime over shared memory). Full details:
[`PLUGIN_INTEGRATION_GUIDE.md`](PLUGIN_INTEGRATION_GUIDE.md) and
[`RECIPE_IPC_PROTOCOL.md`](RECIPE_IPC_PROTOCOL.md).

## The core loop

```cpp
PluginHandle* plugin = Plugin_Create("HostPlugin_12345");   // connect to the host

while (Plugin_WaitCommand(plugin, 100)) {                   // wait <=100 ms for a command
    ImageData img;
    Plugin_GetImage(plugin, &img);                          // borrow the image (no copy)

    char params[2048];
    Plugin_GetParams(plugin, params, sizeof(params));       // the command's params JSON

    // ... your algorithm ...

    Plugin_SendResult(plugin, resultJson);                  // publish the result
}

Plugin_Destroy(plugin);
```

## Data structures

```cpp
struct ImageData {
    uint8_t* pixels;   // pixel data
    int width;
    int height;
    int channels;      // 1 = gray, 3 = BGR
};
```

```cpp
cv::Mat mat(img.height, img.width,
            img.channels == 1 ? CV_8UC1 : CV_8UC3,
            img.pixels);   // borrows the buffer — do not outlive the command
```

## JSON

**Incoming params**

```json
{ "algorithm": "circle_detect", "threshold": 50, "min_radius": 30, "max_radius": 100 }
```

**Result — success**

```json
{ "status": "ok",
  "data": { "circles": [ {"x":100,"y":150,"radius":50} ], "time_ms": 25 } }
```

**Result — error**

```json
{ "status": "error", "error_message": "Invalid image" }
```

## Build (CMake)

```cmake
find_package(OpenCV 4 REQUIRED)
find_package(PluginInterface REQUIRED)
add_executable(plugin main.cpp)
target_link_libraries(plugin PRIVATE
    OpenCV::opencv_core OpenCV::opencv_imgproc
    PluginInterface::PluginInterface)
```

## Common patterns

```cpp
// Simple
while (Plugin_WaitCommand(plugin, 100)) {
    ImageData img; Plugin_GetImage(plugin, &img);
    cv::Mat mat(img.height, img.width, CV_8UC1, img.pixels);
    Plugin_SendResult(plugin, MyAlgorithm(mat).toJson());
}

// With parameters
while (Plugin_WaitCommand(plugin, 100)) {
    ImageData img; Plugin_GetImage(plugin, &img);
    char params[2048]; Plugin_GetParams(plugin, params, sizeof(params));
    int threshold = ParseJson(params, "threshold");
    cv::Mat mat(img.height, img.width, CV_8UC1, img.pixels);
    Plugin_SendResult(plugin, MyAlgorithm(mat, threshold).toJson());
}

// With error handling
while (Plugin_WaitCommand(plugin, 100)) {
    try {
        ImageData img;
        if (!Plugin_GetImage(plugin, &img)) throw std::runtime_error("Failed to get image");
        Plugin_SendResult(plugin, MyAlgorithm(img).toJson());
    } catch (const std::exception& e) {
        char err[256];
        sprintf(err, R"({"status":"error","error_message":"%s"})", e.what());
        Plugin_SendResult(plugin, err);
    }
}
```

## Debugging

```cpp
printf("[MyPlugin] processing\n");
fprintf(stderr, "[MyPlugin] error: %s\n", message);
printf("SDK %s  API %d\n", Plugin_GetVersion(), Plugin_GetAPIVersion());
```

## Sizes and limits (shared-memory V3)

| Item | Limit |
|---|---|
| Image | **32 MB per slot** (4 camera slots, 128 MB image block) |
| Params JSON | **2012 bytes** (`params_data`, NUL-terminated) |
| Result JSON | **2012 bytes** (`result_data`); larger results are truncated/degraded |
| Camera slots | 4 |

> The 2 KB param/result budget is why large recipes are sent as
> `{"recipe_file":"<path>"}` (see the integration guide). The image buffer is borrowed and
> valid only until the next `Plugin_WaitCommand` / `Plugin_SendResult`.

## Latency

| Operation | Latency | Note |
|---|---|---|
| `Plugin_WaitCommand` | <1 ms | returns immediately when a command arrives |
| `Plugin_GetImage` / `GetParams` | <1 ms | just maps/copies |
| `Plugin_SendResult` | <1 ms | writes shared memory |
| Host pickup | ~100 ms | depends on host polling |
| **Round trip** | **~100–200 ms** | plus your algorithm time |

## Common errors

| Symptom | Cause | Fix |
|---|---|---|
| `Plugin_Create` returns NULL | host not running | start the host process |
| `WaitCommand` always times out | host died | check the host |
| `GetImage` fails | no pending command | call only after `WaitCommand` returns true |
| `SendResult` fails | connection lost | restart host/plugin |
| Result not shown | malformed JSON | validate the JSON |

## Command-line

The host launches the plugin process with:

```
plugin.exe --shm-name=HostPlugin_12345 --parent-window=0x12AB34CD
```

```cpp
const char* shmName = nullptr;
const char* parentWindow = nullptr;
for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--shm-name=", 11) == 0)          shmName = argv[i] + 11;
    else if (strncmp(argv[i], "--parent-window=", 16) == 0) parentWindow = argv[i] + 16;
}
```

## Link

- `PluginInterface` (`lib` / `.a`)
- OpenCV `core`, `imgproc`
- Windows `kernel32`, `user32` (automatic)

## Related

| File | Purpose |
|---|---|
| `PLUGIN_INTEGRATION_GUIDE.md` | full integration guide |
| `PLUGIN_SDK_API_REFERENCE.md` | API reference |
| `PluginInterface.h` | the header |
| `RECIPE_IPC_PROTOCOL.md` | the wire protocol |
