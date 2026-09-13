# VisionLab vs HALCON — capability positioning

> **Disclaimer.** HALCON is a trademark of MVTec Software GmbH. This document is an
> independent, feature-level comparison based on publicly documented HALCON operators and
> on our own benchmark harness. It is not affiliated with, endorsed by, or sponsored by
> MVTec. Numbers are from our own test setup and may differ on your data — reproduce them
> on your own samples before making decisions.

The honest summary: **HALCON is the mature, broad reference** for machine vision, with a
huge operator set, decades of field validation, and a full 3D / deep-learning suite.
VisionLab is **narrower but deeper in one place**: it is a *delivery-first* runtime for
inspection machines (recipe + acceptance + reproducibility + unsupervised unknown-defect
coverage), sold as an embeddable per-device runtime rather than a general algorithm
toolbox. For many classical defect/measurement tasks the two are comparable; for breadth
of operators and 3D, HALCON wins; for "ship a machine without a vision engineer", VisionLab
is the intended fit.

---

## 1. Capability map (feature-level)

| Task | HALCON (operator family) | VisionLab | Notes |
|---|---|---|---|
| Sub-pixel circle / line / ellipse / rectangle fit | `*_contour_xld` + `fit_*_contour_xld` | ✅ sub-pixel fits with robust RANSAC + annular / rotated-rect ROIs | Comparable accuracy class; see §2 |
| Shape template match | `create_shape_model` + `find_shape_model` | ✅ gradient-orientation, rotation-invariant | Both rotation-invariant; HALCON adds scale/anisotropy and richer ROI, we cover rotation |
| Blob / connected components | `connection` + `select_shape` (≈30 region features) | ✅ area / circularity / aspect filters | HALCON's `select_shape` feature set is broader (convexity, moments, compactness…) |
| Golden-template / reference diff | `compare_gray` (absolute / light / dark) | ✅ alignment + per-pixel diff | Direction aligned; HALCON's multi-mode + region post-processing is more mature |
| Scratch / linear-structure defects | `lines_gauss` (sub-pixel linear structures) | ✅ morphology + length/width filters | HALCON's sub-pixel ridge extraction is more precise on fine scratches |
| Crack / skeleton metrics | morphology + `skeleton` + fractal measures | ✅ skeleton + length/width | HALCON offers richer professional metrics (fractal dimension, density) |
| Texture / periodic defect (Gabor, FFT, Mura) | `gen_gabor` / FFT filters / background suppression | ✅ Gabor, FFT, Mura | Same underlying approach; HALCON's parameter surface is deeper |
| OCR / barcode / DataMatrix | `*_ocr_class_*`, `find_bar_code`, `find_data_code_2d` | ✅ OCR (DNN / dot-matrix / VLM), barcode/QR, DataMatrix | HALCON's code readers are very mature; we integrate open engines |
| Classification / detection (DL) | HALCON DL (classify / detection / segmentation) | ✅ ONNX YOLO, ONNX/transformer defect detection | Both are DL; HALCON's DL tooling (labeling, training) is more turnkey |
| **Unsupervised anomaly detection** | — (not the classical focus) | ✅ **PatchCore-style: build from good parts, flag unknown defects** | **Our main differentiator** — covers defects you never modeled |
| **Recipe + acceptance workflow** | via procedures/tooling | ✅ **recipe engine + GRR / 6σ reports + per-execution debug capture** | Delivery-first: teach the machine, prove acceptance, reproduce failures |
| 3D vision | ✅ full suite | — (2D runtime) | HALCON clearly ahead |

---

## 2. Benchmark (our harness, reproducible)

We compare against **HALCON reference chains** built for the same images, on public/synthetic
samples, and report detection counts. Representative results:

| Case | HALCON reference chain | VisionLab | Outcome |
|---|---|---|---|
| BGA dark-pad blob | `threshold + connection + select_shape` | 1 region (dark polarity) | aligned — area 45 317 vs 41 822 px |
| Surface scratch | `mean_image + dyn_threshold + select_shape(anisometry)` | 4 defects | aligned — 4 real ~79° scratches; ratio 0.80 vs HALCON |

Method: identical input images; HALCON chain tuned to a reasonable working point; VisionLab
run with documented parameters. This is a **capability check, not a product claim** — always
re-validate on your own parts, lighting, and acceptance criteria.

---

## 3. Where each wins

**Choose HALCON when** you need maximum operator breadth, mature 3D, a full DL training
toolchain, and decades of validated edge cases.

**Choose VisionLab when** your priority is *delivery*: embed a per-device runtime, teach the
inspection as a recipe, get an acceptance report, and cover unknown defects with unsupervised
anomaly detection — without standing up a vision-algorithm team.

**Use both** is legitimate too: VisionLab's out-of-process host SDK can consume results
alongside your existing HALCON stack.

---

## 4. How to evaluate fairly

1. Use **your own** defect/measurement samples and acceptance criteria.
2. Fix the optical setup and lighting (the #1 cause of vision-project failure is lighting,
   not the algorithm).
3. Measure repeatability (GRR / σ) and false-positive/false-negative rates, not just a single
   detection count.
4. Reproduce failures — VisionLab saves the original image, the result overlay and the exact
   parameters per execution so any NG is auditable.
