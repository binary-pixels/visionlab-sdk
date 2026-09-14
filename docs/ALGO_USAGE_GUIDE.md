# Advanced Measurement Algorithms — Usage Guide

This guide covers the **advanced measurement** algorithms — **Arc Fit**, **Caliper**,
**Corner Detect**, **Blob Analysis**, and **Golden Template** — with their ROI setup,
parameters, outputs and common issues.

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

## 6. General

### ROI interaction
Drag inside to move; drag the corner handles of a rectangle to resize; drag the sector center
to move it. Parameter edits update the ROI graphic.

### Result colors
Green = PASS (detected and within thresholds); red = NG.

### Tuning order
1. ROI first — cover the target, exclude everything else.
2. Sensitivity — Edge Thr (caliper) / Quality Level (corner).
3. Robustness — RANSAC iterations / Min Inliers.
