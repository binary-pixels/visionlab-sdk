# VisionLab — User Manual

## What it is

VisionLab is a machine-vision tool for **industrial inspection and measurement**. It is
built around a set of sub-pixel fitting algorithms and a **process-chain recipe** engine,
and can run either standalone (interactive) or embedded (driven by a host application over
IPC).

Main capabilities:

- **Shape fitting** — circle, line, ellipse, rectangle (sub-pixel).
- **Template matching** (gradient-orientation, rotation-invariant).
- **Blob analysis**, **golden-template diff**, **scratch** / **crack** detection.
- **OCR** (DNN / dot-matrix / VLM), **barcode / QR**, **DataMatrix**.
- **Unsupervised anomaly detection** for unknown defects (library built from good parts).
- **Batch processing** with statistics and CSV export.
- **Recipes** — ordered steps + preprocessing + between-step PASS/NG logic.

## Main window

```
┌───────────────────────────────────────────────────────┐
│  Menu: File | Algorithm | Help                          │
├───────────────────────┬─────────────────────────────────┤
│                       │  Control panel                   │
│   Image display       │   - algorithm selection          │
│   (ROI, results,      │   - ROI parameters               │
│    overlays)          │   - algorithm parameters         │
│                       │   - edge-detection method        │
│                       │   - Search / Batch Search        │
└───────────────────────┴─────────────────────────────────┘
```

- **Search** — run the current algorithm on the current image.
- **Batch Search** — run over a folder of images; shows a statistics summary.

## Parameters

### Circle fit

| Parameter | Meaning | Typical range |
|---|---|---|
| Center X/Y | ROI center | image bounds |
| Inner / Outer Radius | annular ROI radii | 0–500 px |
| Radius Min / Max | accepted fit radius | 0–500 px |
| Radius Tol | radius tolerance (pre-filter) | 0.1–100 px |
| Angle Gap | gap threshold for isolated points | 1–90° |

### Line fit

| Parameter | Meaning | Typical range |
|---|---|---|
| Center X/Y | rotated-rect ROI center | image bounds |
| Length / Width | ROI length / width | 10–1000 / 5–200 px |
| Angle | ROI orientation | 0–360° |

### Common

| Parameter | Meaning | Typical range |
|---|---|---|
| Iterations | RANSAC iterations | 10–5000 |
| Inlier Dist | inlier distance threshold | 0.1–50 px |
| Min Inliers | minimum inliers | 3–1000 |
| Canny Low/High | Canny thresholds | 1–500 |
| Sobel Ksize | Sobel kernel size | 1, 3, 5, 7 |
| Subpixel Step | sub-pixel refinement step | 0.05–2.0 px |

## Workflows

**Circle fit.** Open image → select "Sub-pixel circle fit" → set the ROI center to the
expected center and the inner/outer radii to enclose the target circle → tune parameters →
**Search** → read the result in the image and status bar.

**Line fit.** Open image → select "Line fit" → enclose the line with the rotated-rect ROI →
tune → **Search**.

**Tuning.** Coarse first (ROI covers the target), then fine (one parameter at a time),
then validate across several images, then save the parameter set for similar scenes.

**Batch.** Put images in one folder → fix the parameters on a single-image test →
**Batch Search** → review the summary (success/failure counts, average time); export CSV.

**Edge detection.** *Canny*: fast, for crisp edges. *Devernay*: sub-pixel, more accurate but
slower.

## FAQ

- **Image won't load?** Check the format (BMP/PNG/JPG/JPEG) and the path (non-ASCII paths).
- **Circle fit inaccurate?** Fit the ROI to the circle exactly; tighten the radius range;
  try the other edge detector; raise RANSAC iterations.
- **Improve accuracy?** Use Devernay sub-pixel; raise iterations; lower the inlier distance;
  adjust the sub-pixel step.
- **Batch results?** A results dialog gives success/failure counts and average time; export CSV.
- **Result traceability?** Every detection is written to `<exe>/results.db` (SQLite); NG
  source images are copied to `<exe>/archive/ng/<date>/`.
- **Parameter persistence?** The last-used parameters are restored on restart.
- **What is a ROI?** The region of interest — it limits where the algorithm searches,
  avoiding false detections and speeding things up.
- **Red fit result?** A red overlay means the fit failed — re-check the ROI/parameters.

## Advanced tips

- Drag directly in the image to move/resize the ROI; make sure it fully contains the feature.
- Start from defaults and change **one** parameter at a time; record the effect.
- For speed, keep the ROI small and pick the lightest edge method that meets accuracy.

## Process-chain recipes

A **recipe** is the complete inspection program for a part/process: ordered steps (each =
algorithm + parameters + ROI), a preprocessing pipeline, and between-step PASS/NG logic
(AND/OR + geometric constraints).

- **Save / load** — the recipe name + Save/Load store the whole chain as
  `<exe>/recipes/<name>.json`.
- **Steps** — `+ Add current algorithm` appends the tuned algorithm; reorder/edit/remove.
  **12 algorithms** are supported: circle/line/ellipse/rectangle fit, template match, blob
  analysis, golden-template diff, scratch, crack, OCR, barcode/QR, DataMatrix.
- **Multi-shot** — edit a step to assign it to a **photo point (shot)** (or "group into new
  shot") and give that shot an **image path** (offline), **position X/Y**, **image slot** and
  an **emit tag** (point source for aggregate fits). Multi-shot recipes save as **v4**.
- **Aggregate fits (cross-shot)** — add `fit_rect_from_points` / `fit_rect_from_lines` /
  `fit_line_from_points` / `fit_circle_from_points` and list the emit tags. They run after
  **all shots** finish. Recipes containing aggregates save as **v4.1**.
- **Logic** — `AND (all pass)` / `OR (any pass)`.
- **Constraints** — center distance / radius ratio / size ratio / angle diff / equidistant /
  distance to reference line / symmetry; distance and symmetry may judge in px or mm.
- **Run recipe** — executes all steps in the background and shows a per-shot grouped result
  table (# / Shot / Algorithm / Pose / Present / Defect / String / Status / Time) + the
  constraint verdicts + the overall verdict.

See **[Recipe & geometric-constraint guide](RECIPE_GUIDE.md)** and
**[Recipe IPC protocol](RECIPE_IPC_PROTOCOL.md)**.

## Engineering vs Run mode

The `Engineering` / `Run` toggle in the toolbar switches modes; the choice is saved.

| Mode | Purpose | UI |
|---|---|---|
| **Engineering** (default) | tune recipes, parameters, calibrate ROI | all controls: recipe editor, algorithm tree, parameter panel, Run/Batch/Load Config; ROI draggable |
| **Run** | production; prevent accidental edits | hides the editors/parameter panel; keeps the read-only step/constraint list and the **Run recipe** button; ROI locked |

Images and results remain visible in Run mode, and recipe load / parameter writes still work.

## Result traceability (SQLite + NG archive)

Every detection (single or batch) is written to SQLite and NG images are archived:

| Content | Location | Notes |
|---|---|---|
| Results DB | `<exe>/results.db` | table `inspection_results`: time / algorithm / image / camera / OK-NG / measurements JSON / batch; recipe runs write **one row per step** (`is_recipe`, `recipe_name`, `shot_index`, `step_index`, `total_steps`) |
| NG archive | `<exe>/archive/ng/<date>/` | NG source images copied for review |

> Deployment note: `sqldrivers/qsqlite.dll` (plus `Qt6Sql.dll`, `Qt6Concurrent.dll`) must sit
> next to the exe, otherwise persistence silently does nothing.

## Debug image capture (trace failures / reproduce)

With **"Auto-save debug images"** enabled, **every algorithm execution** (single detection or
each recipe step) writes a set for later root-cause analysis and **reproduction**:

| File | Content | Purpose |
|---|---|---|
| `<ts>_<tag>_ok/ng_orig.png` | the original image | reproduce: reload and re-run |
| `<ts>_<tag>_ok/ng_result.png` | original + **ROI box** + fit overlay + debug points + `OK/NG` bar | spot a wrong ROI, an empty ROI, or a fit that drifted |
| `<ts>_<tag>_ok/ng.json` | algorithm + **full parameters (incl. ROI)** + measurements + status + image path/camera/`px_per_mm` | locate the cause and reproduce exactly |

- **Tag**: single detection = algorithm name; recipe step = `shot2_step1_circle_fit`.
- **Location**: grouped per day, `<dir>/<yyyy-MM-dd>/`; default `<exe>/debug_captures`.
- **Options**: auto-save (master), NG-only, directory, size cap (MB); persisted.
- **Usage / cleanup**: the panel shows `used X MB / N files`; over the cap it turns yellow
  ("consider cleaning"). `Refresh` recomputes; `Clean` deletes oldest-first until ≤ cap
  (with confirmation), then prunes empty day folders.
- **No compression**: PNGs are already compressed — use day-folders + a size cap instead.

## Measurement algorithms

Arc fit, caliper and corner detection parameters are documented in
**[Algorithm usage guide](ALGO_USAGE_GUIDE.md)**.

## Support

Open an issue at <https://github.com/binary-pixels/visionlab-sdk/issues>.
