#!/usr/bin/env python3
"""build_memory_bank.py — Build a PatchCore memory bank from "good" (defect-free)
images using a backbone exported by export_backbone.py.

PatchCore is unsupervised: it never sees a defect. It simply records what
"normal" looks like as a set of patch features (the *memory bank*). At inference
any patch that is far from that memory is flagged as anomalous.

This is step 2 of 3. The .pt it produces is what you load in the app
(Train tab -> PatchCore -> Output bank) or in infer_heatmaps.py.

Usage:
    python build_memory_bank.py --good <good_images_dir>
        [--backbone dinov2_vits14.pt] [--out memory_bank.pt]
        [--size 224] [--radius 1] [--coreset 0.10] [--cpu]

    # Also report AUROC / precision / recall against a defect folder:
    python build_memory_bank.py --good good/ --eval --defects defect/

    # A defect tree with several categories:
    #   defects/broken_small  defects/contamination  ...
    python build_memory_bank.py --good good/ --eval --defects defects/*/

Dependencies:
    pip install torch torchvision opencv-python numpy tqdm scikit-learn
"""

import argparse
import time
from pathlib import Path

import cv2
import numpy as np
import torch
import torch.nn.functional as F
from tqdm import tqdm

# ImageNet normalisation (must match export_backbone.py)
MEAN = torch.tensor([0.485, 0.456, 0.406]).view(1, 3, 1, 1)
STD  = torch.tensor([0.229, 0.224, 0.225]).view(1, 3, 1, 1)


# ---------------------------------------------------------------------------
#  Image helpers
# ---------------------------------------------------------------------------
def load_image(path: Path, size: int) -> torch.Tensor:
    """Load BGR image -> normalised float32 tensor [1, 3, H, W]."""
    bgr = cv2.imread(str(path))
    if bgr is None:
        raise FileNotFoundError(f"Cannot read image: {path}")
    rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
    rgb = cv2.resize(rgb, (size, size), interpolation=cv2.INTER_LINEAR)
    t   = torch.from_numpy(rgb).permute(2, 0, 1).float() / 255.0
    return ((t.unsqueeze(0) - MEAN) / STD)


def list_images(folder: Path) -> list:
    exts = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff"}
    return sorted(p for p in folder.iterdir() if p.suffix.lower() in exts)


# ---------------------------------------------------------------------------
#  Feature extraction helpers
# ---------------------------------------------------------------------------
def extract_patches(backbone, img_t: torch.Tensor, device: torch.device) -> torch.Tensor:
    """Run the backbone, return patch tokens [N, D] (no CLS, no batch dim)."""
    with torch.no_grad():
        patch, _ = backbone(img_t.to(device))   # patch: [1, N, D]
    return patch.squeeze(0).cpu()               # [N, D]


def aggregate_neighbourhood(patches: torch.Tensor,
                            grid_h: int, grid_w: int,
                            radius: int = 1) -> torch.Tensor:
    """Average each patch with its (2r+1)^2 spatial neighbours.

    patches: [N, D]  where N = grid_h * grid_w
    Returns: [N, D]
    """
    if radius == 0:
        return patches
    N, D = patches.shape
    assert N == grid_h * grid_w
    feat_map = patches.view(1, grid_h, grid_w, D).permute(0, 3, 1, 2)  # [1,D,H,W]
    kernel   = 2 * radius + 1
    padding  = radius
    pooled = F.avg_pool2d(feat_map, kernel_size=kernel,
                          stride=1, padding=padding)  # [1,D,H,W]
    return pooled.squeeze(0).permute(1, 2, 0).reshape(N, D)             # [N, D]


# ---------------------------------------------------------------------------
#  Greedy coreset subsampling (minimax distance)
# ---------------------------------------------------------------------------
def greedy_coreset(feats: torch.Tensor, ratio: float) -> torch.Tensor:
    """Select a coreset of size floor(ratio*N) from feats [N, D].

    Greedy minimax-distance selection (PatchCore, Roth et al. 2022): each new
    point is the one farthest from everything already selected, so the chosen
    vectors spread out over the whole feature space.
    """
    N = feats.shape[0]
    k = max(1, int(ratio * N))
    if k >= N:
        return feats

    print(f"  Coreset: {N} -> {k} vectors ({ratio*100:.0f}%)...")

    feats_n = F.normalize(feats, dim=1)   # cosine distances

    selected  = [torch.randint(N, (1,)).item()]
    min_dists = torch.full((N,), float("inf"))

    for _ in tqdm(range(k - 1), desc="  coreset", leave=False):
        last = feats_n[selected[-1]].unsqueeze(0)      # [1, D]
        sims = (feats_n @ last.T).squeeze(1)           # [N]
        dists = (1.0 - sims).clamp(min=0.0)
        min_dists = torch.minimum(min_dists, dists)
        selected.append(int(min_dists.argmax().item()))

    return feats[selected]


# ---------------------------------------------------------------------------
#  Build memory bank
# ---------------------------------------------------------------------------
def build_memory_bank(good_dir: Path,
                      backbone,
                      device: torch.device,
                      input_size: int,
                      patch_radius: int,
                      coreset_ratio: float):
    """Returns (memory_bank [K,D], dist_min, dist_max, threshold)."""
    images = list_images(good_dir)
    if not images:
        raise FileNotFoundError(f"No images found in {good_dir}")
    print(f"[*] Building memory bank from {len(images)} good images...")

    # Infer the patch grid from one forward pass
    dummy   = load_image(images[0], input_size)
    patches = extract_patches(backbone, dummy, device)  # [N, D]
    N, D    = patches.shape
    grid_h  = int(N ** 0.5)
    grid_w  = N // grid_h
    print(f"    Patch grid: {grid_h}x{grid_w}, embed_dim={D}")

    all_feats = []
    for img_path in tqdm(images, desc="  extracting"):
        t       = load_image(img_path, input_size)
        patches = extract_patches(backbone, t, device)      # [N, D]
        patches = aggregate_neighbourhood(patches, grid_h, grid_w, patch_radius)
        all_feats.append(patches)

    raw  = torch.cat(all_feats, dim=0)   # [total_patches, D]
    bank = greedy_coreset(raw, coreset_ratio)

    # Training distances -> baseline threshold
    bank_n = F.normalize(bank, dim=1)
    idx    = torch.randperm(raw.shape[0])[:2000]        # sample to avoid OOM
    sample = F.normalize(raw[idx], dim=1)
    dists  = (1.0 - sample @ bank_n.T).min(dim=1).values
    dist_min  = float(dists.min().item())
    dist_max  = float(dists.max().item())
    threshold = float(dists.mean().item() + 3.0 * dists.std().item())
    print(f"    dist range: [{dist_min:.4f}, {dist_max:.4f}]  "
          f"auto-threshold: {threshold:.4f}")

    return bank, dist_min, dist_max, threshold


# ---------------------------------------------------------------------------
#  Save
# ---------------------------------------------------------------------------
def save_memory_bank(path: Path, bank: torch.Tensor,
                     dist_min: float, dist_max: float, threshold: float) -> None:
    """Save as an ordered list: [bank, dist_min, dist_max, threshold].

    The list format is what the runtime loader expects (`torch::load` on a
    vector of tensors). A dict would NOT load.
    """
    torch.save([
        bank.float(),
        torch.tensor(dist_min),
        torch.tensor(dist_max),
        torch.tensor(threshold),
    ], str(path))
    mb = path.stat().st_size / 1e6
    print(f"[OK] Memory bank saved -> {path}  ({bank.shape[0]} vectors, {mb:.1f} MB)")


# ---------------------------------------------------------------------------
#  Optional evaluation
# ---------------------------------------------------------------------------
def _score_image(backbone, bank_n: torch.Tensor, path: Path,
                 device: torch.device, size: int, radius: int) -> float:
    t       = load_image(path, size)
    patches = extract_patches(backbone, t, device)
    N, _    = patches.shape
    gh      = int(N ** 0.5)
    gw      = N // gh
    patches = aggregate_neighbourhood(patches, gh, gw, radius)
    pn      = F.normalize(patches, dim=1)
    d       = (1.0 - pn @ bank_n.to(pn.device).T).min(dim=1).values
    return float(d.max().item())


def evaluate(good_dir: Path, defect_dirs: list, bank: torch.Tensor, backbone,
             device: torch.device, input_size: int, patch_radius: int,
             threshold: float) -> None:
    try:
        from sklearn.metrics import roc_auc_score
    except ImportError:
        print("[WARN] scikit-learn not installed, skipping AUROC evaluation.")
        return

    bank_n = F.normalize(bank, dim=1)
    scores, labels = [], []

    for p in tqdm(list_images(good_dir), desc="eval good"):
        scores.append(_score_image(backbone, bank_n, p, device, input_size, patch_radius))
        labels.append(0)

    for ddir in defect_dirs:
        if not ddir.exists():
            print(f"[WARN] defect folder not found, skipped: {ddir}")
            continue
        for p in tqdm(list_images(ddir), desc=f"eval {ddir.name}"):
            scores.append(_score_image(backbone, bank_n, p, device, input_size, patch_radius))
            labels.append(1)

    if not any(l == 1 for l in labels):
        print("[WARN] no defect images found; AUROC not computed.")
        return

    auroc = roc_auc_score(labels, scores)
    tp = sum(1 for s, l in zip(scores, labels) if l == 1 and s >= threshold)
    fp = sum(1 for s, l in zip(scores, labels) if l == 0 and s >= threshold)
    fn = sum(1 for s, l in zip(scores, labels) if l == 1 and s < threshold)
    tn = sum(1 for s, l in zip(scores, labels) if l == 0 and s < threshold)
    print("\n-- Evaluation ------------------------------------")
    print(f"  Image AUROC : {auroc*100:.2f}%")
    print(f"  Threshold   : {threshold:.4f}")
    print(f"  TP={tp}  FP={fp}  FN={fn}  TN={tn}")
    print(f"  Precision   : {tp/(tp+fp+1e-9):.3f}")
    print(f"  Recall      : {tp/(tp+fn+1e-9):.3f}")
    print("--------------------------------------------------\n")


# ---------------------------------------------------------------------------
#  Entry point
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="Build a PatchCore memory bank from defect-free (good) images")
    parser.add_argument("--good", required=True,
                        help="Folder of normal (defect-free) training images")
    parser.add_argument("--backbone", default="dinov2_vits14.pt",
                        help="TorchScript backbone .pt (default: ./dinov2_vits14.pt)")
    parser.add_argument("--out", default="memory_bank.pt",
                        help="Output memory bank .pt (default: ./memory_bank.pt)")
    parser.add_argument("--size", type=int, default=224,
                        help="Input image size, must match the backbone export (default: 224)")
    parser.add_argument("--radius", type=int, default=1,
                        help="Patch neighbourhood radius for aggregation (default: 1)")
    parser.add_argument("--coreset", type=float, default=0.10,
                        help="Coreset subsampling ratio (default: 0.10)")
    parser.add_argument("--cpu", action="store_true",
                        help="Force CPU even if CUDA is available")
    parser.add_argument("--eval", action="store_true",
                        help="Report AUROC / precision / recall after building the bank "
                             "(needs --defects)")
    parser.add_argument("--defects", nargs="+", default=[],
                        help="Defect image folder(s) used by --eval")
    args = parser.parse_args()

    device    = torch.device("cpu" if args.cpu or not torch.cuda.is_available() else "cuda")
    backbone_pt = Path(args.backbone)
    out_pt      = Path(args.out)
    good_dir    = Path(args.good)

    if not backbone_pt.exists():
        print(f"[ERROR] Backbone not found: {backbone_pt}")
        print("        Run export_backbone.py first.")
        return
    if not good_dir.exists():
        print(f"[ERROR] Good images folder not found: {good_dir}")
        return

    print(f"[*] Loading backbone: {backbone_pt}")
    backbone = torch.jit.load(str(backbone_pt), map_location=device).eval()

    t0 = time.time()
    bank, d_min, d_max, threshold = build_memory_bank(
        good_dir, backbone, device, args.size, args.radius, args.coreset)
    print(f"[*] Memory bank built in {time.time()-t0:.1f}s")

    save_memory_bank(out_pt, bank, d_min, d_max, threshold)

    if args.eval:
        evaluate(good_dir, [Path(d) for d in args.defects], bank, backbone,
                 device, args.size, args.radius, threshold)


if __name__ == "__main__":
    main()
