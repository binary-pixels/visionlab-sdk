# VisionLab Integration Guide

Two ways to put your own logic into, or drive, the VisionLab runtime. Pick by where you
want your code to live:

| **A. In-process plugin** | **B. Out-of-process host** |
|---|---|
| Your algorithm is a DLL loaded by the runtime | Your app launches the runtime and talks over IPC |
| Runs inside the runtime; appears in the UI + recipes | Runtime is a black-box "vision brain" |
| C ABI (`circle_qt_plugin.h`) | Shared-memory IPC (`SharedMemLayout.h`) + host SDKs |
| See [Plugin Quick Start](PLUGIN_SDK_QUICK_START.md) | This document, Part B |

---

# Part A — In-process algorithm plugin (C ABI)

A DLL exporting three functions; the runtime loads it and your algorithm participates in the
algorithm registry, the parameter UI, and the recipe system.

- Header: `shared/plugin_sdk/algo_plugin/circle_qt_plugin.h`
- Full walkthrough + build + gotchas: [`PLUGIN_SDK_QUICK_START.md`](PLUGIN_SDK_QUICK_START.md)
- ABI reference: [`plugin_abi.md`](plugin_abi.md)
- Example: `shared/plugin_sdk/algo_plugin/examples/pin_hole/`

Contract summary: `circle_qt_plugin_api_version()` for the handshake,
`circle_qt_plugin_register()` returns your algorithm metadata (name / label / roi_type /
parameter `schema`), and `circle_qt_plugin_run()` maps `paramsJson` → a result JSON with
`measurements` (consumed by constraints/aggregates) and `overlay` (drawn by the host).

---

# Part B — Out-of-process host (shared-memory IPC)

Your application drives the runtime through a shared-memory command channel. Reference host
implementations live in `Master/` (C++ / C# / Python).

## B.1 Shared-memory layout (V3)

```
Control block  SharedMem_Cmd_<name>   20 KB
  [0]      MultiCamHeader  4 KB          (version, cam_count, host/plugin heartbeat)
  [4096]   CamSlot[0..3]   4 KB each     (commands + params + result)
Image block    SharedMem_Img_<name>   128 MB
  ImageSlot[i] = i * 32 MB   (64-byte ImageHeader + pixels)
Events         Event_H2P_<name>_0..3 / Event_P2H_<name>_0..3
```

`CamSlot` fields: `cmd_id`, `cmd_type`, `cmd_status`, `params_len`, `params_data[2012]`,
`result_len`, `result_data[2012]`.

> **2 KB is a hard limit.** The recipe or parameters go inline in `params_data` (≤ 2012 B);
> the result comes back in `result_data` (≤ 2012 B, see truncation below).

`writer_seq` (a seqlock) lets the plugin reject torn reads; a host that sets it also gets
duplicate-wakeup protection.

## B.2 Command types

| Value | Command | Purpose |
|---|---|---|
| 1 | `PushImage` | Write image + params |
| 2 | `LoadImageFile` | params = `{"path": "..."}` |
| 3 | `Search` | Run an algorithm on the current image + params |
| 4 | `PushAndSearch` | Push an image + one algorithm's params; result carries `semantic` |
| 7 | `GetParams` | Return current params |
| 9 | `Shutdown` | Exit |
| 10 | `RunRecipe` | Whole recipe: images in slots 0..N-1 + recipe JSON |
| 11 | `StartRecipe` | Streaming session: params = full recipe JSON |
| 12 | `PushShot` | Streaming: params = `{"shot_index": k}` + image |
| 13 | `FinishRecipe` | Streaming: aggregate staged shots → full semantic JSON |

## B.3 The three ways to run a recipe

**1. `PushAndSearch` (single algorithm).** One image + one algorithm's params. Result JSON
carries `{"cmd_id","status","detail","semantic"}`. Use it for live tuning/teaching.

**2. `RunRecipe` (whole recipe, ≤ 4 inline images).** Write the images into slots 0..N-1 and
the recipe JSON into `params_data`, send one `RunRecipe`. Best for ≤ 4 camera positions where
you have all images up front.

**3. Streaming session (unlimited shots, overlaps motion).**

```
StartRecipe(recipeJson)              -> plugin caches the recipe, resets staging
                                       reply {"status":"ok","shots":N}
for k in 0..N-1:
    move stage / capture
    PushShot(image, {"shot_index":k}) -> plugin runs shot k immediately; reply shotAcked(k)
FinishRecipe()                       -> aggregate all shots; reply full semantic JSON
```

Streaming uses **one** image slot (each `PushShot` overwrites slot 0), so it is not limited
to 4 shots, and shot k+1 can be captured while shot k is still being computed.

## B.4 Payload encoding

- `PushShot`/`RunRecipe` params are **JSON**. The recipe itself is JSON (Part C).
- **Large recipes**: `params_data` is only ~2 KB. If the recipe JSON exceeds ~2 KB it will be
  **truncated**. Send a file path instead:
  `{"recipe_file": "C:/path/to/recipe.json"}` — the plugin reads the file (same machine).
- **Recipe caching (production)**: `StartRecipe` with a recipe caches it. Later cycles may
  send an empty `params` (or `{"recipe_id":...}`) to **reuse the cache**, so you don't resend
  the recipe every cycle. Re-send when the recipe changes or after a plugin restart.
- **Result truncation**: the result must fit ~2 KB. When it doesn't, the plugin returns a
  degraded `{"truncated":true, "reason":...}` form (per-step verdicts + constraints + strings,
  progressively dropped). Full detail is still written to SQLite on the plugin side.

## B.5 Teaching metadata

During teaching, `PushAndSearch` params may carry extra top-level fields (they don't break
`algorithm`/`parameters`):

```jsonc
{
  "algorithm": "line_fit",
  "parameters": { ... },
  "position": {"x": 123.5, "y": 45.2},   // machine coords of the capture position
  "shot": 0,                              // photo-point (shot) index
  "camera": 1,                            // teaching source camera (slot)
  "light": {"program": 2, "intensity": 80} // lighting used
}
```

The plugin remembers them; when the operator adds a step, that step's shot gets the
`position` / `camera` / `light`. In production these are ignored (images arrive over IPC).

## B.6 Host SDK snippets

```cpp
// C++ — Master/QtHost/IpcClient.h
IpcClient::sendPushAndSearch(image, paramsJson);
IpcClient::sendRunRecipe(images, recipeJson);            // <= 4 images
IpcClient::sendStartRecipe(recipeJson);                  // streaming: then...
IpcClient::sendPushShot(image, k);                       //   ...per shot...
IpcClient::sendFinishRecipe();                           //   ...then finish
```

```python
# Python — Master/plugin_host.py
ipc.send_push_and_search(image, params_json)
ipc.send_run_recipe([img0, img1], recipe_json)
ipc.send_start_recipe(recipe_json); ipc.send_push_shot(image, 0); ipc.send_finish_recipe()
place = find_placement(result); poses = expand_placement(place)   # frame+pattern -> poses
```

```csharp
// C# — Master/WpfHost/PluginIpc.cs
await _host.PushAndSearchAsync(image, configJson, timeout);
await _host.RunRecipeAsync(images, recipeJson, timeout);
await _host.StartRecipeAsync(recipeJson); await _host.PushShotAsync(image, 0); await _host.FinishRecipeAsync();
```

Concurrency: a single global "busy" guard — a new command while a recipe/detection is running
is answered with `{"status":"error","reason":"busy"}`; retry later.

---

# Part C — Recipes & semantic results

## C.1 Recipe JSON (v4.1)

```jsonc
{
  "recipe_version": "4.1",
  "recipe_name": "3-station final inspection",
  "logic": "AND",                        // how shots combine: "AND" | "OR"
  "coordinate_frame": "machine",         // optional: unify cross-shot coords to the machine frame
  "calib": { "px_per_mm": 12.34, "cam_to_machine_deg": 0.8 },   // optional hand-eye
  "shots": [
    { "position": {"x":0, "y":0}, "image_slot": 0, "camera": 0,
      "light": {"program": 1},
      "image": "teach/p1.png",           // teaching/offline path (ignored over IPC)
      "transform": { "mode": "from_position" },
      "steps": [
        { "algorithm": "corner_detect", "parameters": { ... }, "emit": "pts_a" },
        { "algorithm": "golden_template", "parameters": { "template_path": "..." } }
      ] }
  ],
  "aggregates": [
    { "algorithm": "fit_rect_from_points", "points": ["pts_a", "pts_b", "edge_c"], "name": "board" }
  ],
  "constraints": [
    { "type": "center_distance", "a": 3, "b": 0, "max_px": 5 }
  ]
}
```

- **shots**: each consumes its own image. `image_slot` selects the IPC slot; offline/UI runs
  use `image`; `image` is **ignored** during IPC production (the host supplies the image).
- **steps**: `algorithm` + `parameters` (+ `emit:"tag"` to publish points/lines to the
  aggregate pool; + `name` on aggregates to reference them later).
- **aggregates**: run after all shots, over the emitted point/line pools and named results.
  Include fit nodes (`fit_line/rect/circle_from_points`), measurement nodes
  (`distance_*`, `intersect_lines`, `angle_three_points`, `concentricity`,
  `arc_from_three_points`, `aggregate_stat`, `true_position`) and framing nodes
  (`build_frame`, `placement_pattern`).
- **constraints**: `center_distance`, `radius_ratio`, `size_ratio`, `angle_diff`,
  `equidistant`, `line_distance`, `symmetry`, `measure_range`.
- **cross-FOV**: per-shot `transform` (`from_position` / `translate` / `rigid` / `affine`,
  optional `angle_deg` hand-eye) unifies coordinates so geometry pooled across shots is in one
  frame. Constraints index the **flattened** step order (all shot steps, then aggregates).

See [`RECIPE_GUIDE.md`](RECIPE_GUIDE.md) for the full node/constraint reference.

## C.2 Semantic result JSON

```jsonc
{
  "status": "ok|ng",
  "overall": "PASS|NG",
  "logic": "AND",
  "shots": [
    { "position": {...}, "light": {...}, "image": "slot0", "ok": 1,
      "steps": [
        { "algorithm": "circle_fit", "ok": 1, "ms": 12.3,
          "pose": {"x":100.5, "y":50.2, "an":12.3},
          "presence": {"ok": 1},
          "defect": {"count":2, "area":100, "has_defect":true},
          "string": "QR123",
          "shape": {"type":"distance", "v": 100.0} }
      ] }
  ],
  "aggregates": [
    { "algorithm": "build_frame", "ok": 1, "name": "substrate",
      "frame": {"ox":120, "oy":80, "theta_deg":0, "x_axis":[1,0], "y_axis":[0,1], "unit":"mm"} },
    { "algorithm": "placement_pattern", "ok": 1, "name": "place", "count": 24,
      "frame": {"ox":120, "oy":80, "theta_deg":0, "unit":"mm"},
      "pattern": {"mode":"grid","rows":3,"cols":8,"pitch_x":12,"pitch_y":10,"dtheta":5,"unit":"mm"},
      "poses": [ {"x":130,"y":88,"an":0}, ... ] }   // enumerated when count <= 16
  ],
  "constraints": [ { "type":"angle_diff", "ok":1, "description":"..." } ],
  "strings": ["QR123"]
}
```

Per-step fields: `pose` (cx/cy/angle/match_score), `presence` (ok), `defect` (count/area),
`string` (OCR/barcode), `shape` (fitted or measured value — `type` + `r/w/h/n/v`).

## C.3 Expanding a placement array

`placement_pattern` emits a coordinate frame + a compact `pattern` descriptor (and enumerated
`poses` when the count ≤ 16). Expand it to machine poses with:

```
machine(i) = O + R(θ) · local(i),   an = θ + local_angle(i)
```

Helpers are provided in every host SDK: C++ `PlacementExpand.h`, C# `PlacementExpand.cs`,
Python `expand_placement` / `find_placement`.

---

## Notes & limitations

- **Windows-first.** The runtime and sample hosts target Windows (MSVC + Qt + OpenCV).
- **Lighting is your job.** The runtime consumes frames; it does not control cameras or light
  hardware (the host does). Lighting is the most common cause of vision-project failure.
- **`params_data` / `result_data` are ~2 KB.** Use `recipe_file` for large recipes; the result
  degrades gracefully when over budget, with full detail in SQLite.
- **Optional commercial 3rd-party libs** (e.g. Basler pylon, HALCON) are **not** redistributed
  with the SDK and must be licensed separately by you.
