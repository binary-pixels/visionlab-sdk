#!/usr/bin/env python3
"""export_backbone.py — Export a DINOv2 (or timm ViT) backbone to TorchScript
for use by the PatchCore anomaly detector.

The exported module takes a float32 NCHW tensor (ImageNet-normalised) and
returns a tuple:

    patch_tokens : [B, num_patches, embed_dim]   — spatial patch embeddings
    cls_token    : [B, embed_dim]                — global image embedding

This is step 1 of 3. Run it once per backbone; the .pt it produces is reused for
every product you train.

By default both a FP32 and a FP16 TorchScript are written:
    <model>.pt        — float32  (universal, works on CPU and GPU)
    <model>_fp16.pt   — float16  (GPU only, ~1.5-2× faster with Tensor Cores)

Usage:
    python export_backbone.py                          # DINOv2-S/14, both precisions
    python export_backbone.py --model dinov2_vits14
    python export_backbone.py --no-fp16                # FP32 only
    python export_backbone.py --fp16-only              # FP16 only (needs CUDA)
    python export_backbone.py --out my_backbone.pt --size 224
    python export_backbone.py --cpu                    # force CPU (FP16 skipped)
    python export_backbone.py --onnx                   # also write an .onnx

Dependencies:
    pip install torch torchvision
    pip install timm          # only for the vit_* models
"""

import argparse
import sys
from pathlib import Path

import torch
import torch.nn as nn


# ---------------------------------------------------------------------------
#  Wrappers: return (patch_tokens, cls_token)
# ---------------------------------------------------------------------------
class DinoV2Wrapper(nn.Module):
    """Wraps a DINOv2 model to return intermediate patch + CLS tokens.

    DINOv2 forward_features() returns a dict with:
        'x_norm_patchtokens' : [B, N_patches, D]
        'x_norm_clstoken'    : [B, D]
    We expose only these two tensors so the TorchScript export is simple.
    """

    def __init__(self, backbone: nn.Module):
        super().__init__()
        self.backbone = backbone

    def forward(self, x: torch.Tensor):
        out = self.backbone.forward_features(x)
        patch = out["x_norm_patchtokens"]   # [B, N, D]
        cls   = out["x_norm_clstoken"]      # [B, D]
        return patch, cls


class ViTWrapper(nn.Module):
    """Wraps a timm ViT model to return intermediate patch + CLS tokens.

    timm ViT forward_features() returns [B, N+1, D] where index 0 is CLS.
    """

    def __init__(self, backbone: nn.Module):
        super().__init__()
        self.backbone = backbone

    def forward(self, x: torch.Tensor):
        tokens = self.backbone.forward_features(x)  # [B, 1+N, D]
        cls   = tokens[:, 0, :]     # [B, D]
        patch = tokens[:, 1:, :]    # [B, N, D]
        return patch, cls


# ---------------------------------------------------------------------------
#  Model loaders
# ---------------------------------------------------------------------------
SUPPORTED_MODELS = {
    "dinov2_vits14": "dinov2",
    "dinov2_vitb14": "dinov2",
    "dinov2_vitl14": "dinov2",
    "vit_s_16":      "timm",
    "vit_b_16":      "timm",
    "vit_s_8":       "timm",
}

TIMM_NAMES = {
    "vit_s_16": "vit_small_patch16_224",
    "vit_b_16": "vit_base_patch16_224",
    "vit_s_8":  "vit_small_patch8_224",
}


def load_model(model_name: str, device: torch.device) -> nn.Module:
    family = SUPPORTED_MODELS.get(model_name)
    if family is None:
        print(f"[ERROR] Unknown model '{model_name}'. Supported: {list(SUPPORTED_MODELS)}")
        sys.exit(1)

    if family == "dinov2":
        print(f"[*] Loading {model_name} from torch.hub (facebookresearch/dinov2)...")
        backbone = torch.hub.load("facebookresearch/dinov2", model_name, pretrained=True)
        backbone = backbone.eval().to(device)
        return DinoV2Wrapper(backbone).eval().to(device)

    else:  # timm
        try:
            import timm
        except ImportError:
            print("[ERROR] timm not installed. Run: pip install timm")
            sys.exit(1)
        timm_name = TIMM_NAMES[model_name]
        print(f"[*] Loading {timm_name} from timm...")
        backbone = timm.create_model(timm_name, pretrained=True)
        backbone = backbone.eval().to(device)
        return ViTWrapper(backbone).eval().to(device)


# ---------------------------------------------------------------------------
#  Export helpers
# ---------------------------------------------------------------------------
def export_torchscript(wrapper: nn.Module, size: int, out_path: Path,
                       device: torch.device, dtype: torch.dtype = torch.float32) -> None:
    """Trace and save the wrapper to TorchScript.

    Args:
        wrapper:  Already on *device* and in the target *dtype*.
        size:     Square input resolution (pixels).
        out_path: Destination .pt file.
        device:   Torch device used for the dummy input.
        dtype:    torch.float32 or torch.float16.
    """
    label = "FP16" if dtype == torch.float16 else "FP32"
    dummy = torch.zeros(1, 3, size, size, device=device, dtype=dtype)
    print(f"[*] Tracing {label} with input shape {list(dummy.shape)} ...")
    with torch.no_grad():
        traced = torch.jit.trace(wrapper, dummy)
        patch, cls = traced(dummy)
    print(f"    patch_tokens : {list(patch.shape)}  dtype={patch.dtype}")
    print(f"    cls_token    : {list(cls.shape)}  dtype={cls.dtype}")
    traced.save(str(out_path))
    sz_mb = out_path.stat().st_size / 1024 / 1024
    print(f"[OK] {label} TorchScript saved -> {out_path}  ({sz_mb:.1f} MB)")


def export_onnx(wrapper: nn.Module, size: int, out_path: Path,
                device: torch.device) -> None:
    dummy = torch.zeros(1, 3, size, size, device=device)
    print(f"[*] Exporting ONNX -> {out_path} ...")
    torch.onnx.export(
        wrapper, dummy, str(out_path),
        input_names=["image"],
        output_names=["patch_tokens", "cls_token"],
        dynamic_axes={"image": {0: "batch"},
                      "patch_tokens": {0: "batch"},
                      "cls_token":    {0: "batch"}},
        opset_version=17,
    )
    print(f"[OK] ONNX saved -> {out_path}")


def fp16_out_path(fp32_path: Path) -> Path:
    """Derive FP16 output path: <stem>_fp16.pt"""
    return fp32_path.with_name(fp32_path.stem + "_fp16" + fp32_path.suffix)


def _get_patch_size(model_name: str) -> int:
    if "14" in model_name: return 14
    if "_8" in model_name: return 8
    return 16


def _get_embed_dim(wrapper: nn.Module, size: int, device: torch.device) -> int:
    with torch.no_grad():
        dummy = torch.zeros(1, 3, size, size, device=device)
        _, cls = wrapper(dummy)
    return cls.shape[-1]


# ---------------------------------------------------------------------------
#  Entry point
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="Export a transformer backbone (DINOv2/ViT) to TorchScript "
                    "(FP32 and/or FP16)")
    parser.add_argument("--model", default="dinov2_vits14",
                        choices=list(SUPPORTED_MODELS),
                        help="Backbone variant (default: dinov2_vits14)")
    parser.add_argument("--size", type=int, default=224,
                        help="Input image size in pixels (default: 224)")
    parser.add_argument("--out", default=None,
                        help="Output .pt path for FP32 (default: ./<model>.pt). "
                             "FP16 is written to <stem>_fp16.pt alongside it.")
    parser.add_argument("--onnx", action="store_true",
                        help="Also export an ONNX file alongside the FP32 TorchScript")
    parser.add_argument("--cpu", action="store_true",
                        help="Force CPU export (FP16 export is skipped on CPU)")
    prec = parser.add_mutually_exclusive_group()
    prec.add_argument("--no-fp16", action="store_true",
                      help="Skip FP16 export (write FP32 only)")
    prec.add_argument("--fp16-only", action="store_true",
                      help="Skip FP32 export (write FP16 only, requires GPU)")
    args = parser.parse_args()

    device = torch.device("cpu" if args.cpu or not torch.cuda.is_available() else "cuda")
    print(f"[*] Device: {device}")

    out_fp32 = Path(args.out) if args.out else Path(f"{args.model}.pt")
    out_fp16 = fp16_out_path(out_fp32)

    want_fp32 = not args.fp16_only
    want_fp16 = not args.no_fp16 and device.type == "cuda"

    if args.fp16_only and device.type != "cuda":
        print("[ERROR] --fp16-only requires a CUDA GPU. Use --cpu to export FP32 on CPU.")
        sys.exit(1)

    # Load once in FP32
    wrapper_fp32 = load_model(args.model, device)

    # -- FP32 export --------------------------------------------------------
    if want_fp32:
        export_torchscript(wrapper_fp32, args.size, out_fp32, device, torch.float32)

    # -- FP16 export --------------------------------------------------------
    if want_fp16:
        print("[*] Converting model to FP16 for half-precision export...")
        import copy
        wrapper_fp16 = copy.deepcopy(wrapper_fp32).half().eval()
        export_torchscript(wrapper_fp16, args.size, out_fp16, device, torch.float16)
        del wrapper_fp16
    elif not want_fp16 and not args.no_fp16 and device.type != "cuda":
        print("[!] FP16 export skipped — no CUDA GPU available.")

    if args.onnx and want_fp32:
        out_onnx = out_fp32.with_suffix(".onnx")
        export_onnx(wrapper_fp32, args.size, out_onnx, device)

    # -- Summary ------------------------------------------------------------
    embed_dim   = _get_embed_dim(wrapper_fp32, args.size, device)
    num_patches = (args.size // _get_patch_size(args.model)) ** 2
    print()
    print("-- Exported files ----------------------------------------------")
    if want_fp32:
        print(f"  FP32  ->  {out_fp32}")
    if want_fp16:
        print(f"  FP16  ->  {out_fp16}")
    print(f"  embed_dim   = {embed_dim}")
    print(f"  num_patches = {num_patches}  (for {args.size}x{args.size} input)")
    print()
    print("Next: python build_memory_bank.py --good <good_images_dir> "
          f"--backbone {out_fp32}")
    print("----------------------------------------------------------------")


if __name__ == "__main__":
    main()
