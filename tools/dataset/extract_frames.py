#!/usr/bin/env python3
"""extract_frames.py — Extract frames from a video to build a dataset.

Usage:
    python extract_frames.py <video> <output_dir> [--step N] [--max N]

  --step   keep every N-th frame (default 1)
  --max    stop after N frames (default 1800; 0 = all)

Output: frame_00000.jpg, frame_00001.jpg, ... in <output_dir>.
Requires: pip install opencv-python
"""
import argparse
import os

import cv2

ap = argparse.ArgumentParser(description="Extract frames from a video")
ap.add_argument("video", help="input video path")
ap.add_argument("output", help="output directory for frames")
ap.add_argument("--step", type=int, default=1, help="keep every N-th frame")
ap.add_argument("--max", type=int, default=1800, help="stop after N frames (0 = all)")
args = ap.parse_args()

os.makedirs(args.output, exist_ok=True)
cap = cv2.VideoCapture(args.video)
if not cap.isOpened():
    raise SystemExit(f"[error] cannot open video: {args.video}")

total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
print(f"video {w}x{h}, {total} frames; keeping 1/{args.step}")

limit = total if args.max <= 0 else min(args.max, total)
saved = 0
for i in range(limit):
    ok, frame = cap.read()
    if not ok:
        print(f"[warn] frame {i} read failed, stopping")
        break
    if i % args.step == 0:
        cv2.imwrite(os.path.join(args.output, f"frame_{i:05d}.jpg"),
                    frame, [cv2.IMWRITE_JPEG_QUALITY, 95])
        saved += 1
cap.release()
print(f"done: {saved} frames -> {args.output}")
