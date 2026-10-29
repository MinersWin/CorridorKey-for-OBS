# SPDX-License-Identifier: GPL-3.0-or-later
"""Run a bounded native backend test against the generated W01 fixture."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def validate(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    graph = (args.fixtures / args.precision / "tiny.onnx").resolve()
    request, response = output / "request.json", output / "response.json"
    job = dict(schema=1, mode="probe", bundle=str(args.bundle.resolve()), graph=str(graph),
               precision={"fp32": 2, "fp16": 1, "bf16": 3}[args.precision],
               adapter=args.adapter, require_adapter_match=bool(args.adapter),
               iterations=5, profile=str(output / "profile"))
    if args.cache:
        weights = graph.parent / "tiny.weights"
        job["weights_hash"] = hashlib.sha256(weights.read_bytes() if weights.exists() else graph.read_bytes()).hexdigest()
        job["cache"] = str(args.cache.resolve())
    request.write_text(json.dumps(job, indent=2))
    response.unlink(missing_ok=True)
    # The native supervisor enforces the deadline and terminates its process tree.
    with (output / "supervisor.log").open("w") as log:
        process = subprocess.run([str(args.helper.resolve()), "--supervise", str(request), str(response), str(args.timeout_ms)],
                                 stdout=log, stderr=log, check=False)
    if response.exists():
        print(response.read_text())
    return process.returncode


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, default=Path(".cache/fixtures"))
    parser.add_argument("--precision", choices=["fp32", "fp16", "bf16"], default="fp32")
    parser.add_argument("--adapter", default="", help="OBS LUID/PCI identity; empty permits hardware discovery only")
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout-ms", type=int, default=25000)
    raise SystemExit(validate(parser.parse_args()))
