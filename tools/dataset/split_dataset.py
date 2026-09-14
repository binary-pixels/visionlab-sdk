#!/usr/bin/env python3
"""split_dataset.py — Collect annotated images + YOLO labels into the flat
structure the C++ trainer consumes. (The trainer holds out validation itself via
its `val_split` parameter, so no separate train/val folders are needed.)

Output:
    <output>/
      images/     all annotated images
      labels/     matching .txt labels
      classes.txt

Usage:
    python split_dataset.py --input <annotated_dir> [--output <dir>]

Accepts either a flat folder (images + .txt side by side) or an
`images/` + `labels/` layout.
"""
import argparse
import os
import shutil
import sys

ap = argparse.ArgumentParser(description="Flatten a YOLO dataset for the C++ trainer")
ap.add_argument("--input", "-i", required=True, help="annotated dataset directory")
ap.add_argument("--output", "-o", default=None, help="output dir (default: <input>/../split)")
args = ap.parse_args()

SRC = args.input
OUT = args.output or os.path.join(os.path.dirname(os.path.abspath(SRC)), "split")
IMG_EXTS = {".jpg", ".jpeg", ".png", ".bmp"}

pairs = []


def scan(img_dir, lbl_dir):
    if not os.path.isdir(img_dir):
        return
    for f in sorted(os.listdir(img_dir)):
        name, ext = os.path.splitext(f)
        if ext.lower() not in IMG_EXTS:
            continue
        txt = os.path.join(lbl_dir, name + ".txt")
        img = os.path.join(img_dir, f)
        if os.path.exists(txt):
            pairs.append((img, txt))


if os.path.isdir(os.path.join(SRC, "images")):
    scan(os.path.join(SRC, "images"), os.path.join(SRC, "labels"))
else:
    scan(SRC, SRC)

if not pairs:
    print("[error] no annotated images found (need matching .txt files)")
    sys.exit(1)

print(f"annotated images: {len(pairs)}")
os.makedirs(os.path.join(OUT, "images"), exist_ok=True)
os.makedirs(os.path.join(OUT, "labels"), exist_ok=True)

for img_src, txt_src in pairs:
    shutil.copy2(img_src, os.path.join(OUT, "images", os.path.basename(img_src)))
    shutil.copy2(txt_src, os.path.join(OUT, "labels", os.path.basename(txt_src)))

classes_src = os.path.join(SRC, "classes.txt")
if not os.path.exists(classes_src):
    classes_src = os.path.join(os.path.dirname(os.path.abspath(SRC)), "classes.txt")
if os.path.exists(classes_src):
    shutil.copy2(classes_src, os.path.join(OUT, "classes.txt"))

print(f"done: {len(pairs)} images -> {OUT}/images + labels + classes.txt")
