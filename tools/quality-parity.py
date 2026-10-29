# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
"""External research oracle and libobs reconstruction comparison. No model code is vendored."""
import argparse
import json
import os
from pathlib import Path
import sys

os.environ["OPENCV_IO_ENABLE_OPENEXR"] = "1"
os.environ["CK_SWIN_MASK"] = "0"
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
sys.dont_write_bytecode = True
import cv2
import numpy as np
import torch
import torch.nn.functional as F


def hwc(t):
    return t.detach().float().cpu()[0].permute(1, 2, 0).numpy()


def rgba(rgb, alpha=None):
    if alpha is None:
        alpha = np.ones((*rgb.shape[:2], 1), np.float32)
    return np.ascontiguousarray(np.concatenate([rgb, alpha], axis=2), dtype=np.float32)


def encode(x):
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.maximum(x, 0) ** (1 / 2.4) - 0.055)


def decode(x):
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def prepare(args):
    sys.path.insert(0, str(args.research))
    from corridorkey_obs.engine import CorridorKeyRealTimeEngine, EngineConfig, QualityPreset
    cfg = EngineConfig(preset=QualityPreset.FAST_4K, stage2_interval=0, guide_mode="rgb3",
                       refiner_path="", compile_post=False, triton_post=True)
    engine = CorridorKeyRealTimeEngine(cfg)
    manifest = []
    segments = [p for split in ("eval", "ck1") for p in sorted((args.research / "eval_data" / split).iterdir()) if p.is_dir()]
    if args.segments:
        segments = [p for p in segments if p.name in args.segments.split(",")]
    with torch.inference_mode():
        for seg in segments:
            engine.reset_temporal_state()
            alpha_dir = next((seg / "teacher").glob("*_alpha_exr"))
            fg_dir = next((seg / "teacher").glob("*_fg_exr"))
            alphas, fgs = sorted(alpha_dir.glob("*.exr")), sorted(fg_dir.glob("*.exr"))
            for index, image in enumerate(sorted((seg / "rgb").glob("*.png"))[:args.frames]):
                folder = args.output / f"{seg.name}-{index:03d}"
                folder.mkdir(parents=True, exist_ok=True)
                rgb = cv2.imread(str(image))[:, :, ::-1].copy().astype(np.float32) / 255
                tensor = torch.from_numpy(rgb).permute(2, 0, 1)[None].cuda()
                fg, alpha = engine.process_frame_gpu(tensor)
                low = engine.last_lowres
                fg_low = engine.cu.linear_to_srgb(engine.cu.asinh_to_linear(low["fg_asinh_low"].float()).clamp(min=0)).clamp(0, 1)
                np.save(folder / "native.npy", rgba(rgb))
                np.save(folder / "guide.npy", rgba(hwc(low["rgb_low"])))
                np.save(folder / "model.npy", rgba(hwc(fg_low), hwc(low["alpha_low"])))
                np.save(folder / "research.npy", rgba(hwc(fg), hwc(alpha)))
                # Separate reconstruction arithmetic from native BF16 and final BF16 rounding.
                grid = F.interpolate(low["rgb_low"].float(), size=low["alpha_low"].shape[-2:], mode="bilinear", align_corners=False)
                coef = engine._rgb3_guided_coef(grid, low["alpha_low"].float(),
                    lambda x: F.avg_pool2d(x, 5, stride=1, padding=2, count_include_pad=False), cfg.guided_eps)
                from corridorkey_obs.fused_post import fullres_guided_triton
                params = torch.tensor([cfg.unmix_inner, cfg.alpha_contrast, cfg.alpha_offset, cfg.despill_strength], device="cuda")
                same_fg, same_a = fullres_guided_triton(tensor, coef, fg_low - grid, params)
                np.save(folder / "oracle.npy", rgba(hwc(same_fg), hwc(same_a)))
                np.save(folder / "coef.npy", hwc(coef))
                np.save(folder / "delta.npy", rgba(hwc(fg_low - grid)))
                item = {k: str(folder / f"{k}.npy") for k in ("native", "guide", "model")}
                item.update(output=str(folder / "before"), source=str(image), teacher_alpha=str(alphas[index]),
                            teacher_fg=str(fgs[index]), segment=seg.name, index=index)
                manifest.append(item)
                (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2))
                print(f"Prepared {seg.name} {index} {rgb.shape}", flush=True)
                del tensor, fg, alpha, same_fg, same_a


def metrics(actual, expected, band, interior):
    result = {}
    for name, mask in (("band", band), ("interior", interior)):
        for field, x, y in (("alpha", actual[..., 3], expected[..., 3]),
                            ("fg", actual[..., :3], expected[..., :3]),
                            ("premul", actual[..., :3] * actual[..., 3:], expected[..., :3] * expected[..., 3:])):
            error = np.abs(x - y)[mask]
            result[f"{field}_{name}_mae"] = float(error.mean()) if error.size else 0
            result[f"{field}_{name}_max"] = float(error.max()) if error.size else 0
    return result


def measure(args):
    manifest = json.loads((args.output / "manifest.json").read_text())
    rows = []
    for item in manifest:
        folder = Path(item["native"]).parent
        research = np.load(folder / "research.npy")
        oracle = np.load(folder / "oracle.npy")
        a = cv2.imread(item["teacher_alpha"], cv2.IMREAD_UNCHANGED).clip(0, 1)
        if a.ndim == 3:
            a = a[..., 0]
        fg = encode(cv2.imread(item["teacher_fg"], cv2.IMREAD_UNCHANGED)[..., ::-1].clip(0)).clip(0, 1)
        spill = np.maximum(fg[..., 1] - (fg[..., 0] + fg[..., 2]) / 2, 0) * 0.65
        fg += spill[..., None] * np.array([0.5, -1, 0.5], np.float32)
        teacher = rgba(fg.clip(0, 1), a[..., None])
        radius = max(2, round(6 * a.shape[0] / 2160))
        band = cv2.dilate(((a > .02) & (a < .98)).astype(np.uint8), np.ones((radius * 2 + 1,) * 2, np.uint8)).astype(bool)
        interior = (a >= .98) & ~band
        row = {"segment": item["segment"], "index": item["index"], "research_teacher": metrics(research, teacher, band, interior)}
        for version in ("before", "after"):
            p = folder / f"{version}.rgba.f32"
            if not p.exists():
                continue
            linear = np.fromfile(p, np.float32).reshape(research.shape)
            raw = np.fromfile(folder / f"{version}.raw.f32", np.float32).reshape(research.shape)
            fg = raw[..., :3].copy()
            spill = np.maximum(fg[..., 1] - (fg[..., 0] + fg[..., 2]) / 2, 0) * .65
            fg = np.clip(fg + spill[..., None] * np.array([.5, -1, .5], np.float32), 0, 1)
            actual = rgba(fg, linear[..., 3:])
            row[f"{version}_teacher"] = metrics(actual, teacher, band, interior)
            row[f"{version}_research"] = metrics(actual, research, band, interior)
            row[f"{version}_oracle"] = metrics(actual, oracle, band, interior)
            row[f"{version}_transfer_max"] = float(np.abs(linear[..., :3] - decode(fg) * linear[..., 3:]).max())
            half = np.fromfile(folder / f"{version}.rgba.f16", np.float16).astype(np.float32).reshape(linear.shape)
            row[f"{version}_half_max"] = float(np.abs(half - linear).max())
            if item["index"] == 0:
                composite = encode(linear[..., :3] + .04 * (1 - linear[..., 3:])).clip(0, 1)
                cv2.imwrite(str(folder / f"{version}.png"), np.uint8(composite[..., ::-1] * 255 + .5))
                diff = np.abs(actual[..., :3] * actual[..., 3:] - research[..., :3] * research[..., 3:])
                cv2.imwrite(str(folder / f"{version}-diff.png"), np.uint8(np.clip(diff[..., ::-1] * 8, 0, 1) * 255))
        rows.append(row)
        print(item["segment"], item["index"], {k: round(v["alpha_band_mae"] * 1000, 4) for k, v in row.items() if isinstance(v, dict)}, flush=True)
    (args.output / "metrics.json").write_text(json.dumps(rows, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["prepare", "measure"])
    parser.add_argument("--research", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=3)
    parser.add_argument("--segments")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    globals()[args.action](args)
