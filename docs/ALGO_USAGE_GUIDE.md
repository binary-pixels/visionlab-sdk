# Advanced Algorithms — Usage Guide

This guide documents the **non-trivial algorithms** — arc / ellipse / rectangle fit, caliper,
corner detect, template match, blob analysis, golden-template diff, scratch, crack, color
analysis, OCR, barcode and DataMatrix — with their ROI setup, parameters, outputs and common
issues. (Circle and line fit are covered in [`USER_MANUAL.md`](USER_MANUAL.md).)

> **This is not the full algorithm list.** Circle / line fitting parameters are documented in
> [`USER_MANUAL.md`](USER_MANUAL.md); the complete set of 16 step algorithms (circle / line /
> ellipse / rectangle / arc fit, template match, caliper, corner, blob, golden-template,
> scratch, crack, color analysis, OCR, barcode, DataMatrix) and their outputs are in
> [`RECIPE_GUIDE.md`](RECIPE_GUIDE.md) §3. The parameter names below are the **UI labels**;
> the corresponding recipe JSON keys are the algorithm schema fields.

---

## 1. Arc Fit

Extracts a circular arc's center, radius, arc length, chord length and angular span.
Pipeline: **annular-sector ROI → radial scan lines → sub-pixel edge points → RANSAC circle fit**.

### ROI (annular sector — matches an arc)

| ROI parameter | Meaning | Guidance |
|---|---|---|
| Center X / Y | sector center (px) | roughly at the expected arc center |
| Inner R | inner radius | ~20–40 px below the expected radius |
| Outer R | outer radius | ~20–40 px above the expected radius |
| Start / End Deg | scan range (0° = 3 o'clock, clockwise) | restrict to where the arc is; 360 = full circle |

> `Outer R − Inner R` is the sampling band; too narrow misses the edge, too wide adds
> background noise. Aim for ~30–50% of the expected radius.

### Scan / fit parameters

| Parameter | Recommended |
|---|---|
| Scan Lines | ≥180° arc: 36; partial arc: ≈ arc length / 10 px |
| Edge Thr | high contrast 50–100; low contrast 15–30 |
| RANSAC Iter | 500 (1000 for high precision) |
| Inlier Dist | 1.0–2.0 px (sub-pixel) |
| Min Inliers | ≥ Scan Lines × 0.5 |

### Outputs
center (cx, cy), radius R, span (°), arc length (px), chord length (px), inlier count.

### Tips
1. Drag the sector center to the arc center.
2. Size Inner/Outer R so the sampling band straddles the arc edge.
3. For a partial arc, set Start/End Deg to avoid the gap.
4. If too few inliers, widen Inner/Outer R or lower Edge Thr.

### Common issues
| Symptom | Cause | Fix |
|---|---|---|
| Fit fails / 0 inliers | ROI doesn't cover the edge | check Inner/Outer R |
| Large center error | arc too short (<60°) | widen the angular range / raise Min Inliers |
| Background taken as edge | Edge Thr too low | raise Edge Thr; narrow the band |
| Jittery result | too few Scan Lines | raise to 60–100 |

---

## 2. Caliper

Deploys evenly spaced virtual calipers along a straight path; each scans perpendicular to the
path and finds the strongest edge. **Single-edge** mode locates one edge + straightness;
**Width** mode finds both edges and measures width.

### ROI (rotated rectangle)
Long axis along the edge; short axis (scan length) perpendicular, covering ~10–20 px each
side; angle aligned to the edge.

### Parameters

| Parameter | Meaning | Recommended |
|---|---|---|
| Count | number of calipers | 100 (fewer for short edges) |
| Edge Thr | gradient threshold | 100; 30–50 for low contrast |
| Polarity | edge direction | `Dark→Bright`, `Bright→Dark`, or `Both` |
| Edge Select | which edge per caliper | `Strongest` (default); `First` / `Last` |
| Min Valid Ratio | minimum valid fraction, else NG | 0.5 |

### Modes
- **Single Edge**: gives edge position + straightness (max perpendicular deviation, px).
  Set Polarity to the true direction; if parallel edges appear, narrow the ROI.
- **Width**: set Polarity = `Both`; pairs nearest +/- gradients → average width (px). Narrow
  the ROI to a single target so pairing is unambiguous.

### Outputs
valid/total calipers (`N/M`), width (Width mode), straightness (px, −1 = too few points),
PASS/NG (valid ratio ≥ Min Valid Ratio and straightness ≤ limit).

### Tips
Aim for valid calipers ≥ 80%. Enable **Straightness** with a limit when flatness matters.

### Common issues
| Symptom | Cause | Fix |
|---|---|---|
| 0 valid calipers | Edge Thr too high / ROI off | lower Edge Thr; recheck ROI |
| Width varies with translation | `Both` but ROI spans more than one edge pair | narrow the ROI |
| Straightness too large | noisy calipers | raise Edge Thr; adjust Min Valid Ratio |
| Wrong polarity result | direction reversed | switch to the other polarity / `Both` |

---

## 3. Corner Detect

Finds corners in a ROI (Shi-Tomasi / Harris / FAST), returns them by response strength.
Use it to locate a rectangle's four corners or an L-shaped feature.

### ROI (rectangle)
Include the **whole object**, not just the corner area — the edge mask needs the boundary.

### Detector
| Detector | Note | Use |
|---|---|---|
| Shi-Tomasi (default) | stable, general | most scenes, rectangle corners |
| Harris | rotation-invariant, more params | high-precision corners |
| FAST | very fast, no response value | real-time, low precision |

### Parameters
| Parameter | Recommended |
|---|---|
| Max Corners | 4 for a rectangle |
| Min Distance | ~80% of the smallest corner spacing (default 100 px) |
| Quality Level | 0.05–0.3 (higher = stricter) |
| Min Response | 0; 0.01–0.05 when noisy |
| Subpixel | on (≈0.1 px) |

### Edge masking (key noise suppression)
- **Edge Mask**: always on — detect only near image edges.
- **Largest Region** (recommended): use the largest bright region's contour as the mask —
  ideal for a bright part on a dark background; eliminates small noise specks.
- Canny Low/High: only when Largest Region is off (e.g. 150/300).

> Most "false corners" come from small bright specks whose edges outscore the real corner.
> Edge Mask + Largest Region fixes this.

### Corner-angle filter
`Min Corner Angle` measures the local turn angle at a candidate: real rectangle corners ≈ 90°,
quantization noise on a curved edge ≈ 5–20°. Setting 25° separates rectangle from circle; 0
disables the filter.

### Common issues
| Symptom | Fix |
|---|---|
| Corners on noise specks | enable Largest Region; raise Quality Level |
| Only 2–3 of 4 corners | lower Min Distance; raise Max Corners |
| Circle edge flagged as corner | set Min Corner Angle 25–45° |
| Imprecise coordinates | enable Subpixel |

---

## 4. Blob Analysis

Binarize + connected components in a ROI to find bright/dark regions and report area,
circularity, aspect ratio and centroid.

Use cases: counting (solder joints, holes, pins), size checks, shape filtering
(circle vs rectangle), presence detection.

### Parameters
| Parameter | Recommended |
|---|---|
| Threshold | 0 (Otsu); manual for clear contrast |
| Polarity | `Bright` (bright on dark) / `Dark` (dark on bright) |
| Min / Max Area (px²) | per target size |
| Min Circularity | 0.7–0.9 for round joints; 0 = off |
| Min / Max Aspect Ratio | 1.0–1.5 for near-circular |
| Max Count | target upper bound |

### Outputs
blob count; per-blob centroid (cx, cy), area, circularity (4π·area/perimeter²), aspect ratio.

### Tips
Polarity first, then Otsu threshold, then area filtering; add circularity for round parts.

### Common issues
| Symptom | Fix |
|---|---|
| Too many blobs (noise) | raise Threshold; raise Min Area |
| Missed blobs | check Polarity; lower Min Area |
| Neighbors merged | erode, or lower Max Area to split |
| Round filter drops rectangles | lower Min Circularity or disable it |

---

## 5. Golden Template

Aligns the current image to a recorded "golden" reference, diffs pixel-wise, and flags regions
over a threshold as defects.

Use cases: PCB solder comparison, print/label inspection, screen dead-pixel, component
orientation.

### Pipeline
```
golden template ┐
                ├─ align (ECC / feature) → diff → threshold → connected components
current image  ┘
```
`diff = |template − aligned_scene|` (both directions) or `template − min(template, scene)`
(darkening only).

### Alignment
| Mode | Use |
|---|---|
| None | fixed camera, precise part repeat |
| **ECC** (recommended) | sub-pixel; tolerates small translation/rotation |
| Feature-Based (ORB + homography) | larger warp/rotation |

ECC motion model: `Translation` (2), **`Euclidean`** (3, default), `Affine` (6).

### Diff parameters
| Parameter | Recommended |
|---|---|
| Diff Threshold | −1 (Otsu auto); 30–80 manual when noisy |
| Diff direction | Both (default) / darkening-only for missing material |
| Morph close size | 5 (fills holes); 0 = off |
| Min / Max Defect Area | 20 / 1000000 px² |

### ROI
A fixed window (no sliding): the scene is cropped to the ROI and compared with the template.
Capture the template from the **same region** of a known-good part; matching sizes give the
best result.

### Angular range
| Mode | Range |
|---|---|
| None | 0° |
| ECC Translation | ~0° |
| ECC Euclidean | ±10–15° |
| ECC Affine | ±15° (with scale/shear) |
| Feature-Based | any angle |

### Outputs
`No defects` or `DEFECT FOUND: N regions`, with per-region area + centroid (in full-image
coordinates) and a red overlay box.

### Common issues
| Symptom | Fix |
|---|---|
| Defects everywhere | alignment failed → use Feature-Based; verify the template |
| Many edge false-positives | alignment residual → use ECC; raise Min Defect Area |
| Missed defects | lower Diff Threshold / use −1 |
| Slow | smaller ROI; use Translation model |
| ECC doesn't converge | large offset → Feature-Based, or fix the part position |

---

## 6. Template Match

Locates a trained template using **gradient-orientation** features, tolerant of rotation
within a configured angle range. Use it for part / logo / pad presence, orientation and count
(e.g. "find all connectors", "check the pad angle").

### Model (template)
A template must be **trained** from a sample patch and saved as a model file; matching uses
`model.path`. The training parameters below are set when the model is created.

### Search ROI
A dedicated **template ROI** (drag the region to search) sets where the matcher looks;
`search_roi` in the recipe holds its rectangle. A tight ROI is faster and less prone to false
matches.

### Parameters
| Label (JSON key) | Meaning | Range / default |
|---|---|---|
| Min score (`score_threshold`) | minimum match score to accept | 0–100, default 70 |
| Max count (`max_count`) | maximum instances returned | 1–50, default 5 |
| Start angle (`train.angle_start`) | lowest rotation to train/look for | −180–180°, default −30 |
| End angle (`train.angle_end`) | highest rotation | −180–180°, default 30 |
| Angle step (`train.angle_step`) | rotation granularity | 1–30°, default 5 |
| Features (`train.num_features`) | number of template features | 16–256, default 200 |
| Weak gradient (`train.weak_threshold`) | low gradient threshold for feature extraction | 1–200, default 120 |
| Strong gradient (`train.strong_threshold`) | high gradient threshold | 1–255, default 255 |

### Outputs
cx, cy, **angle**, **match_score** — up to `max_count` instances, each with its rotation and
score. Feed them to constraints (e.g. `angle_diff`, `center_distance`).

### Tips
- Train the model from a clean, centered sample under the production lighting.
- Keep the angle range just wide enough for the real rotation (wider = slower, more false hits).
- Raise **Min score** until false matches disappear, then back off slightly.

### Common issues
| Symptom | Fix |
|---|---|
| No / weak matches | lower **Min score**; retrain from a matching sample (lighting/scale) |
| Many false matches | narrow the ROI / angle range; raise **Min score** |
| Angle off | lower **Angle step** (e.g. 1–2°) |
| Slow | tighter ROI + angle range; fewer **Features** |

---

## 7. OCR

Recognizes text with a **DNN** model or the built-in **dot-matrix** reader. Use it for serial
numbers, date codes, laser/ink markings and dot-matrix prints.

### ROI
A **rectangle** ROI around the text — the full string with a small margin, roughly horizontal
(a few degrees is fine).

### Parameters
| Label (JSON key) | Meaning | Range / default |
|---|---|---|
| Mode (`mode`) | `auto` / `dnn` / `dotmatrix` | default `auto` |
| Detect model (`det_model`) | text-detection model file (DNN) | path, optional |
| Recognize model (`rec_model`) | text-recognition model file (DNN) | path, optional |
| Confidence (`conf_thresh`) | minimum per-character confidence | 0–1, default 0.5 |
| CLAHE (`clahe`) | local-contrast pre-processing | off |
| Invert (`invert`) | invert polarity before reading | off |

- **Mode**: `auto` uses the DNN path when a recognize model is set, otherwise dot-matrix;
  force `dnn` or `dotmatrix` to pin the reader.
- **Models**: the DNN models are **provided by you** (not bundled) — set both a detection and a
  recognition model for best results.

### Outputs
`ocr_text`, `ocr_length`.

### Tips
- Light for even, high contrast on the characters (no glare, no shadow across the string).
- Set **Invert** if the text polarity is the opposite of what the reader expects.
- Enable **CLAHE** for uneven / low-contrast stamps.
- Dot-matrix: keep the dot pitch ≥ ~3 px; a magnified / higher-resolution view helps small
  stamps.

### Common issues
| Symptom | Fix |
|---|---|
| Empty / wrong text | improve lighting; toggle **Invert**; enable **CLAHE** |
| Partial string | enlarge the ROI |
| DNN not used | set `rec_model` or force **Mode** = `dnn` |
| Dot-matrix misread | zoom in / higher resolution; try **Mode** = `dotmatrix` |

---

## 8. Ellipse Fit

Fits an ellipse to an elliptical edge band. Use for ovals, seals, ring outer contours, and
where a circle fit is biased by slight ellipticity.

### ROI
An **elliptical annular sector**: center, inner/outer radii along **axis A** (major) and
**axis B** (minor), and an orientation angle. Size the band to straddle the elliptical edge.

### Parameters
| Label (JSON key) | Default | Meaning |
|---|---|---|
| Center X/Y (`roi.center.x/y`) | px | ROI center |
| Inner/Outer axis A (`roi.a_inner`/`a_outer`) | 50 / 150 px | major-axis band |
| Inner/Outer axis B (`roi.b_inner`/`b_outer`) | 30 / 120 px | minor-axis band |
| Angle (`roi.angle`) | 0° | ellipse orientation (−180–180) |
| Angle gap (`filter.angle_gap_deg`) | 30° | isolated-point gap |
| Eccentricity min/max (`filter.eccentricity_min/max`) | 0 / 1 | accept range |
| Axis A/B min/max (`ellipse_range.*`) | 0 / 2000 px | accepted axes |
| Inlier distance / Min inliers (`ransac.*`) | 3.0 px / 6 | robust fit |
| Edge method (`edge_detection.method`) | canny | canny / devernay / edge_profile |
| Tukey robust fit (`robust.tukey_refine`) / iterations / clipping | off / 200 / 10 | outlier rejection |

### Outputs
cx, cy, radius (= major axis A), **width, height, angle**.

### Tips / issues
- Set axis A ≥ axis B and match `roi.angle` to the tilt; **eccentricity min** > 0 rejects
  near-circles. Enable **Tukey** when the edge has gaps/outliers.
- Width/height swapped → axis A/B or `roi.angle` mismatched; jittery → raise **Min inliers**.

---

## 9. Rectangle Fit

Fits an oriented rectangle to a rectangular part/feature. Use for chips, pads, windows,
brackets.

### ROI
A **rotated rectangle** (center, width, height, angle) — enclose the whole rectangle.

### Parameters
| Label (JSON key) | Default | Meaning |
|---|---|---|
| Center X/Y (`roi.center.x/y`) | px | ROI center |
| Width/Height (`roi.width`/`height`) | 100 / 80 px | ROI size |
| Angle (`roi.angle`) | 0° | ROI orientation |
| Min/Max width (`size_ranges.width_min/max`) | 10 / 200 px | accepted width |
| Min/Max height (`size_ranges.height_min/max`) | 10 / 200 px | accepted height |
| Edge method + Canny/Sobel/Subpixel (`edge_detection.*`) | canny, 100/200, 3, 0.5 | edge extraction |
| RANSAC iterations/inlier/min-inliers (`ransac.*`) | 500 / 3.0 / 6 | robust fit |
| Min-area rect (`filter.use_min_area_rect`) | off | capsule mode for tilted bars |
| Angle tolerance (`filter.angle_tolerance`) | 10° | max edge-vs-expected angle |

### Outputs
cx, cy, **width, height, angle**.

### Tips / issues
- Match `roi.angle` to the tilt; set `size_ranges` to reject wrong-size fits. For elongated
  caps/leads try **Min-area rect** and raise **Angle tolerance**.
- Fails on rounded corners → raise **Inlier distance** / use min-area rect.

---

## 10. Scratch Detection

Detects linear surface marks (scratches) via morphological top-hat / black-hat. Use for
polished surfaces, glass, coatings.

### Parameters
| Label (JSON key) | Default | Meaning |
|---|---|---|
| Morph operator (`morph_op`) | both | tophat / blackhat / both / morphgrad |
| Struct width/height (`se_width`/`se_height`) | 30 / 30 | structuring element size |
| Threshold (`thresh_value`) | 20 | binarization threshold |
| Min area (`min_area`) | 5 px² | minimum scratch area |
| Min aspect ratio (`min_aspect_ratio`) | 2.0 | reject blobs, keep lines |

### Outputs
`defect_count`, `defect_total_area`.

### Tips / issues
- Struct element slightly **larger than the scratch width**; **top-hat** = bright scratches,
  **black-hat** = dark, **both** = unknown polarity. Raise **Min aspect ratio** (3–5) to keep
  only line-like defects.
- Too many blobs → raise **Threshold**/**Min area**/**Min aspect ratio**; texture picked up →
  larger struct / better lighting.

---

## 11. Crack Detection

Detects cracks as thin dark/bright paths (+ optional coverage judgement). Use for castings,
welds, ceramics.

### Parameters
| Label (JSON key) | Default | Meaning |
|---|---|---|
| Dark crack (`dark_crack`) | true | true = dark cracks |
| Top-hat size (`tophat_size`) | 15 | enhancement element |
| Canny low/high (`canny_low`/`canny_high`) | 30 / 90 | edge thresholds |
| Min length (`min_length`) | 20 px | minimum crack length |
| Coverage (`pass_coverage`) | 0.0 | max crack coverage to pass (0 = off) |

### Outputs
`crack_count`, `crack_coverage` (fraction of the ROI).

### Tips / issues
- **Dark crack** = false for bright cracks (some ceramics); tune Canny to the crack contrast;
  set **Coverage** > 0 to make the step judge PASS/NG by total crack fraction.
- Noise cracks → raise **Min length**/Canny; missed → lower Canny / match `tophat_size` to width.

---

## 12. Color Analysis

Highlights pixels inside an HSV range and reports region count + coverage, plus a k-means
dominant-color readout. Use for color presence/grading and coverage checks.

### Parameters
| Label (JSON key) | Default | Meaning |
|---|---|---|
| H/S/V low (`hsv_low_h/s/v`) | 0 / 0 / 0 | HSV lower bound |
| H/S/V high (`hsv_high_h/s/v`) | 179 / 255 / 255 | HSV upper bound |
| K-means colors (`kmeans_k`) | 5 | dominant colors reported |
| Morph close (`morph_close`) | 5 | fill small holes |
| Min area (`min_area`) | 50 px² | minimum region area |
| Uniformity (`check_uniformity`) / threshold / grid | off / 30 / 16 | evenness check |

### Outputs
Region count and coverage (shown in the result); dominant BGR colors with ratios.

> **Note**: Color Analysis is a **visualization / coverage** step — it does **not** currently
> emit scalar measurement fields, so it can't feed `measure_range`/constraints directly. For a
> **judged count**, use **Blob Analysis** (optionally after a color pre-filter).

### Tips
- Set the HSV bounds from a sample; H is 0–179 (OpenCV scale). Tighten **Min area** /
  enable **Morph close** to clean up regions.

---

## 13. Barcode / QR

Reads 1D barcodes and QR codes (ZXing).

### Parameters
| Label (JSON key) | Default | Meaning |
|---|---|---|
| Try QR (`try_qr`) | on | decode QR / 2D |
| Try linear (`try_linear`) | on | decode 1D barcodes |
| Scale factor (`scale_factor`) | 1.0 (0.1–10) | pre-scale before decoding |

### Outputs
`barcode_count`, `barcode_texts[]`, `barcode_first_text`.

### Tips / issues
- Raise **Scale factor** (2–4×) for small/high-density codes; even, glare-free lighting, code
  roughly level. No decode → raise scale / improve focus; wrong text → tighten ROI to one code.

---

## 14. DataMatrix

Reads ECC200 DataMatrix codes (incl. direct-part-marking).

### Parameters
| Label (JSON key) | Default | Meaning |
|---|---|---|
| Try inverted (`try_invert`) | on | also try inverted polarity |
| CLAHE (`use_clahe`) | on | local-contrast pre-processing |
| Multi-scale (`multi_scale`) | on | try several scales |

### Outputs
`dm_count`, `dm_texts[]`, `dm_first_text`.

### Tips / issues
- Keep **CLAHE** on for low-contrast/etched marks; **Multi-scale** helps size uncertainty. For
  dotted DPM codes, module size should be ≥ ~4 px.

---

## 15. General

### ROI interaction
Drag inside to move; drag the corner handles of a rectangle to resize; drag the sector center
to move it. Parameter edits update the ROI graphic.

### Result colors
Green = PASS (detected and within thresholds); red = NG.

### Tuning order
1. ROI first — cover the target, exclude everything else.
2. Sensitivity — Edge Thr (caliper) / Quality Level (corner).
3. Robustness — RANSAC iterations / Min Inliers.
