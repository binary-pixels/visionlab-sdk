# Training & Annotation Guide

Build a custom detector from your own images — **annotate** a dataset, **train** a YOLOv8
model in-tool (C++ / LibTorch, **no Python required**), **test** it, and **export** it to ONNX
for use in inspection. The anomaly-detection trainer (PatchCore) is covered separately in
[`PATCHCORE_GUIDE.md`](PATCHCORE_GUIDE.md).

## 1. Pipeline

```
images ──▶ [Annotate] ──▶ dataset (YOLO txt + classes.txt) ──▶ [Train YOLOv8]
                                                                   │
                                            best.pt ◀──────────────┘
                                               │
                              [Export ONNX] ───┴─── [Test] over a folder
                                   │
                                   ▼
                          use in inspection (defect / OCR) + annotation AI-assist
```

## 2. Annotation (Annotate tab)

1. **Open Folder** → pick the dataset root.
2. **Add Class** → type a class name (e.g. `chip`).
3. Drag a **rectangle box** around each target.
4. Navigate: Prev / Next (or `A` / `D`).
5. **Save All** → writes `labels/<stem>.txt` for each image.

Extras:
- **AI-assist** (optional): load an existing YOLO `.onnx` and click **Auto-Annotate This
  Image** / **Auto-Annotate All Unlabeled**.
  - **Confidence** — minimum detection score.
  - **Merge IoU** — post-NMS union merge (join overlapping boxes); `OFF` disables it.
  - **Class mapping** — map the model's classes to yours via `model:local`, e.g. `14:0,15:1`,
    or use the 🔍 probe to list the model's classes on the current image.
- **VLM OCR char-assist** (optional): recognize text in a selected box + projection-split it
  into **character-level** boxes (for OCR datasets).
- **Export** — COCO JSON.

> Add the classes **before** annotating: the class id order in `classes.txt` defines the label
> indices.

## 3. Dataset format

Two layouts are recognized:

**`images/` + `labels/` subfolders (recommended):**

```
dataset_root/
├── classes.txt          # one class name per line
├── images/  001.jpg 002.jpg ...
└── labels/  001.txt 002.txt ...
```

**Flat (no subfolders):**

```
dataset_root/
├── classes.txt
├── 001.jpg  001.txt
└── 002.jpg  002.txt
```

Each label line: `<class_id> <cx> <cy> <w> <h>`, normalized to `[0,1]`.

Images are letterbox-resized to `img_size × img_size`; each label list is padded to
`max_boxes` (default 100).

## 4. Large images (4K)

YOLOv8's default input is 640×640. Scaling a 3840×2160 image to 640 loses ~6× detail — a
300 px target becomes ~50 px, and small-object recall drops.

- **Option A — raise `img_size`** to 1280 (needs ≥ 8 GB VRAM).
- **Option B — tile (recommended for 4K)**: split a 4K image into overlapping 640×640 tiles
  (~160 px overlap). One 4K image becomes ~30–50 training samples, targets stay near native
  size.
- **Option C — tile then train at 640** for a good speed/accuracy balance.

A helper script ships with the toolkit: `tools/dataset/tile_dataset.py` (with clipped labels).

## 5. Gather the dataset for the trainer

`tools/dataset/split_dataset.py` collects all image/label pairs into the flat layout the
trainer reads:

```
split/
├── images/
├── labels/
└── classes.txt
```

No separate train/val folders are needed — the trainer holds out validation internally via its
`val_split` parameter (default 0.2).

## 6. Training (Train tab → YOLOv8)

**Model** — a YOLOv8 detector implemented from scratch in C++/LibTorch: **CSPDarknet** backbone
+ **PANet** neck + **decoupled head**. Presets `n / s / m / l / x` (nano ships ready; larger
sizes need matching channel/repeat config).

### GPU auto-detect

On opening the Train tab, CUDA is probed: a green banner shows the GPU name / VRAM / SM, a
yellow banner means CPU. When a GPU is found, **Use CUDA GPU** is checked and recommended
parameters are filled in.

### Recommended params by VRAM

| VRAM | Model | Input size | Batch |
|---|---|---|---|
| CPU / < 4 GB | n | 640 | 4 |
| 4–6 GB | n | 640 | 8 |
| 6–8 GB | n | 640 | 16 |
| 8–12 GB | s | 640 | 16 |
| 12–16 GB | s | 1280 | 8 |
| 16–24 GB | m | 1280 | 16 |
| ≥ 24 GB | l | 1280 | 32 |

### Parameters

| Parameter | Recommended | Notes |
|---|---|---|
| **Model** | n / s | nano for small datasets; larger = more accurate but slower |
| **Input size** | 640 / 1280 | larger = more accurate, VRAM grows quadratically |
| **Epochs** | 200 | small datasets (< 200 imgs): 200–300 |
| **Batch size** | per VRAM | **must not exceed the image count** (auto-clamped) |
| **Optimizer** | Adam | Adam converges fast; SGD+momentum generalizes slightly better |
| **LR** | 0.001 | Adam ~1e-3; SGD ~0.01 |
| **LR final** | 0.0001 | cosine end value; 0 makes the final LR too low |
| **Weight decay** | 0.0005 | regularization for small datasets |
| **Val split** | 0.20 | ≥ 0.15 when samples are few |
| **Use CUDA GPU** | auto | unchecked when no CUDA |

**Matching Ultralytics** — importing an ultralytics `args.yaml` overrides `lr`, `lrf`,
`weight_decay`, `momentum`, `epochs`, `nbs`, all augmentation parameters and the box/cls/dfl
loss gains. Augmentation (HSV jitter, flips, rotation, translate, scale, **mosaic**) follows
the Ultralytics defaults; **mosaic is disabled for the last `close_mosaic` epochs**, **EMA**
is kept for validation, and **warmup** ramps bias-LR / momentum. **Early stopping** (mAP50
patience) and **overfit detection** (val-loss ↑ while train-loss ↓) stop a diverging run.

**Fine-tuning** — set a pretrained checkpoint; the learning rate is scaled down (default
`0.1×`) and warmup is skipped for stable fine-tuning.

**Small datasets (< 100 images)**: `batch = min(0.8·N, 8)`, `epochs` 300–500,
`val_split = 0.20`, and tile first — avoid the large models (they overfit).

### Per-epoch metrics
box / cls / dfl / total loss, val loss, and **Precision / Recall / mAP50 / mAP50-95**.

## 7. Training output

```
<output_dir>/
├── train.log          # full timestamped log
├── best.pt            # best validation checkpoint
└── epoch_10.pt        # periodic checkpoints (save_period)
```

## 8. Export & use

- **Export TorchScript (.pt)** — deployable; load in C++ with `torch::jit::load()`.
- **Export ONNX (.onnx)** — generates a conversion script; run it to produce the `.onnx`:
  ```bash
  python <output_dir>/export_onnx.py
  ```

The `.onnx` model is consumed by the runtime's inference modules (defect detection / OCR DNN)
and by the annotation **AI-assist** (§2).

## 9. Tips & troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Crash on Start | uncaught LibTorch error | read the last `FATAL:` line in `train.log` |
| `FATAL c10::Error: CUDA` | GPU checked but no CUDA | uncheck **Use CUDA GPU** |
| `no images found` | wrong path / structure | confirm the folder layout in §3 |
| `batch > training samples` | auto-clamped, training continues | lower the batch size |
| OOM | insufficient VRAM | lower `img_size` or `batch` |
| val loss won't drop | LR too high/low | verify `lr ≈ 0.001`; check annotation quality |
| Poor accuracy on small objects | 4K scaled to 640 | tile (§4) + `img_size=640` |

- **≥ 200–500 images per class** for a solid detector; keep classes balanced.
- Start from a **pretrained checkpoint** for small datasets.
- Watch **`mAP50-95`** and the val-loss curve; export the `best.pt`.

## 10. Related

- [`PATCHCORE_GUIDE.md`](PATCHCORE_GUIDE.md) — anomaly (unknown-defect) training.
- [`RECIPE_GUIDE.md`](RECIPE_GUIDE.md) — using detection/measurement results in a recipe.
- [`ALGO_USAGE_GUIDE.md`](ALGO_USAGE_GUIDE.md) — OCR / template-match / defect algorithm tuning.
