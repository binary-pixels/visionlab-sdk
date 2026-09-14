#!/usr/bin/env python3
"""tile_dataset.py — Tile large (e.g. 4K) annotated images into overlapping small
tiles for YOLOv8 training (the training-side of SAHI). Solves the small-object
problem: a 300 px target in a 4K image shrinks to ~50 px at 640, hurting recall.

Input : annotated images + YOLO .txt labels (flat, or images/ + labels/)
Output: <tile-size>² tiles + clipped labels + classes.txt

Usage:
    python tile_dataset.py --input <annotated_dir> [--output <dir>]
                           [--tile-size 640] [--overlap 160] [--min-vis 0.3]

  --overlap  overlap between tiles (default 160 px ≈ 25%)
  --min-vis  minimum visible bbox fraction to keep a box in a tile (default 0.3)

Requires: pip install opencv-python numpy
"""
import argparse
import os
import shutil

import cv2
import numpy as np

ap = argparse.ArgumentParser(description="Tile large annotated images for YOLOv8")
ap.add_argument("--input", "-i", required=True, help="annotated dataset dir")
ap.add_argument("--output", "-o", default=None, help="output dir (default: <input>/../tiled)")
ap.add_argument("--tile-size", "-s", type=int, default=640)
ap.add_argument("--overlap", "-p", type=int, default=160, help="overlap in px (default 25%)")
ap.add_argument("--min-vis", type=float, default=0.3, help="min visible bbox fraction to keep")
args = ap.parse_args()

SRC = args.input
OUT = args.output or os.path.join(os.path.dirname(os.path.abspath(SRC)), "tiled")
TS, OV, MIN_VIS = args.tile_size, args.overlap, args.min_vis
IMG_EXTS = {".jpg", ".jpeg", ".png", ".bmp"}

os.makedirs(os.path.join(OUT, "images"), exist_ok=True)
os.makedirs(os.path.join(OUT, "labels"), exist_ok=True)


def load_yolo_labels(txt_path, img_w, img_h):
    """Return [(cls, x1, y1, x2, y2)] in absolute pixels."""
    boxes = []
    if not os.path.exists(txt_path):
        return boxes
    with open(txt_path) as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) != 5:
                continue
            cls, cx, cy, bw, bh = int(parts[0]), *map(float, parts[1:])
            boxes.append((cls, (cx - bw / 2) * img_w, (cy - bh / 2) * img_h,
                          (cx + bw / 2) * img_w, (cy + bh / 2) * img_h))
    return boxes


def clip_box_to_tile(cls, x1, y1, x2, y2, tx, ty, ts):
    """Clip a box to the tile; keep it only if >= MIN_VIS of its area is inside."""
    orig_area = (x2 - x1) * (y2 - y1)
    if orig_area <= 0:
        return None
    cx1, cy1 = max(x1, tx), max(y1, ty)
    cx2, cy2 = min(x2, tx + ts), min(y2, ty + ts)
    if cx2 <= cx1 or cy2 <= cy1:
        return None
    if (cx2 - cx1) * (cy2 - cy1) / orig_area < MIN_VIS:
        return None
    return (cls, ((cx1 + cx2) / 2 - tx) / ts, ((cy1 + cy2) / 2 - ty) / ts,
            (cx2 - cx1) / ts, (cy2 - cy1) / ts)


if os.path.isdir(os.path.join(SRC, "images")):
    img_dir, lbl_dir = os.path.join(SRC, "images"), os.path.join(SRC, "labels")
else:
    img_dir = lbl_dir = SRC

stride = TS - OV
total_tiles = 0
for fname in sorted(os.listdir(img_dir)):
    name, ext = os.path.splitext(fname)
    if ext.lower() not in IMG_EXTS:
        continue
    img = cv2.imread(os.path.join(img_dir, fname))
    if img is None:
        continue
    H, W = img.shape[:2]
    boxes = load_yolo_labels(os.path.join(lbl_dir, name + ".txt"), W, H)

    tile_idx = 0
    for ty in range(0, H, stride):
        for tx in range(0, W, stride):
            tx = min(tx, W - TS) if W > TS else 0
            ty = min(ty, H - TS) if H > TS else 0
            tile = img[ty:ty + TS, tx:tx + TS]
            if tile.shape[0] != TS or tile.shape[1] != TS:
                pad = np.full((TS, TS, 3), 114, dtype=np.uint8)
                pad[:tile.shape[0], :tile.shape[1]] = tile
                tile = pad
            clipped = [r for r in (clip_box_to_tile(c, *b, tx, ty, TS) for c, *b in boxes) if r]
            stem = f"{name}_t{tile_idx:03d}"
            cv2.imwrite(os.path.join(OUT, "images", stem + ".jpg"),
                        tile, [cv2.IMWRITE_JPEG_QUALITY, 95])
            with open(os.path.join(OUT, "labels", stem + ".txt"), "w") as f:
                for (c, bx, by, bw, bh) in clipped:
                    f.write(f"{c} {bx:.6f} {by:.6f} {bw:.6f} {bh:.6f}\n")
            tile_idx += 1
            total_tiles += 1
            if tx + TS >= W:
                break
        if ty + TS >= H:
            break
    print(f"  {fname}: {W}x{H} -> {tile_idx} tiles")

for cand in (os.path.join(SRC, "classes.txt"), os.path.join(img_dir, "classes.txt")):
    if os.path.exists(cand):
        shutil.copy2(cand, os.path.join(OUT, "classes.txt"))
        break

print(f"\ndone: {total_tiles} tiles -> {OUT}")
print(f"tile {TS}x{TS}, overlap {OV}px")
print(f"next: python split_dataset.py --input {OUT}")
