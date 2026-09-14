# Dataset Tools

Small Python helpers for preparing a YOLOv8 dataset from **videos** and **large (4K) images**.
Requires `pip install opencv-python` (and `numpy` for tiling).

| Script | Purpose |
|---|---|
| `extract_frames.py` | Extract frames from a video into an image folder |
| `tile_dataset.py` | Split large images into overlapping tiles with clipped labels |
| `split_dataset.py` | Collect annotated pairs into the flat layout the trainer consumes |

## Typical flow

```bash
# 1. Build an image set (skip if you already have images)
python extract_frames.py clip.mp4 frames/

# 2. Annotate    — open the tool → Annotate tab → pick frames/ → draw boxes → Save All

# 3. (4K images) tile into 640×640 overlapping tiles with clipped labels
python tile_dataset.py --input frames/            # -> frames/../tiled

# 4. Collect into the flat trainer layout
python split_dataset.py --input frames/           # (or the tiled dir)

# 5. Train        — Train tab → pick the split dir → Start Training
```
(The trainer holds out the validation set itself via its `val_split` parameter.)

## extract_frames.py

```bash
python extract_frames.py <video> <output_dir> [--step N] [--max N]
```
- `--step` — keep every N-th frame (default 1)
- `--max` — stop after N frames (default 1800; `0` = all)

Writes `frame_00000.jpg`, `frame_00001.jpg`, …

## tile_dataset.py

```bash
python tile_dataset.py --input <annotated_dir> [--output <dir>]
                       [--tile-size 640] [--overlap 160] [--min-vis 0.3]
```
Splits each image into `<tile-size>²` tiles with a given `--overlap`, and **clips the YOLO
boxes** to each tile; a box is kept only if at least `--min-vis` of its area falls inside the
tile. Accepts a flat folder or an `images/` + `labels/` layout; copies `classes.txt`.

> Recommended for **4K** images: one 4K image becomes ~30–50 training samples with targets near
> native size. Then train at `img_size = 640`.

## split_dataset.py

```bash
python split_dataset.py --input <annotated_dir> [--output <dir>]
```
Collects all image/label pairs into:

```
<output>/
├── images/
├── labels/
└── classes.txt
```

(the exact layout the C++ trainer reads). Accepts a flat folder or an `images/` + `labels/`
layout. No separate train/val folders are needed — the trainer splits validation internally.
