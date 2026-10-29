# SPDX-License-Identifier: GPL-3.0-or-later
"""Make a local W10 dev config and hash-checked graph fixture without modifying exports."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--exports", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--helper", type=Path, required=True)
p.add_argument("--bundle", type=Path, required=True)
p.add_argument("--adapter", required=True)
p.add_argument("--fixture-fp32", type=Path, required=True)
p.add_argument("--fixture-fp16", type=Path, required=True)
p.add_argument("--staged", action="store_true")
args = p.parse_args()
root = Path(__file__).resolve().parent.parent
release = root / "tests/fixtures/ck2-release-r3.json"
hashes = json.loads(release.read_text())["sha256"]
target = args.output.resolve()
models = target / "models"
models.mkdir(parents=True, exist_ok=True)
for filename, expected in hashes.items():
    source = args.exports / filename
    if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
        raise ValueError(f"Export differs from release revision 3: {filename}")
    shutil.copy2(source, models / filename)
for name in sorted({Path(n).stem for n in hashes}):
    for suffix in (".weights", ".ref.npz"):
        source, dest = args.exports / (name + suffix), models / (name + suffix)
        if source.exists() and not dest.exists():
            # Read-only consumers only. The copied graphs never write external weights.
            try:
                dest.hardlink_to(source.resolve())
            except OSError:
                shutil.copy2(source, dest)
config = {
    "models": str(models), "cache": str(target / "engines"),
    "helper": str(args.helper.resolve()), "scratch": str(target / "scratch"),
    "fixture_fp32": str(args.fixture_fp32.resolve()), "fixture_fp16": str(args.fixture_fp16.resolve()),
    "bundles": {"tensorrt": str(args.bundle.resolve())}, "adapter": args.adapter,
    "capture": True, "interop": not args.staged, "build_timeout_ms": 300000,
    "release_fixture": str(release),
}
(target / "config.json").write_text(json.dumps(config, indent=2) + "\n")
print(target / "config.json")
