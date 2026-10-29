# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure a dedicated, already running portable OBS via tests/obs-w11.lua.

The caller owns gpu-window.lock for the entire run. No websocket or browser source.
"""
import argparse
import importlib.util
import json
import re
import statistics
import time
from datetime import datetime, timezone
from pathlib import Path


def main(args):
    spec = importlib.util.spec_from_file_location("interop", Path(__file__).with_name("measure-interop.py"))
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    log = max((args.obs / "config/obs-studio/logs").glob("*.txt"), key=lambda x: x.stat().st_mtime)
    read = lambda: log.read_text(encoding="utf-8", errors="replace")
    args.output.mkdir(parents=True, exist_ok=True)
    for profile in ("facecam", "balanced", "quality"):
        for transport in ("staged_grid", "auto"):
            for refiner in (False, True):
                name = f"{profile}-{transport}-{'on' if refiner else 'off'}"
                destination = args.output / (name + ".json")
                if destination.exists():
                    continue
                settings = dict(settings_version=2, preset="custom",
                    profile="facecam-60" if profile == "facecam" else profile,
                    grid="256x512" if profile == "facecam" else "512x512", backend="directml",
                    transport=transport, refiner_enabled=refiner, edge_refiner=refiner,
                    refiner_max_tiles=dict(facecam=384, balanced=512, quality=768)[profile],
                    roi_enabled=profile != "facecam", target_fps="60", stale_timeout_ms=1000,
                    stage2_interval="1" if profile == "quality" else "0")
                offset = len(read())
                command = args.commands / "command.json"
                temporary = command.with_suffix(".tmp")
                temporary.write_text(json.dumps(dict(case=name, image=str(args.image.resolve()), settings=settings)))
                temporary.replace(command)
                print("Preparing " + name, flush=True)
                deadline = time.monotonic() + 600
                while time.monotonic() < deadline:
                    text = read()[offset:]
                    rows = helper.metrics(text, "[corridorkey:w09]")
                    last_graph = "refiner_b" + str(settings["refiner_max_tiles"]) if refiner else "moge_" + settings["grid"]
                    if "W11 case=" + name in text and last_graph + " ready in " in text and rows and rows[-1].get("keyed_fps", 0) > 0:
                        break
                    time.sleep(2)
                else:
                    raise RuntimeError("No output for " + name)
                time.sleep(args.settle)
                transports = re.findall(r"\[corridorkey:w10\] transport=(\S+)", read())
                selected_transport = transports[-1] if transports else "staged"
                expected_transport = "d3d11-d3d12-dml" if transport == "auto" else "staged"
                if selected_transport != expected_transport:
                    raise RuntimeError(f"{name}: expected {expected_transport}, got {selected_transport}")
                start = len(read())
                samples = []
                print("Measuring " + name, flush=True)
                started = datetime.now(timezone.utc).isoformat()
                deadline = time.monotonic() + args.seconds
                while time.monotonic() < deadline:
                    samples.append(helper.gpu_sample())
                    time.sleep(1)
                text = read()[start:]
                rows = [row for row in helper.metrics(text, "[corridorkey:w08]") if "age_median_ms" in row]
                if not rows:
                    raise RuntimeError("No age samples for " + name)
                report = dict(case=name, started=started, duration_s=args.seconds,
                    settings=settings, transport=selected_transport, gpu_samples=samples, age_samples=rows,
                    stages=helper.metrics(text, "[corridorkey:w09]"),
                    refiner=helper.metrics(text, "[corridorkey:w13]"),
                    keyed_fps=statistics.median(x["keyed_fps"] for x in rows),
                    age_median_ms=statistics.median(x["age_median_ms"] for x in rows),
                    age_p95_ms=statistics.median(x["age_p95_ms"] for x in rows),
                    whole_gpu_mib=statistics.median(x["memory_mib"] for x in samples))
                if refiner and transport == "auto" and (not report["refiner"] or
                        any(row.get("readback_bytes", -1) != 0 for row in report["refiner"])):
                    raise RuntimeError("Device refiner used staged atlas: " + name)
                destination.write_text(json.dumps(report, indent=2))
                destination.with_suffix(".log").write_text(text)
                print({key: report[key] for key in ("case", "keyed_fps", "age_median_ms", "whole_gpu_mib")}, flush=True)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--obs", type=Path, required=True)
    p.add_argument("--commands", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--image", type=Path, required=True)
    p.add_argument("--seconds", type=int, default=45)
    p.add_argument("--settle", type=int, default=15)
    main(p.parse_args())
