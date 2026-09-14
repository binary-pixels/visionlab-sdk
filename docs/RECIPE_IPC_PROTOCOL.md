# Recipe IPC Protocol

The wire contract for driving the VisionLab runtime from your own application over
**shared memory**. This is the "black-box" integration model; the in-process C ABI is a
separate model (see [`plugin_abi.md`](plugin_abi.md)).

For recipes, results and placement arrays, see [`RECIPE_GUIDE.md`](RECIPE_GUIDE.md) and
[`PLUGIN_INTEGRATION_GUIDE.md`](PLUGIN_INTEGRATION_GUIDE.md) Part C. Reference host clients
ship in `Master/` (C++, C#, Python).

---

## 1. Shared-memory layout (V3)

```
Control block   SharedMem_Cmd_<name>    20 KB
  [0]      MultiCamHeader    4 KB        (version, cam_count, heartbeats)
  [4096]   CamSlot[0..3]     4 KB each   (per-camera command + params + result)

Image block     SharedMem_Img_<name>   128 MB
  ImageSlot[i] = i * 32 MB               (64-byte ImageHeader + pixels)

Events          Event_H2P_<name>_0..3   (host → plugin, one per camera)
                Event_P2H_<name>_0..3   (plugin → host, one per camera)
```

**CamSlot fields**: `cmd_id`, `cmd_type`, `cmd_status`, `params_len`, `params_data[2012]`,
`result_len`, `result_data[2012]`, plus a `writer_seq` (seqlock) so the plugin can reject
torn reads.

> **The param/result budget is ~2 KB.** A recipe or parameter set is written inline into
> `params_data`; the result comes back in `result_data` (both ≤ 2012 bytes). Large recipes go
> by file path (§4).

## 2. Command types

| Value | Command | Purpose |
|---|---|---|
| 1 | `PushImage` | write image + params |
| 2 | `LoadImageFile` | params = `{"path": "..."}` |
| 3 | `Search` | run an algorithm on the current image + params |
| 4 | `PushAndSearch` | push an image + one algorithm's params; result carries `semantic` |
| 7 | `GetParams` | return current params |
| 9 | `Shutdown` | exit |
| 10 | `RunRecipe` | whole recipe: images in slots 0..N-1 + recipe JSON |
| 11 | `StartRecipe` | streaming session: params = full recipe JSON |
| 12 | `PushShot` | streaming: params = `{"shot_index": k}` + image |
| 13 | `FinishRecipe` | streaming: aggregate staged shots → full semantic JSON |

## 3. Running a recipe — three ways

**1. `PushAndSearch`** (single algorithm). One image + one algorithm's params. The result JSON
carries `{"cmd_id","status","detail","semantic"}`. Use it for live tuning/teaching.

**2. `RunRecipe`** (whole recipe, ≤ 4 inline images). Write the images into slots 0..N-1 and
the recipe JSON into `params_data`, then send one `RunRecipe`. Best when you have all images
up front (≤ 4 camera positions).

**3. Streaming session** (unlimited shots, overlaps motion):

```
StartRecipe(recipeJson)               -> plugin caches the recipe, resets staging
                                         reply {"status":"ok","shots":N}
for k in 0..N-1:
    move stage / capture
    PushShot(image, {"shot_index":k}) -> plugin runs shot k immediately; reply shotAcked(k)
FinishRecipe()                        -> aggregate all shots; reply full semantic JSON
```

Streaming uses **one** image slot (each `PushShot` overwrites it), so it is **not limited to
4 shots**, and shot k+1 can be captured while shot k is still computing.

## 4. Payload encoding

- **Params** are JSON. For `RunRecipe`/`StartRecipe` the value is the recipe JSON; for
  `PushShot` it is `{"shot_index": k}`.
- **Large recipes**: `params_data` is ~2 KB, so a recipe larger than that is **truncated** if
  sent inline. Send a file path instead:
  `{"recipe_file": "C:/path/to/recipe.json"}` — the plugin reads the file (same machine).
- **Recipe caching (production)**: `StartRecipe` with a recipe caches it; later cycles may
  send an empty `params` to reuse the cache, so you don't resend the recipe every cycle.
  Re-send when the recipe changes or after a plugin restart.
- **Result truncation**: the result must fit ~2 KB. When it doesn't, the plugin returns a
  degraded `{"truncated":true,"reason":...}` form (per-step verdicts + constraints + strings,
  progressively dropped). Full detail is written to SQLite on the plugin side.

## 5. Teaching metadata

During teaching, `PushAndSearch` params may carry extra top-level fields (they don't break
`algorithm`/`parameters`):

```jsonc
{
  "algorithm": "line_fit",
  "parameters": { ... },
  "position": {"x": 123.5, "y": 45.2},      // machine coords of the capture position
  "shot": 0,                                 // photo-point (shot) index
  "camera": 1,                               // teaching source camera (slot)
  "light": {"program": 2, "intensity": 80}   // lighting used
}
```

The plugin remembers them; when the operator adds a step, that step's shot gets the
`position` / `camera` / `light`. In production these are ignored (images arrive over IPC).

## 6. Semantic result

`RunRecipe`, streaming `FinishRecipe`, and `PushAndSearch` (via `semantic`) all return the
same **semantic result schema**:

```jsonc
{ "status":"ok|ng", "overall":"PASS|NG", "logic":"AND",
  "shots":[ { "position":{...}, "light":{...}, "image":"slot0", "ok":1,
    "steps":[ { "algorithm":"circle_fit", "ok":1, "ms":12.3,
                "pose":{"x":..,"y":..,"an":..}, "presence":{"ok":1},
                "defect":{"count":..,"area":..}, "string":"QR123",
                "shape":{"type":"distance","v":100.0} } ] } ],
  "aggregates":[ { "algorithm":"build_frame", "name":"substrate",
                   "frame":{"ox":..,"oy":..,"theta_deg":..,"unit":"mm"} } ],
  "constraints":[ { "type":"angle_diff", "ok":1, "description":"..." } ],
  "strings":[ ... ] }
```

Full schema and the placement-array expansion are in
[`PLUGIN_INTEGRATION_GUIDE.md`](PLUGIN_INTEGRATION_GUIDE.md) Part C.

## 7. Concurrency & lifecycle

- One global "busy" guard: a new command while a recipe/detection is running is answered with
  `{"status":"error","reason":"busy"}`; retry later.
- Camera slots 0..3 map to the `camera` field (multi-camera rigs). `RunRecipe` inlines at most
  4 images; use the streaming session for more.
- Commands are processed serially per session; the plugin runs heavy work off the UI thread.

## 8. Host client SDKs

```cpp
// C++  (Master/QtHost/IpcClient.h)
IpcClient::sendPushAndSearch(image, paramsJson);
IpcClient::sendRunRecipe(images, recipeJson);            // <= 4 images
IpcClient::sendStartRecipe(recipeJson); sendPushShot(image, k); sendFinishRecipe();
```

```python
# Python  (Master/plugin_host.py)
ipc.send_push_and_search(image, params_json)
ipc.send_run_recipe([img0, img1], recipe_json)
ipc.send_start_recipe(recipe_json); ipc.send_push_shot(image, 0); ipc.send_finish_recipe()
```

```csharp
// C#  (Master/WpfHost/PluginIpc.cs)
await _host.PushAndSearchAsync(image, configJson, timeout);
await _host.RunRecipeAsync(images, recipeJson, timeout);
await _host.StartRecipeAsync(recipeJson); await _host.PushShotAsync(image, 0); await _host.FinishRecipeAsync();
```
