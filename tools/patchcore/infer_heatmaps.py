#!/usr/bin/env python3
"""infer_heatmaps.py — Run PatchCore inference and produce anomaly heatmaps +
AUROC evaluation with a memory bank built by build_memory_bank.py.

This is step 3 of 3 — use it to sanity-check a memory bank and pick a threshold
before deploying. The same .pt files are loaded by the app.

Usage:
    # Evaluate good vs. defect folders, print AUROC, and write heatmaps
    python infer_heatmaps.py --bank memory_bank.pt --good good/ --defects defects/

    # Several defect categories
    python infer_heatmaps.py --bank memory_bank.pt --good good/ \
        --defects defects/broken_small defects/contamination

    # Interactive single-image display (needs a GUI; press any key to close)
    python infer_heatmaps.py --bank memory_bank.pt --img test/part_01.jpg

    # Skip the heatmaps, print metrics only
    python infer_heatmaps.py --bank memory_bank.pt --good good/ --defects defects/ --no-heatmaps

Dependencies:
    pip install torch torchvision opencv-python numpy tqdm scikit-learn
"""

import argparse
from pathlib import Path

import cv2
import numpy as np
import torch
import torch.nn.functional as F
from tqdm import tqdm

# Reuse helpers from the memory-bank builder
from build_memory_bank import (
    MEAN, STD,
    list_images,
    extract_patches,
    aggregate_neighbourhood,
)


def load_image(path: Path, size: int, device: torch.device) -> torch.Tensor:
    bgr = cv2.imread(str(path))
    if bgr is None:
        raise FileNotFoundError(path)
    rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
    rgb = cv2.resize(rgb, (size, size), interpolation=cv2.INTER_LINEAR)
    t   = torch.from_numpy(rgb).permute(2, 0, 1).float() / 255.0
    return ((t.unsqueeze(0) - MEAN) / STD).to(device)


def load_memory_bank(path: Path):
    """Returns (bank [K,D], dist_min, dist_max, threshold).

    Reads the list format [bank, dist_min, dist_max, threshold] written by
    build_memory_bank.save_memory_bank.
    """
    data = torch.load(str(path), weights_only=True)
    return (data[0],
            float(data[1].item()),
            float(data[2].item()),
            float(data[3].item()))


# ---------------------------------------------------------------------------
#  Inference core
# ---------------------------------------------------------------------------
def infer_single(img_t: torch.Tensor, backbone, bank_n: torch.Tensor,
                 device: torch.device, radius: int):
    """Returns (image_score, score_map_uint8, heatmap_bgr).

    image_score is the 99th percentile of the patch-distance map — i.e. the
    worst local region decides, which is what you want for localised defects.
    """
    img_t   = img_t.to(device)
    patches = extract_patches(backbone, img_t, device)   # [N, D]
    N, D    = patches.shape
    gH      = int(N ** 0.5)
    gW      = N // gH
    patches = aggregate_neighbourhood(patches, gH, gW, radius)

    pn   = F.normalize(patches, dim=1)                        # [N, D]
    sims = pn @ bank_n.to(pn.device).T                        # [N, K]
    dist = (1.0 - sims).clamp(0.0).min(dim=1).values          # [N]

    dist_map = dist.reshape(gH, gW).detach().cpu().numpy()

    size = img_t.shape[-1]
    score_map = cv2.resize(dist_map, (size, size), interpolation=cv2.INTER_LINEAR)
    score_map = cv2.GaussianBlur(score_map, (11, 11), 4.0)

    image_score = float(np.percentile(score_map, 99))

    # Normalise to [0,255] for visualisation
    d_min, d_max = dist_map.min(), dist_map.max()
    range_v = (d_max - d_min) if d_max > d_min + 1e-6 else 1.0
    norm = ((score_map - d_min) / range_v * 255).clip(0, 255).astype(np.uint8)
    heatmap = cv2.applyColorMap(norm, cv2.COLORMAP_JET)

    return image_score, norm, heatmap


# ---------------------------------------------------------------------------
#  Evaluate AUROC
# ---------------------------------------------------------------------------
def evaluate_auroc(good_dir, defect_dirs, backbone, bank, device,
                   size: int, radius: int, threshold: float) -> None:
    try:
        from sklearn.metrics import roc_auc_score, average_precision_score
    except ImportError:
        print("[WARN] scikit-learn not installed, skipping AUROC.")
        return

    bank_n = F.normalize(bank, dim=1)
    scores, labels = [], []

    if good_dir is not None and good_dir.exists():
        for p in tqdm(list_images(good_dir), desc="good"):
            t = load_image(p, size, device)
            s, _, _ = infer_single(t, backbone, bank_n, device, radius)
            scores.append(s); labels.append(0)

    per_cat = {}
    for ddir in defect_dirs:
        if not ddir.exists():
            print(f"[WARN] defect folder not found, skipped: {ddir}")
            continue
        cat_scores = []
        for p in tqdm(list_images(ddir), desc=ddir.name):
            t = load_image(p, size, device)
            s, _, _ = infer_single(t, backbone, bank_n, device, radius)
            scores.append(s); labels.append(1)
            cat_scores.append(s)
        per_cat[ddir.name] = cat_scores

    if not any(l == 1 for l in labels):
        print("[WARN] no defect images found; AUROC not computed.")
        return

    auroc = roc_auc_score(labels, scores)
    ap    = average_precision_score(labels, scores)

    tp = sum(1 for s, l in zip(scores, labels) if l == 1 and s >= threshold)
    fp = sum(1 for s, l in zip(scores, labels) if l == 0 and s >= threshold)
    fn = sum(1 for s, l in zip(scores, labels) if l == 1 and s < threshold)
    tn = sum(1 for s, l in zip(scores, labels) if l == 0 and s < threshold)

    print("\n-- Evaluation ------------------------------------------------")
    print(f"  Image AUROC        : {auroc*100:.2f}%")
    print(f"  Average Precision  : {ap*100:.2f}%")
    print(f"  Threshold          : {threshold:.4f}")
    print(f"  TP={tp}  FP={fp}  FN={fn}  TN={tn}")
    prec = tp / (tp + fp + 1e-9)
    rec  = tp / (tp + fn + 1e-9)
    print(f"  Precision={prec:.3f}  Recall={rec:.3f}  F1={2*prec*rec/(prec+rec+1e-9):.3f}")
    if per_cat:
        print()
        good_sc = [s for s, l in zip(scores, labels) if l == 0]
        if good_sc:
            print(f"  good                : {len(good_sc)} images, mean={np.mean(good_sc):.4f}")
        for cat, csc in per_cat.items():
            print(f"  {cat:<20}: {len(csc)} images, mean={np.mean(csc):.4f}  "
                  f"detected={sum(1 for s in csc if s>=threshold)}/{len(csc)}")
    print("--------------------------------------------------------------\n")


# ---------------------------------------------------------------------------
#  Save annotated heatmaps
# ---------------------------------------------------------------------------
def save_heatmaps(images, backbone, bank_n, device, size, radius,
                  threshold, out_dir: Path, label: str) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for p in tqdm(images, desc=f"  heatmaps/{label}"):
        bgr_orig = cv2.imread(str(p))
        if bgr_orig is None:
            continue
        t = load_image(p, size, device)
        img_score, _, heatmap = infer_single(t, backbone, bank_n, device, radius)
        is_defect = img_score >= threshold

        h, w = bgr_orig.shape[:2]
        hm_r = cv2.resize(heatmap, (w, h))
        overlay = cv2.addWeighted(bgr_orig, 0.5, hm_r, 0.5, 0)

        tag_color = (0, 0, 220) if is_defect else (0, 200, 0)
        tag_text  = (f"DEFECT  score={img_score:.3f}" if is_defect
                     else f"OK  score={img_score:.3f}")
        cv2.putText(overlay, tag_text, (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.9, tag_color, 2)

        cv2.imwrite(str(out_dir / f"{p.stem}_heatmap.jpg"), overlay)

    print(f"    Saved to {out_dir}")


# ---------------------------------------------------------------------------
#  Single-image mode
# ---------------------------------------------------------------------------
def infer_one(img_path: Path, backbone, bank: torch.Tensor,
              threshold: float, size: int, radius: int,
              device: torch.device) -> None:
    bank_n   = F.normalize(bank, dim=1)
    bgr_orig = cv2.imread(str(img_path))
    if bgr_orig is None:
        print(f"[ERROR] Cannot read {img_path}")
        return

    t = load_image(img_path, size, device)
    img_score, _, heatmap = infer_single(t, backbone, bank_n, device, radius)
    is_defect = img_score >= threshold

    h, w = bgr_orig.shape[:2]
    hm_r = cv2.resize(heatmap, (w, h))
    overlay = cv2.addWeighted(bgr_orig, 0.5, hm_r, 0.5, 0)

    tag = f"{'DEFECT' if is_defect else 'OK'}  score={img_score:.4f}  thresh={threshold:.4f}"
    color = (0, 0, 220) if is_defect else (0, 200, 0)
    cv2.putText(overlay, tag, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, color, 2)
    cv2.imshow("PatchCore", overlay)
    cv2.waitKey(0)
    cv2.destroyAllWindows()
    print(f"  score={img_score:.4f}  threshold={threshold:.4f}  "
          f"result={'DEFECT' if is_defect else 'OK'}")


# ---------------------------------------------------------------------------
#  Entry point
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="PatchCore inference: anomaly heatmaps + AUROC")
    parser.add_argument("--backbone", default="dinov2_vits14.pt",
                        help="TorchScript backbone .pt (default: ./dinov2_vits14.pt)")
    parser.add_argument("--bank", default="memory_bank.pt",
                        help="Memory bank .pt (default: ./memory_bank.pt)")
    parser.add_argument("--good", default=None,
                        help="Good images folder, scored as label 0 in the AUROC report")
    parser.add_argument("--defects", nargs="+", default=[],
                        help="Defect image folder(s), scored as label 1")
    parser.add_argument("--size", type=int, default=224,
                        help="Input image size, must match the backbone export (default: 224)")
    parser.add_argument("--radius", type=int, default=1,
                        help="Patch neighbourhood radius (default: 1)")
    parser.add_argument("--out", default="defect_results",
                        help="Output folder for heatmaps (default: ./defect_results)")
    parser.add_argument("--no-heatmaps", action="store_true",
                        help="Skip writing heatmap images")
    parser.add_argument("--img", default=None,
                        help="Single image path for interactive display")
    parser.add_argument("--cpu", action="store_true",
                        help="Force CPU even if CUDA is available")
    args = parser.parse_args()

    device  = torch.device("cpu" if args.cpu or not torch.cuda.is_available() else "cuda")
    bb_path = Path(args.backbone)
    bk_path = Path(args.bank)
    out_dir = Path(args.out)

    for p, name in [(bb_path, "backbone"), (bk_path, "memory bank")]:
        if not p.exists():
            print(f"[ERROR] {name} not found: {p}")
            return

    print(f"[*] Loading backbone: {bb_path}")
    backbone = torch.jit.load(str(bb_path), map_location=device).eval()
    print(f"[*] Loading memory bank: {bk_path}")
    bank, d_min, d_max, threshold = load_memory_bank(bk_path)
    bank = bank.to(device)
    print(f"    Bank: {bank.shape[0]} vectors x {bank.shape[1]} dims")
    print(f"    Threshold: {threshold:.4f}")

    if args.img:
        infer_one(Path(args.img), backbone, bank,
                  threshold, args.size, args.radius, device)
        return

    defect_dirs = [Path(d) for d in args.defects]
    good_dir    = Path(args.good) if args.good else None

    evaluate_auroc(good_dir, defect_dirs, backbone, bank,
                   device, args.size, args.radius, threshold)

    if not args.no_heatmaps:
        bank_n = F.normalize(bank, dim=1)
        print(f"[*] Saving heatmaps to {out_dir} ...")
        if good_dir is not None and good_dir.exists():
            save_heatmaps(list_images(good_dir), backbone, bank_n,
                          device, args.size, args.radius, threshold,
                          out_dir / "good", "good")
        for ddir in defect_dirs:
            if ddir.exists():
                save_heatmaps(list_images(ddir), backbone, bank_n,
                              device, args.size, args.radius, threshold,
                              out_dir / ddir.name, ddir.name)


if __name__ == "__main__":
    main()
