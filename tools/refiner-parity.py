# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the native GPU atlas and blended output against an external research checkout."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

os.environ["OPENCV_IO_ENABLE_OPENEXR"] = "1"
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
sys.dont_write_bytecode = True
import numpy as np
import torch
import torch.nn.functional as F


def engine(root, budget):
    sys.path[:0] = [str(root), str(root / "CorridorKey2.0-master")]
    from corridorkey_obs.engine import CorridorKeyRealTimeEngine, EngineConfig
    from corridorkey_obs.refiner import load_refiner
    from corridorkey import color_utils
    e = CorridorKeyRealTimeEngine.__new__(CorridorKeyRealTimeEngine)
    e.cfg = EngineConfig(guide_mode="refiner", refiner_max_tiles=budget,
                         compile_post=False, compile_refiner=False, triton_post=True)
    e.device = torch.device("cuda")
    e.cu = color_utils
    e.refiner = load_refiner(root / "checkpoints/refiner_v12_w24.pt")
    e._fullres_fn = e._fullres_triton = e._post_params = e._tile_fns = e._refiner_compiled = None
    return e


def tensor(path):
    return torch.from_numpy(np.load(path)).permute(2, 0, 1)[None].cuda()


def hwc(t):
    return t.detach().float().cpu()[0].permute(1, 2, 0).numpy()


def items(args):
    return json.loads(args.manifest.read_text())


def render(args):
    rows = items(args)
    for row in rows:
        prefix = Path(row["native"]).parent / args.label
        row.update(output=str(prefix), refiner_budget=args.budget)
        row["interop"] = args.interop
        if args.dml_library:
            row["dml_library"] = str(args.dml_library.resolve())
        if args.blend:
            row["refiner_centres"] = str(prefix) + ".centres.f32"
    manifest = args.output / f"{args.label}-render.json"
    manifest.write_text(json.dumps(rows, indent=2))
    env = dict(os.environ)
    env["PATH"] = str(args.obs / "bin/64bit") + os.pathsep + env["PATH"]
    env["OBS_TEST_GRAPHICS"] = str(args.obs / "bin/64bit/libobs-d3d11.dll")
    env["OBS_TEST_DATA"] = (args.obs / "data/libobs").as_posix() + "/"
    with (args.output / f"{args.label}-render.log").open("w") as log:
        subprocess.run([str(args.probe), "unused", str(args.data), "1920", "offline", str(manifest)],
                       env=env, stdout=log, stderr=log, check=True)


@torch.inference_mode()
def oracle(args):
    e = engine(args.research, args.budget)
    records, inference = [], []
    for row in items(args):
        folder = Path(row["native"]).parent
        prefix = folder / args.label
        rgb = tensor(row["native"])[:, :3]
        guide = tensor(row["guide"])[:, :3]
        model = tensor(row["model"])
        alpha, fg = model[:, 3:], model[:, :3]
        guide = F.interpolate(guide, size=alpha.shape[-2:], mode="bilinear", align_corners=False)
        box = lambda x: F.avg_pool2d(x, 5, stride=1, padding=2, count_include_pad=False)
        coef = e._rgb3_guided_coef(guide, alpha, box, .0005)
        band = e._refiner_band(alpha)
        bg = e._background_low(guide, alpha)
        maps = torch.cat([coef, fg - guide, bg, alpha, band], 1)
        h, w = rgb.shape[-2:]
        th, tw = (h + 31) // 32, (w + 31) // 32
        selected = np.fromfile(str(prefix) + ".selection.f32", np.float32).reshape(-1, 4)
        n = min(args.budget, th * tw)
        indices = selected[:n, 1].astype(np.int64)
        assert len(np.unique(indices)) == n and np.all((indices >= 0) & (indices < th * tw))
        if n < args.budget:
            indices = np.r_[indices, np.repeat(indices[:1], args.budget - n)]
        idx = torch.from_numpy(indices).cuda()
        ar = torch.arange(52, device="cuda") - 10
        rows = (idx[:, None] // tw * 32 + ar).clamp(0, h - 1)
        cols = (idx[:, None] % tw * 32 + ar).clamp(0, w - 1)
        native = rgb[0][:, rows[:, :, None], cols[:, None, :]].permute(1, 0, 2, 3).float()
        gy = ((rows.float() + .5) / h * 2 - 1)[:, :, None].expand(-1, -1, 52)
        gx = ((cols.float() + .5) / w * 2 - 1)[:, None, :].expand(-1, 52, -1)
        lp = F.grid_sample(maps.expand(args.budget, -1, -1, -1), torch.stack([gx, gy], -1),
                           padding_mode="border", align_corners=False)
        params = torch.tensor([1., 1.05, 0., .65], device="cuda")
        expected, _, _ = e._tile_pre(native, lp, params)
        actual = np.fromfile(str(prefix) + ".x.f32", np.float32).reshape(args.budget, 11, 52, 52)
        diff = np.abs(actual - expected.float().cpu().numpy())
        np.save(str(prefix) + ".expected-x.npy", expected.float().cpu().numpy())
        occ = F.adaptive_max_pool2d(F.max_pool2d(band, 3, 1, 1), (th, tw))
        scores = (F.adaptive_avg_pool2d(band, (th, tw)) + .001 * occ).flatten().cpu().numpy()
        gpu_scores = np.fromfile(str(prefix) + ".scores.f32", np.float32).reshape(-1, 4)[:th*tw, 0]
        expected_selection = np.argsort(-scores, kind="stable")[:n]
        record = {"segment": row.get("segment", "fixture"), "index": row.get("index", 0),
                  "input_channel_max": diff.max(axis=(0, 2, 3)).tolist(),
                  "input_channel_mae": diff.mean(axis=(0, 2, 3)).tolist(),
                  "score_max": float(np.abs(scores - gpu_scores).max()),
                  "band_max": float(np.abs(np.fromfile(str(prefix) + ".band.f32", np.float32).reshape(*alpha.shape[-2:], 4)[..., :1] - hwc(band)).max()),
                  "background_max": float(np.abs(np.fromfile(str(prefix) + ".background.f32", np.float32).reshape(*alpha.shape[-2:], 4)[..., :3] - hwc(bg)).max()),
                  "selection_set_difference": int(len(set(indices[:n]) ^ set(expected_selection)))}
        np.save(str(prefix) + ".x.npy", actual)
        inference.append({"input": str(prefix) + ".x.npy", "output": str(prefix) + ".centres.f32", "runs": args.runs})
        for name, source in (("oracle", rgb), ("research", rgb.to(torch.bfloat16))):
            if args.inputs_only:
                continue
            refined_fg, refined_a = e._post_v3(source, guide, alpha, fg, args.budget)
            np.save(folder / f"{args.label}-{name}.npy", np.concatenate([hwc(refined_fg), hwc(refined_a)], -1))
        records.append(record)
        (args.output / f"{args.label}-inputs.json").write_text(json.dumps(records, indent=2))
        (args.output / f"{args.label}-inference.json").write_text(json.dumps(inference, indent=2))
        print(record, flush=True)


def infer(args):
    subprocess.run([str(args.validator), str(args.config), "refiner", str(args.budget),
                    str(args.output / f"{args.label}-inference.json"),
                    str(args.output / f"{args.label}-inference-report.json"), args.backend], check=True)


def measure(args):
    spec = importlib.util.spec_from_file_location("quality", Path(__file__).with_name("quality-parity.py"))
    q = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(q)
    import cv2
    records = []
    for row in items(args):
        folder = Path(row["native"]).parent
        reference = args.reference_label or args.label
        oracle = np.load(folder / f"{reference}-oracle.npy")
        research = np.load(folder / f"{reference}-research.npy")
        prefix = folder / args.label
        linear = np.fromfile(str(prefix) + ".rgba.f32", np.float32).reshape(oracle.shape)
        raw = np.fromfile(str(prefix) + ".raw.f32", np.float32).reshape(oracle.shape)
        fg = raw[..., :3].copy()
        spill = np.maximum(fg[..., 1] - (fg[..., 0] + fg[..., 2]) * .5, 0) * .65
        fg = np.clip(fg + spill[..., None] * np.array([.5, -1, .5], np.float32), 0, 1)
        actual = np.concatenate([fg, linear[..., 3:]], -1)
        a = cv2.imread(row["teacher_alpha"], cv2.IMREAD_UNCHANGED).clip(0, 1)
        if a.ndim == 3:
            a = a[..., 0]
        tf = q.encode(cv2.imread(row["teacher_fg"], cv2.IMREAD_UNCHANGED)[..., ::-1].clip(0)).clip(0, 1)
        spill = np.maximum(tf[..., 1] - (tf[..., 0] + tf[..., 2]) * .5, 0) * .65
        tf = np.clip(tf + spill[..., None] * np.array([.5, -1, .5], np.float32), 0, 1)
        teacher = q.rgba(tf, a[..., None])
        radius = max(2, round(6 * a.shape[0] / 2160))
        band = cv2.dilate(((a > .02) & (a < .98)).astype(np.uint8), np.ones((radius * 2 + 1,) * 2, np.uint8)).astype(bool)
        interior = (a >= .98) & ~band
        record = {"segment": row.get("segment"), "index": row.get("index")}
        for name, x, y in (("plugin_oracle", actual, oracle), ("plugin_research", actual, research),
                           ("plugin_teacher", actual, teacher), ("research_teacher", research, teacher),
                           ("rgb3_teacher", np.load(folder / "research.npy"), teacher)):
            record[name] = q.metrics(x, y, band, interior)
        if row.get("index") == 0:
            composite = q.encode(linear[..., :3] + .04 * (1 - linear[..., 3:])).clip(0, 1)
            cv2.imwrite(str(prefix) + ".png", np.uint8(composite[..., ::-1] * 255 + .5))
        records.append(record)
        (args.output / f"{args.label}-metrics.json").write_text(json.dumps(records, indent=2))
        print(row.get("segment"), row.get("index"), {k: v["alpha_band_mae"] for k, v in record.items() if isinstance(v, dict)}, flush=True)


def check(args):
    inputs = json.loads((args.output / f"{args.label}-inputs.json").read_text())
    post = json.loads((args.output / f"{args.label}-metrics.json").read_text())
    full = json.loads((args.output / f"{args.full_label}-metrics.json").read_text())
    identities = lambda rows: {(x["segment"], x["index"]) for x in rows}
    if not inputs or identities(inputs) != identities(post) or identities(post) != identities(full):
        raise RuntimeError("Missing or mismatched parity frames")
    for rows in (post, full):
        for row in rows:
            for value in row.values():
                if isinstance(value, dict) and not all(np.isfinite(x) for x in value.values()):
                    raise RuntimeError("Nonfinite parity metric")
    channel_max = np.max([x["input_channel_max"] for x in inputs], axis=0)
    channel_mae = np.max([x["input_channel_mae"] for x in inputs], axis=0)
    post_max = max(v for x in post for k, v in x["plugin_research"].items() if k.endswith("_mae"))
    teacher_gap = max(abs(x["plugin_teacher"][k] - v) for x in full
                      for k, v in x["research_teacher"].items() if k.endswith("_mae"))
    report = dict(frames=len(post), input_channel_max=channel_max.tolist(),
                  input_channel_worst_frame_mae=channel_mae.tolist(),
                  score_max=max(x["score_max"] for x in inputs),
                  selection_set_difference=max(x["selection_set_difference"] for x in inputs),
                  reconstruction_max_region_mae=post_max, full_pipeline_max_teacher_error_gap=teacher_gap,
                  input_max_tolerance=.0005, input_mae_tolerance=.000005,
                  reconstruction_tolerance=.002, teacher_gap_tolerance=.005)
    report["passed"] = bool(np.isfinite(channel_max).all() and np.isfinite(channel_mae).all()
                            and channel_max.max() <= .0005 and channel_mae.max() <= .000005
                            and report["score_max"] <= .000001 and not report["selection_set_difference"]
                            and np.isfinite(post_max) and post_max <= .002
                            and np.isfinite(teacher_gap) and teacher_gap <= .005)
    (args.output / "refiner-check.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    if not report["passed"]:
        raise RuntimeError("Refiner parity tolerance exceeded")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=["render", "oracle", "infer", "measure", "check"])
    p.add_argument("--research", type=Path, required=True)
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--budget", type=int, default=384)
    p.add_argument("--label", default="refiner")
    p.add_argument("--reference-label", help="Use another run's unchanged research output for a full-pipeline comparison")
    p.add_argument("--full-label", default="dml-refiner")
    p.add_argument("--inputs-only", action="store_true", help="Check inputs without rerunning the research post pass")
    p.add_argument("--probe", type=Path)
    p.add_argument("--data", type=Path)
    p.add_argument("--obs", type=Path, default=Path("C:/Program Files/obs-studio"))
    p.add_argument("--blend", action="store_true")
    p.add_argument("--interop", action="store_true", help="Use CUDA texture transfers and atlas conversion in the native probe")
    p.add_argument("--dml-library", type=Path, help="Use D3D11/D3D12 atlas transfers with this DirectML library")
    p.add_argument("--validator", type=Path)
    p.add_argument("--config", type=Path)
    p.add_argument("--backend", default="tensorrt")
    p.add_argument("--runs", type=int, default=1)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    globals()[args.action](args)
