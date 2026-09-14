# Plugin ABI — In-Process Algorithm Plugins (v1)

The **in-process C ABI** is the smallest, lowest-friction way to put your own algorithm into
the VisionLab runtime: you build a DLL that exports three functions, and your algorithm then
behaves like a built-in one — it appears in the algorithm tree with an auto-generated
parameter form, and can be used as a **recipe step** whose measurements feed the
constraint/aggregate engine.

> There are two integration models. This document covers the **in-process C ABI** (your
> algorithm runs inside the runtime). The other model — your app drives the runtime as a
> black box over shared-memory IPC — is covered in
> [`PLUGIN_INTEGRATION_GUIDE.md`](PLUGIN_INTEGRATION_GUIDE.md) and
> [`RECIPE_IPC_PROTOCOL.md`](RECIPE_IPC_PROTOCOL.md).

A complete, compilable example is at
`shared/plugin_sdk/algo_plugin/examples/pin_hole/`. Build it standalone in three minutes with
[`PLUGIN_SDK_QUICK_START.md`](PLUGIN_SDK_QUICK_START.md).

---

## 1. Design principles

- **Pure C ABI + JSON contract.** No C++ objects cross the DLL boundary (`cv::Mat`,
  `QJsonObject`, STL containers are an ABI disaster across DLLs).
- **Schema reuse.** The `schema` you declare is exactly the field format the runtime's
  `ParamFormWidget` consumes → the parameter panel is generated with **zero UI changes**.
- **Result reuse.** Your `overlay` maps onto the runtime's existing `DetectionOutcome`
  fields → drawn by the existing overlay renderer.
- **Recipe reuse.** A plugin algorithm is just "algorithm key + JSON parameters", so recipe
  serialization and the constraint engine work unchanged (§7).
- **The host allocates buffers; the plugin only fills them.** Never allocate/free across the
  DLL boundary.

---

## 2. Header `circle_qt_plugin.h`

The single header you ship to customers: `shared/plugin_sdk/algo_plugin/circle_qt_plugin.h`.

```c
#define CIRCLE_QT_PLUGIN_API_VERSION 1

#ifdef _WIN32
#  ifdef CIRCLE_QT_PLUGIN_BUILD
#    define CQ_EXPORT __declspec(dllexport)
#  else
#    define CQ_EXPORT __declspec(dllimport)
#  endif
#else
#  define CQ_EXPORT __attribute__((visibility("default")))
#endif

/* Image: row-major. channels = 1 (gray) or 3 (BGR).
   Input `data` is host-owned and read-only; output `outDisplayBgr->data`
   is a host-allocated writable buffer (size it for rows*cols*3). */
typedef struct {
    unsigned char* data;
    int rows;
    int cols;
    int channels;   /* 1 or 3 */
    int step;       /* bytes per row (>= cols*channels; stride allowed) */
} CQImage;

#ifdef __cplusplus
extern "C" {
#endif

/* Version handshake: called first after load; mismatch => refuse to load. */
CQ_EXPORT int circle_qt_plugin_api_version(void);

/* Register: write the metadata JSON of all your algorithms into outJson.
   0 = ok; <0 = error; -1 if outBufSize is too small. */
CQ_EXPORT int circle_qt_plugin_register(const char* appVersion,
                                        char* outJson, int outBufSize);

/* Run: one common entry point; `algo` selects the algorithm.
   0 = ran (an NG verdict is data in the result's `ok` field);
   <0 = hard error (bad input / internal exception), reason in errBuf. */
CQ_EXPORT int circle_qt_plugin_run(const char* algo,
                                   const CQImage* img,
                                   const char* paramsJson,
                                   char* outResultJson, int outResultBufSize,
                                   CQImage* outDisplayBgr,   /* nullable */
                                   char* errBuf, int errBufSize);

#ifdef __cplusplus
}
#endif
```

**Conventions:** all strings are UTF-8; the host allocates every buffer; the plugin never
returns a pointer it allocated.

---

## 3. Register JSON (output of `circle_qt_plugin_register`)

```json
{
  "api_version": 1,
  "algorithms": [
    {
      "name": "customer_pin_hole",
      "label": "Pin hole (customer)",
      "category": "measure",
      "roi_type": "none",
      "schema": [
        {"name": "thresh_value", "label": "Threshold", "type": "int",
         "min": 1, "max": 255, "default": 100, "slider": true},
        {"name": "min_radius", "label": "Min radius", "type": "double",
         "min": 1, "max": 2000, "step": 1, "default": 5, "slider": true}
      ],
      "defaults": { "thresh_value": 100, "min_radius": 5 }
    }
  ]
}
```

| Field | Meaning |
|---|---|
| `name` | Unique algorithm key (referenced by recipes) |
| `label` | UI display name |
| `category` | Metadata: `measure` / `defect` / `other` (informational) |
| `roi_type` | Fixed enum `circle` / `ellipse` / `line` / `rect` / `tm` / `none` → maps to the runtime's `ROIType`; `none` or empty hides the ROI editor |
| `schema` | **The `ParamFormWidget` field format** (`name/label/type/min/max/step/default/slider`) — the host calls `setSchema` directly |
| `defaults` | Default parameters (nested JSON) |

`schema` field types: `int` / `double` / `bool` / `enum` (with an `options` array) / `string`
— the same set as the built-in algorithms.

---

## 4. Result JSON (output of `circle_qt_plugin_run`)

```json
{
  "ok": true,
  "status": "Pin hole diameter OK",
  "ms": 1.2,
  "measurements": {
    "radius_px": 88.5,
    "diameter_px": 177.0
  },
  "overlay": {
    "circles": [{ "cx": 320.0, "cy": 240.0, "radius": 88.5 }],
    "lines":   [{ "x1": 0, "y1": 0, "x2": 10, "y2": 10 }],
    "rects":   [{ "cx": 0, "cy": 0, "width": 100, "height": 80, "angle": 0 }]
  }
}
```

| Field | Meaning |
|---|---|
| `ok` | Algorithm verdict (OK / NG); `status` is readable text |
| `ms` | Elapsed time |
| `measurements` | Key/value pairs consumed by the constraint engine and the result panel; `points` and `line_x1/y1/x2/y2` feed cross-shot aggregation (§7.3) |
| `overlay.circles/lines/rects` | Mapped onto `DetectionOutcome`'s `hasCircle`/`hasLine`/`rectResults` → drawn with the standard overlay |
| `outDisplayBgr` (nullable) | The plugin paints a full BGR image → the host shows it (`replaceDisplayImage`). Use this for custom shapes / debug overlays |

**overlay → DetectionOutcome mapping**

| Plugin overlay | DetectionOutcome | Display |
|---|---|---|
| `circles[]` | `hasCircle` / `cx,cy,radius` | circle + crosshair |
| `lines[]` | `hasLine` / `linePt1,linePt2` | line |
| `rects[]` | `rectResults` | rotated rectangle |
| `outDisplayBgr` | `displayImg` + `replaceDisplayImage` | full-frame replace |

v1 supports circles / lines / rectangles + the full-image fallback. Custom point overlays
(`overlay.points`) are planned for v1.1.

---

## 5. Host integration

At startup the runtime scans `<exe>/plugins/*.dll|*.so`, and for each plugin:

1. loads it, resolves the three exports, and checks `circle_qt_plugin_api_version()`;
2. parses the `register` metadata, rejecting name collisions with existing algorithms;
3. adds each algorithm to the tree under a top-level **Plugins** group, with a
   schema-driven parameter form and (per `roi_type`) the appropriate ROI editor;
4. routes execution to `circle_qt_plugin_run()` for both single-image detection and recipe
   steps. In a recipe, the plugin's `measurements` are passed through unchanged, so
   constraints and aggregation consume them exactly like built-in steps.

The runtime passes the **full working image** (gray/BGR, **no ROI crop** — built-in algorithms
crop internally). If your algorithm needs a ROI, put its coordinates in your parameter JSON
and crop inside the plugin.

---

## 6. Recipe compatibility

A plugin algorithm can be used in recipes for **step execution + constraints + cross-shot
aggregation** — because a plugin step is just `{algorithm, parameters, emit, ...}` JSON, and
its `measurements` are passed through untouched.

| Recipe capability | Status |
|---|---|
| Step execution + OK/NG | ✅ |
| Constraint judging (`measurements`) | ✅ (zero changes) |
| Cross-shot aggregate point pool | ✅ mechanism ready — return `points` (or `line_*`) in `measurements` and tag the step with `emit` in the recipe; coordinates are in original-image pixels, matching the aggregate pool |

To feed aggregation, include points in your result:

```json
{
  "ok": true,
  "measurements": {
    "radius_px": 88.5,
    "points": [ { "x": 318.1, "y": 241.0 }, { "x": 322.0, "y": 239.5 } ]
  }
}
```

and give that recipe step an `emit` tag — **no new top-level field or ABI version needed**.

---

## 7. ABI stability rules

- **Frozen ABI.** `CQImage` and the three export signatures: once released, any change bumps
  `CIRCLE_QT_PLUGIN_API_VERSION` (the host rejects a mismatch). The parameter/result **JSON
  may gain fields** (forward compatible) but must not remove or change the meaning of
  existing ones.
- **Trust model.** A plugin is in-process native code; a crash takes the host down. v1 targets
  your own SDK / contracted customers. Untrusted plugins would need an out-of-process model.
- **Memory discipline.** Write only into host-provided buffers; never return plugin-allocated
  memory.
- **Image ownership.** Input `CQImage.data` is host-owned and read-only; `outDisplayBgr->data`
  is a host-allocated writable buffer (sized `rows*cols*3`); fill it and set
  rows/cols/channels/step.

---

## 8. Planned extensions (post-v1)

- `overlay.points` — plugin-defined point clouds (host adds a `pluginPoints` overlay).
- ROI-cropped images passed to plugins.
- A HALCON backend switch at the runner layer.
- An embedded-Python path with the same register/run contract.
