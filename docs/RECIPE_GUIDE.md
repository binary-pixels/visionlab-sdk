# Process-Chain Recipes & Geometric Constraints

A **recipe** is the complete inspection program for a part or process. It has three parts:

1. **Ordered steps** — each step = one algorithm + full parameters (including ROI).
2. **Preprocessing pipeline** — illumination compensation / filtering / FFT / grayscale, plus a calibration reference.
3. **Between-step logic** — how the steps combine to a PASS/NG verdict (AND/OR) + geometric constraints.

Recipes are JSON files stored under `<exe>/recipes/`, so a line/part change is a file swap.

---

## 1. Recipe JSON (v3, flat)

```jsonc
{
  "recipe_version": "3.0",
  "recipe_name": "battery-cap final",
  "logic": "AND",                       // "AND" | "OR"
  "steps": [
    { "algorithm": "circle_fit", "parameters": { "roi": {...}, ... } },
    { "algorithm": "line_fit",   "parameters": { ... } }
  ],
  "constraints": [
    { "type": "center_distance", "a": 0, "b": 1, "max_px": 20.0 },
    { "type": "angle_diff",      "a": 0, "b": 1, "target_deg": 90.0, "tol_deg": 1.0 }
  ]
}
```

## 2. Multi-shot recipes (v4)

Steps are grouped into **shots** (photo points), each consuming its own image. Constraints
index the **flattened** step order (shot 0 steps, then shot 1 steps, …), so the constraint
engine is unchanged.

```jsonc
{
  "recipe_version": "4.1",
  "recipe_name": "3-station final",
  "logic": "AND",
  "shots": [
    { "position": {"x": 0,   "y": 0},   "image_slot": 0,
      "image": "shots/p1.png",          // offline path (empty => current/pushed image)
      "steps": [
        { "algorithm": "corner_detect", "parameters": { ... }, "emit": "pts_a" },
        { "algorithm": "golden_template", "parameters": { "template_path": "..." } }
      ] },
    { "position": {"x": 120, "y": 0}, "image_slot": 1,
      "steps": [ { "algorithm": "line_fit", "parameters": { ... }, "emit": "edge_c" } ] }
  ],
  "aggregates": [
    { "algorithm": "fit_rect_from_points", "points": ["pts_a", "pts_b", "edge_c"] }
  ],
  "constraints": [ { "type": "center_distance", "a": 3, "b": 0, "max_px": 5 } ]
}
```

- **shot → image**: IPC runs use `image_slot` (0..3); offline runs `imread` the `image`
  path; empty → current/pushed image.
- **`image` is teaching/offline-only** — during IPC production it is **ignored**; the host
  supplies the image per shot.
- **Overall verdict** = (shots combined by `logic`) AND (all constraints pass). A shot passes
  when all its steps pass.
- **Backward compatible**: v1/v2/v3 recipes are treated as a single shot.

## 2.1 Cross-field-of-view coordinate unification

Each shot's pixels are in **its own local image frame**. To combine geometry across shots,
declare a 2-D transform per shot:

```jsonc
{ "coordinate_frame": "machine",                       // recipe-level switch (optional)
  "calib": { "cam_to_machine_deg": 0.8 },               // hand-eye camera↔machine angle
  "shots": [
    { "position": {"x": 120, "y": 0},
      "transform": { "mode": "from_position", "angle_deg": 0.8 }, ... } ] }
```

| `transform.mode` | Parameters | Meaning |
|---|---|---|
| `from_position` | `angle_deg` (opt) | translate by `position(mm) × px_per_mm` (+ hand-eye rotation) |
| `translate` | `dx, dy` | pixel translation |
| `rigid` | `angle_deg, dx, dy` | rotate + translate |
| `affine` | `a,b,c,d,e,f` | general 2×3 affine |

Applies to the aggregate layer (point/line pools + named results); constraints reference the
raw step measurements (which stay in local pixels) — for cross-shot judging, reference
aggregate outputs. Requires calibration (`px_per_mm`) for `from_position` and mm outputs.

## 2.2 Aggregate fits (cross-shot)

Steps tagged `"emit":"tag"` publish their `points` (and, for `line_fit`, their line) into a
pool. Aggregates run after **all** shots and consume the pool by tag. A step that outputs a
`name` becomes referenceable by later aggregates.

| `aggregates[].algorithm` | Fit | Input | Output |
|---|---|---|---|
| `fit_rect_from_points` | min-area rotated rectangle | `points` | cx, cy, width, height, angle |
| `fit_rect_from_lines` | rectangle from 4 edge lines (exact corners) | `lines` | cx, cy, width, height, angle, corners |
| `fit_line_from_points` | least-squares line (PCA; or directed) | `points` | cx, cy, angle, endpoints; straightness `rms_error_px`/`max_error_px` |
| `fit_circle_from_points` | algebraic (Kasa) circle | `points` | cx, cy, radius, **diameter**, roundness `rms_error_px`/`max_error_px` |

## 2.3 Measurement / compute nodes

Nodes that turn pooled points/lines/circles into scalar measurements or derived geometry.

**Data sources:** `points` (emit tags, or a named output's center), `lines` (line pool tag,
or a named output's `line_x1..y2`), `circles` (named `fit_circle_from_points` output),
`names` (a set of named results).

**Chaining (computation DAG):** a named aggregate's center is republished into the point pool
and its line into the line pool, so later nodes can reference it by `name`
(e.g. `intersect_lines → name:"corner"` then `distance_points points:["corner","holeA"]`).
A `points` tag with multiple points resolves to their **centroid** (documented caution).

| `algorithm` | Input | Output | Use |
|---|---|---|---|
| `distance_points` | `points`(2) | distance_px/_mm, midpoint | hole spacing, two points |
| `distance_lines` | `lines`(2) | distance_px/_mm | parallel-edge gap, slot width |
| `distance_circles` | `circles`(2) | distance_px = center distance, gap_px = edge-to-edge (neg = overlap), *_mm | hole spacing; wall gap |
| `distance_point_line` | `points`(1)+`lines`(1) | distance_px/_mm | feature-to-datum distance |
| `intersect_lines` | `lines`(2) | cx, cy (virtual corner) | corner/spear point |
| `angle_three_points` | `points`(3, first = vertex) | angle ∈ [0,180] | corner angle |
| `directed_angle_between_lines` | `lines`(2) | angle ∈ [0,360), signed_angle | oriented rotation |
| `concentricity` | `circles`(2) | dx, dy, offset, eccentricity | concentricity / eccentricity |
| `arc_from_three_points` | `points`(3: start, mid, end) | radius, diameter, central_angle, arc_length_px, chord_length_px | fillet / arc metrics |
| `aggregate_stat` | `names` + `field` + `stat` | value, min/max/mean/stddev/count | multi-point statistics |
| `true_position` | `points`(1) + `nominal_x/y` | dx, dy, deviation_px | GD&T position |

## 2.4 Coordinate frames & placement arrays

For substrates larger than one field of view: fit two near-perpendicular edges → build a
frame at their corner → generate a placement array.

```jsonc
"aggregates": [
  { "algorithm": "fit_line_from_points", "points": ["L1","L2"], "name": "left" },
  { "algorithm": "fit_line_from_points", "points": ["T1","T2"], "name": "top"  },
  { "algorithm": "build_frame", "lines": ["top","left"], "x_axis": "top",
    "handedness": "right", "name": "substrate" },
  { "algorithm": "placement_pattern", "frame": "substrate", "name": "place",
    "mode": "grid", "origin": {"x": 10.0, "y": 8.0}, "angle": 0.0,
    "rows": 2, "cols": 3, "pitch_x": 12.0, "pitch_y": 10.0, "dtheta": 5.0 }
]
```

- **`build_frame`**: origin = the two lines' intersection; X axis = the `x_axis` line (sign
  disambiguated by the other line); Y axis orthogonalized by `handedness`. Outputs
  `frame{ox, oy, theta_deg, x_axis, y_axis, perp_deg, unit}` in **mm** (if calibrated).
- **`placement_pattern`**: `mode` = `single` / `line` / `grid` / `list`; per-element rotation
  `angle + i·dtheta`; `origin_in:"machine"` inverse-solves a machine-coordinate origin into
  the frame. Outputs `count`, `poses[]` (machine coords) and a compact `pattern` descriptor.
- **Host expansion**: `machine(i) = O + R(θ)·local(i)`, `an = θ + local_angle(i)`. Helper in
  every host SDK (`PlacementExpand.h` / `PlacementExpand.cs` / `expand_placement`).

---

## 3. Step algorithms

| `algorithm` | Purpose | Measurements |
|---|---|---|
| `circle_fit` | circle fit | cx, cy, radius |
| `line_fit` | line fit | cx, cy, **angle** (0–180°), line_x1/y1/x2/y2 |
| `ellipse_fit` | ellipse fit | cx, cy, radius(=a), width, height, angle |
| `rectangle_fit` | rectangle fit | cx, cy, width, height, angle |
| `template_match` | template match | cx, cy, angle, match_score |
| `blob_analysis` | connected components | blob_count, blob_total_area, blob_first_area |
| `golden_template` | reference-image diff | defect_count, defect_total_area |
| `scratch_detect` | scratch detection | defect_count, defect_total_area |
| `crack_detect` | crack detection | crack_count, crack_coverage |
| `ocr` | text recognition | ocr_text, ocr_length |
| `barcode` | barcode / QR | barcode_count, barcode_texts[], barcode_first_text |
| `data_matrix` | DataMatrix | dm_count, dm_texts[], dm_first_text |

Each step's measurements are available to constraints. `line_fit` also exposes its endpoints
for `line_distance`.

## 4. Constraint types

Evaluated after **all** steps. A constraint whose referenced step is NG judges NG.

| Type | Rule | Fields | Use |
|---|---|---|---|
| `center_distance` | distance(A center, B center) ≤ max | `a, b, max_px` or `use_mm, max_mm` | concentricity, hole position |
| `radius_ratio` | radius(A)/radius(B) ∈ [min,max] | `a, b, min, max` | wall thickness, fit |
| `size_ratio` | field(A)/field(B) ∈ [min,max] | `a, b, field(radius/width/height), min, max` | inner/outer size ratio |
| `angle_diff` | \|angle(A)−angle(B)\| (mod 180 → [0,90]) ≈ target ± tol | `a, b, target_deg (0=parallel, 90=perpendicular), tol_deg` | perpendicularity, parallelism |
| `equidistant` | max\|d(k) − mean(d)\| ≤ max (≥3 ordered steps) | `steps[], max_px` or `use_mm, max_mm` | multi-hole equal spacing |
| `line_distance` | distance of step A center to a reference line ≤ max | `a, line_step` or explicit `line_x1..y2`, `max_px` / `use_mm, max_mm` | feature-to-datum distance |
| `symmetry` | \|mirror(A over axis) − B\| ≤ max | `a, b, axis_x1..y2, max_px` / `use_mm, max_mm` | symmetry |
| `measure_range` | step A field ∈ [min,max] | `a, field, min, max, use_mm` | judge any scalar (distance/angle/diameter/… ) |

> Distance/symmetry/measure_range support **px or mm**: with mm selected, the value is divided
> by the camera's `px_per_mm`; if uncalibrated, the constraint judges NG.

> Guardrail: a non-locating step (OCR/barcode, center = 0) referenced by a geometric
> constraint judges NG ("missing center") — never a false PASS.

**Overall** = (steps combined by AND/OR) AND (all constraints pass).

## 5. Scenarios

| Scenario | Steps | Constraint | Defect detected |
|---|---|---|---|
| Concentricity | outer circle, inner circle | `center_distance` | raceway offset |
| Hole position | datum hole, measured hole | `center_distance` | hole offset |
| Bevel perpendicularity | edge A, edge B | `angle_diff` target 90 | non-perpendicular face |
| Rail parallelism | edge A, edge B | `angle_diff` target 0 | misalignment |
| Wall uniformity | outer circle, inner circle | `radius_ratio` | uneven wall |
| Multi-hole spacing | N holes | `equidistant` | unequal pitch |
| Feature-to-datum | feature, datum edge | `line_distance` | datumed distance |
| Symmetry | left, right | `symmetry` | off-center |
| Circle/line gap | circle A, circle B | `distance_circles` (gap_px) | interference |
| Corner angle | 3 points | `angle_three_points` | angle deviation |
| Arc / fillet | 3 arc points | `arc_from_three_points` | fillet radius |
| Placement position | point vs nominal | `true_position` | position tolerance |

## 6. UI flow

1. **Add a step** — pick an algorithm, tune parameters/ROI, `+ Add current algorithm`.
2. **Logic** — `AND` / `OR`.
3. **Add a constraint** — pick steps A/B, type, thresholds (px or mm).
4. **Shot grouping** — edit a step to assign a shot (or create a new one); set image path /
   position / slot / emit tag. Steps show `Shot k · n. algorithm`.
5. **Aggregate / measurement node** — pick a fit or measurement type and source tags/names.
6. **Run** — `Run recipe`; multi-shot recipes show a per-shot grouped table + constraints.
7. **Save / load** — multi-shot → **v4**, with aggregates → **v4.1**, otherwise **v3**.

## 7. Semantic result JSON

```jsonc
{ "algorithm": "circle_fit", "ok": 1, "ms": 12.3,
  "pose": {"x": 100.5, "y": 50.2, "an": 12.3},
  "presence": {"ok": 1},
  "defect": {"count": 2, "area": 100, "has_defect": true},
  "string": "QR123",
  "shape": {"type": "rect", "w": 180, "h": 180, "n": 4} }
```

- `pose` from cx/cy/angle/match_score; `defect` from defect_count → crack_count → blob_count;
  `string` from ocr_text → barcode_first_text → dm_first_text; `shape` on aggregates /
  measurement nodes (`type` + `r/w/h/n/v`, where `v` is the scalar for distance/angle/stat).

Full schema: see [`PLUGIN_INTEGRATION_GUIDE.md`](PLUGIN_INTEGRATION_GUIDE.md) Part C.

## 8. Limitations

- No cross-step conditional logic (no "check B only if A passes").
- mm judging requires calibration (`px_per_mm`); otherwise distance constraints in mm judge NG.
- A geometric constraint referencing a non-locating step (OCR/barcode, center 0) judges NG.
- Template/model/image paths are absolute — a recipe moved to another machine must find those
  files there.
- IPC inlines at most 4 shots; large recipes are sent by file path.
