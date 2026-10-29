# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
"""Local-only native binder parity. No downloads; no model files belong in git."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from datetime import datetime, timezone


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def equal(left, right):
    with left.open("rb") as a, right.open("rb") as b:
        while True:
            x, y = a.read(1024 * 1024), b.read(1024 * 1024)
            if x != y:
                return False
            if not x:
                return True


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("binder", type=Path)
    ap.add_argument("--exports", type=Path, default=Path(os.environ.get("CK_BINDER_EXPORTS", r"S:\CorridorKey2-Release\onnx_test")))
    ap.add_argument("--research", type=Path, default=Path(r"S:\Projects\CorridorKey2-for-OBS"))
    ap.add_argument("--output-dir", type=Path, required=True)
    ap.add_argument("--source", action="append", default=[], metavar="ID=PATH")
    ap.add_argument("--keep", action="store_true", help="Keep bound outputs for an optional inference check")
    ap.add_argument("--graph", default="*", help="Manifest filename glob, without .json")
    args = ap.parse_args()
    manifests = sorted(args.exports.glob(args.graph + ".json"))
    if not manifests:
        print("SKIP: local exporter manifests and reference weights are absent")
        return 77
    root = args.research
    paths = {
        "ck2": root / "CorridorKey2.0-master/weights/corridorkey_v2.safetensors",
        "stats": root / "CorridorKey2.0-master/weights/foundation_stats.safetensors",
        "rvm": root / "CorridorKey2.0-master/vfms/rvm/pretrain_rvm_small16_256_204031069.npz",
        "moge": Path(r"S:\CorridorKey2-Release\moge2_vitb_encoder\model.safetensors"),
        "refiner": Path(r"E:\CorridorKey2-Cache\hf_release\CorridorKey-Realtime-EdgeRefiner\corridorkey_edge_refiner_v15.safetensors"),
    }
    for item in args.source:
        key, value = item.split("=", 1)
        paths[key] = Path(value)
    required = {key for p in manifests for key in json.loads(p.read_text())["sources"]}
    missing = [key for key in required if key not in paths or not paths[key].is_file()]
    if missing:
        print("SKIP: local checkpoints absent: " + ", ".join(sorted(missing)))
        return 77
    output_dir = args.output_dir.resolve()
    if output_dir == args.exports.resolve():
        ap.error("Output directory must differ from the read-only export directory")
    output_dir.mkdir(parents=True, exist_ok=True)
    results = []
    for path in manifests:
        metadata = path.read_bytes()
        manifest = json.loads(metadata)
        output = output_dir / (path.stem + ".weights")
        reference = path.with_suffix(".weights")
        extent = max((w["offset"] + w["length"] for w in manifest["weights"]), default=0)
        if not reference.is_file() or reference.stat().st_size != extent:
            print(f"FAIL {path.stem}: reference missing or incomplete; wait for the exporter to finish", flush=True)
            return 1
        before = reference.stat()
        reference_hash = digest(reference)
        command = [str(args.binder.resolve()), str(path.resolve()), str(output), reference_hash]
        for key in sorted(manifest["sources"]):
            command.extend([key, str(paths[key].resolve())])
        start = time.monotonic()
        bound = subprocess.run(command, capture_output=True, text=True, timeout=600)
        if bound.returncode:
            print(f"FAIL {path.stem}: {bound.stderr}", flush=True)
            return 1
        result = json.loads(bound.stdout)
        result.update(graph=path.stem, byte_equal=equal(output, reference), seconds=round(time.monotonic() - start, 2),
                      manifest_sha256=hashlib.sha256(metadata).hexdigest(), verified_at=datetime.now(timezone.utc).isoformat())
        after = reference.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns) or path.read_bytes() != metadata:
            print(f"FAIL {path.stem}: exporter artifacts changed during validation", flush=True)
            return 1
        results.append(result)
        print(json.dumps(result), flush=True)
        (output_dir / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        if not result["byte_equal"]:
            return 1
        if not args.keep:
            output.unlink()
            Path(str(output) + ".ready.json").unlink()
    return 0


if __name__ == "__main__":
    sys.exit(main())
