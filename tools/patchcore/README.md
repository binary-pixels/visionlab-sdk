# PatchCore Toolchain

Python helpers for **unsupervised anomaly detection** with PatchCore.

Unlike a YOLOv8 detector, PatchCore never sees a defect. You give it a folder of
**defect-free** images and it records what "normal" looks like as a set of patch
features (the *memory bank*). At inference, any region that differs from every
"normal" feature is flagged, and a heatmap shows where. 5–10 good images are
often enough to start.

Requires `pip install torch torchvision opencv-python numpy tqdm scikit-learn`
(add `timm` only if you use the `vit_*` backbones).

| Script | Purpose |
|---|---|
| `export_backbone.py` | Export a DINOv2 / ViT backbone to TorchScript (run once) |
| `build_memory_bank.py` | Build the memory bank from good images (the "training" step) |
| `infer_heatmaps.py` | Score images, print AUROC, and write anomaly heatmaps |

## Pipeline

```
export_backbone.py ──▶ dinov2_vits14.pt        (backbone, once per install)
                              │
   good images ──▶ build_memory_bank.py ──▶ memory_bank.pt
                              │
                              ▼
                    infer_heatmaps.py  (validate + pick a threshold)
                              │
                              ▼
                    app: Train tab ▸ PatchCore  (same two .pt files)
```

## 1. Export the backbone (once)

```bash
python export_backbone.py --model dinov2_vits14 --size 224
# -> dinov2_vits14.pt   (FP32, ~85 MB)  + dinov2_vits14_fp16.pt (GPU only)
```

First run downloads the weights from `torch.hub` (needs network); afterwards the
`.pt` is a standalone TorchScript file.

The exported module takes a normalised `[1,3,S,S]` tensor and returns
`patch_tokens [1,N,D]` + `cls_token [1,D]`. Verify with the printed shapes:

```
patch_tokens : [1, 256, 384]   # 16x16 patches, 384 dims  (for 224 input, DINOv2-S)
cls_token    : [1, 384]
```

## 2. Build the memory bank

```bash
python build_memory_bank.py --good path/to/good_images
# -> memory_bank.pt
```

Key options:

| Option | Default | Meaning |
|---|---|---|
| `--size` | 224 | must match the backbone export |
| `--radius` | 1 | neighbourhood aggregation: `0` fine texture, `1` 3×3, `2` 5×5 |
| `--coreset` | 0.10 | fraction of patch vectors kept in the bank |
| `--cpu` | off | force CPU if no CUDA |

Optional sanity check — report AUROC / precision / recall against defect folders:

```bash
python build_memory_bank.py --good good/ --eval --defects defects/
```

Output is an ordered list `[bank, dist_min, dist_max, threshold]`. **Keep this
list format** — the runtime loader expects a vector of tensors, not a dict.
The threshold is auto-set to `mean + 3σ` of the training patch distances.

## 3. Validate and pick a threshold

```bash
# AUROC + per-category detection counts + heatmaps
python infer_heatmaps.py --bank memory_bank.pt --good good/ \
    --defects defects/broken_small defects/contamination

# Single image, interactive window (press any key to close)
python infer_heatmaps.py --bank memory_bank.pt --img test/part_01.jpg
```

Heatmaps go to `./defect_results/`, with the original image at 50 % opacity, the
JET heatmap over it, and a green `OK` / red `DEFECT` tag with the score.

The image score is the **99th percentile** of the patch-distance map, so a
single small defect still drives the score up.

## Using the same model files in the app

Train tab ▸ **PatchCore**:

| App field | Value |
|---|---|
| **Backbone (.pt)** | `dinov2_vits14.pt` (or the `_fp16` variant on GPU) |
| **Good images** | the folder you used for step 2 |
| **Output bank** | where to write `memory_bank.pt` |
| **Input size / Patch radius / Coreset ratio** | same values as step 2 |

The **Test** panel on that page loads a memory bank and runs a folder of test
images, writing annotated heatmaps — the in-app equivalent of `infer_heatmaps.py`.

## Tuning

| Parameter | Default | Guidance |
|---|---|---|
| `--radius` | 1 | `0` for very fine texture, `2` for rough/textured surfaces |
| `--coreset` | 0.10 | raise to `0.20` for few images (<20), lower to `0.05` for many (>100) |
| `--size` | 224 | changing it requires re-exporting the backbone at the same size |
| `threshold` | auto | raise it if you get false positives, lower it if you miss defects |

Aim for a healthy margin between the good-image scores and the defect scores
before you ship — the AUROC report shows both distributions per category.

## Notes

- **Models.** The backbone weights are downloaded from `torch.hub`
  (`facebookresearch/dinov2`); DINOv2 code/weights are Apache-2.0. Check the
  licence of any backbone you redistribute.
- **Determinism.** Training picks a random starting point for the coreset and a
  random sample for threshold statistics, so two runs give slightly different
  banks. The detection quality is stable; the exact score is not bit-identical.
- **GPU.** FP16 backbones run on CUDA only. The FP32 file works everywhere.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `[ERROR] Backbone not found` | `dinov2_vits14.pt` missing | run `export_backbone.py` |
| `torch::load` / `torch.load` raises | bank saved as a dict | rebuild with `build_memory_bank.py` (list format) |
| Heatmap all green (never flags) | threshold too high | lower the threshold or rebuild the bank |
| Heatmap all red (always flags) | threshold too low, or a different backbone than the bank was built with | check `--backbone` matches |
| AUROC < 85 % | too few / dirty good images | add good samples, make sure `good/` has no defects |
| `--fp16-only` errors | no CUDA GPU | use `--cpu` for an FP32 export |
