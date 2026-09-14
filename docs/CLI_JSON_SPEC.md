# CLI JSON Configuration Spec

The parameter-configuration format shared by the UI and the CLI tools. It records an
algorithm's parameters and lets a run be **reproduced** later.

## Top-level format

```json
{
  "version": "1.0",
  "algorithm": "circle_fit",
  "image_path": "path/to/image.bmp",
  "output_path": "path/to/result.png",
  "timestamp": "2026-04-03T13:26:47",
  "parameters": { }
}
```

| Field | Type | Meaning |
|---|---|---|
| `version` | string | config format version (currently `"1.0"`) |
| `algorithm` | string | algorithm key (`circle_fit`, `line_fit`, …) |
| `image_path` | string | input image path (absolute or relative) |
| `output_path` | string (optional) | result image path |
| `timestamp` | string (optional) | when the parameters were produced (ISO 8601) |
| `parameters` | object | algorithm-specific parameters |

---

## `circle_fit`

```json
{
  "version": "1.0",
  "algorithm": "circle_fit",
  "image_path": "image.bmp",
  "output_path": "result.png",
  "timestamp": "2026-04-03T13:26:47",
  "parameters": {
    "roi": { "center": { "x": 640.0, "y": 480.0 },
             "inner_radius": 200.0, "outer_radius": 300.0 },
    "radius_range": { "min": 220.0, "max": 280.0 },
    "filter": { "radius_tolerance": 20.0, "angle_gap_deg": 30.0 },
    "ransac": { "iterations": 1000, "inlier_distance": 3.0, "min_inliers": 10 },
    "edge_detection": { "method": "devernay", "canny_low": 50, "canny_high": 150,
                        "sobel_ksize": 3, "subpixel_step": 0.5 }
  }
}
```

- **`roi`** — annular ROI: `center.x/y` (px), `inner_radius`, `outer_radius` (px).
- **`radius_range`** — accepted fit radius `min`/`max` (px).
- **`filter`** — `radius_tolerance` ∈ [0.1, 100] px; `angle_gap_deg` ∈ [1, 90]°.
- **`ransac`** — `iterations` ∈ [10, 5000]; `inlier_distance` ∈ [0.01, 50] px;
  `min_inliers` ∈ [3, 1000].
- **`edge_detection`** — `method` = `devernay` (sub-pixel, recommended) or `canny`;
  `canny_low/high` ∈ [1, 500]; `sobel_ksize` ∈ {1,3,5,7}; `subpixel_step` ∈ [0.05, 2.0].

## `line_fit`

```json
{
  "version": "1.0",
  "algorithm": "line_fit",
  "image_path": "image.bmp",
  "output_path": "result.png",
  "parameters": {
    "roi": { "type": "rotated_rect", "center": { "x": 640.0, "y": 480.0 },
             "width": 400.0, "height": 100.0, "angle": 0.0 },
    "line_range": { "min_length": 100.0, "max_length": 500.0 },
    "filter": { "distance_tolerance": 5.0, "angle_tolerance": 10.0 },
    "ransac": { "iterations": 1000, "inlier_distance": 2.0, "min_inliers": 10 },
    "edge_detection": { "method": "devernay", "canny_low": 50, "canny_high": 150,
                        "sobel_ksize": 3, "subpixel_step": 0.5 }
  }
}
```

- **`roi`** — rotated rectangle: `type` = `"rotated_rect"`; `center.x/y` (px); `width`,
  `height` (px); `angle` ∈ [-180, 180]°.
- **`line_range`** — expected line length `min_length`/`max_length` (px).
- **`filter`** — `distance_tolerance` (px), `angle_tolerance` (°).
- **`ransac`** / **`edge_detection`** — as above.

---

## CLI usage

```bash
# circle_fit
circle_fit_cli config.json
circle_fit_cli config.json result.png          # overrides output_path
circle_fit_cli image.bmp                        # legacy: image only
circle_fit_cli image.bmp config.json
circle_fit_cli image.bmp config.json result.png

# line_fit
line_fit_cli config.json
line_fit_cli config.json result.png
line_fit_cli image.bmp
line_fit_cli image.bmp result.png
```

## UI export

After a successful fit, the UI can write a config next to the image, named
`<image>_<algorithm>_config.json` (e.g. `test_circle_fit_config.json`).

## Version compatibility

- **v1.0** (current): circle_fit and line_fit.
- Planned: v1.1 ellipse_fit; v1.2 batch configs; v2.0 preprocessing parameters.

## Notes

1. Prefer absolute paths, or paths relative to the config file.
2. One decimal place is enough for floats.
3. The CLI validates ranges — out-of-range values fall back to defaults with a warning.
4. The CLI also accepts the legacy command-line forms.
5. On JSON parse failure, emit a clear error.
