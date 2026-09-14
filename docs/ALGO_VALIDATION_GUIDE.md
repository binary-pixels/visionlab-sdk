# Algorithm Validation Guide

How a VisionLab inspection recipe is validated for **accuracy**, **robustness**, and
**repeatability** before a line goes live. Use this when proving acceptance to a quality
department, or when defining your own acceptance tests.

> The runtime ships its own GRR / repeatability tooling. This document describes the
> methodology and the industry-standard metrics to report.

## 1. The three-level validation method

Most measurement failures come from optics/lighting, not the algorithm — so validate in this
order.

### Level 1 — System error (calibration plate)

- Input: a high-precision glass calibration target (grid/dot pitch accurate to ±1 µm).
- Measure: known distances (dot spacing, circle centers) vs. nominal.
- **Acceptance: systematic error < ±1 px; repeatability < 0.3 px.**

### Level 2 — Algorithm error (synthetic images)

- Generate synthetic circles / lines / rectangles with known parameters, add Gaussian noise
  (σ = 2, 5, 10).
- Compare the fit to the ground truth.
- **Acceptance: RMSE < 0.1 px (noise-free); < 0.3 px at σ = 5.**

### Level 3 — Robustness (real samples)

- 100+ real parts, each with a manually verified ground truth.
- Compute precision / recall against the ground truth.
- **Acceptance: F1 > 0.98; process capability Cpk ≥ 1.33.**

## 2. Pre-delivery verification checklist

**Phase 1 — Optics**
- Camera intrinsics (`px_per_mm`, distortion): reprojection RMS < 0.5 px.
- Straightness of a known straight edge: deviation < 1 px.
- Field-of-view / scale confirmed against a known-size target.

**Phase 2 — Algorithm unit tests**
- Synthetic → real images, with and without noise.
- ≥30 standard parts, measured 10× each.
- Extreme conditions: over/under-exposure, tilt, occlusion (≥20 parts each).

**Phase 3 — System integration**
- 24 h continuous run (watch for leaks).
- Concurrent multi-threaded processing.
- Fault handling: empty / corrupt / oversized images.

**Phase 4 — Statistical process control**
- Sample 100 parts, plot control charts.
- **Cpk ≥ 1.67** (a 6σ process) for production stability.
- Alarm at 3σ.

## 3. Metrics to report

| Metric | Definition | Industrial target |
|---|---|---|
| Precision | TP / (TP + FP) | > 99% |
| Recall | TP / (TP + FN) | > 99.5% |
| F1 | 2·P·R / (P + R) | > 99% |
| Cpk | min(USL−µ, µ−LSL) / 3σ | ≥ 1.67 |
| AUROC | area under ROC | > 0.99 |
| mAP@0.5 | mean average precision (detection) | > 0.95 |
| PSNR | peak signal-to-noise ratio (restoration) | > 30 dB |
| SSIM | structural similarity | > 0.95 |

Report the numbers on **your** samples — never rely on a supplier's headline figure.

## 4. Benchmarking method

Warm up, then average over many runs, on the target hardware:

```cpp
for (int i = 0; i < 10; i++) runAlgo(img);          // warm up
auto t0 = cv::getTickCount();
for (int i = 0; i < 100; i++) runAlgo(img);
double ms = (cv::getTickCount() - t0) * 10.0 / cv::getTickFrequency();
```

Test at the resolutions you actually run: 640×480, 1280×960, 4096×3000; single vs. multi-thread.

## 5. Relevant standards

| Standard | Subject |
|---|---|
| ISO 5436-1 | surface roughness measurement |
| ISO 1328-1 | gear accuracy grades |
| ISO 724 | screw threads |
| ISO/IEC 15415 / 15416 | 2D / 1D barcode print quality |
| ISO 13320 | laser-diffraction particle sizing |
| IPC-A-610 | acceptability of electronic assemblies |
| ISO 5817 | weld quality levels |
| CIE 15:2004 | colorimetry |
| ISO 13655 | spectrophotometric measurement |

## 6. Camera / lens selection (rule of thumb)

- **Resolution** ≈ smallest feature ÷ required positioning accuracy × 5–10. Range 0.5–25 MP.
- **Frame rate** ≥ line speed with ~30% headroom. 30–500 fps.
- **Interface** bandwidth ≥ pixel clock × bit depth (USB3 / GigE / Camera Link / CoaXPress).
- **Lens** image circle ≥ sensor diagonal × 1.1; resolution ≥ sensor Nyquist; distortion
  < 0.1% for metrology, < 1% for general detection.
- **Moving parts** need a **global-shutter** camera (avoid rolling-shutter smear).
