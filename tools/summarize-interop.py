# SPDX-License-Identifier: GPL-3.0-or-later
"""Reduce measure-interop.py results without discarding their measurement scope."""
import argparse
import json
import statistics as stats
from pathlib import Path


def summarize(path):
    data = json.loads(path.read_text())
    case = data["case"]
    age, stages, refiner = (data[key] for key in ("age", "stages", "refiner"))
    if not age or not stages or any(row["age_median_ms"] < 0 for row in age):
        raise ValueError(f"Incomplete keyed telemetry: {path}")
    if case["refiner"]:
        budget = dict(facecam=384, balanced=512, quality=768)[case["profile"]]
        if not refiner or any(row["tiles"] != budget for row in refiner):
            raise ValueError(f"Wrong tile budget: {path}")
        for row in refiner:
            transferred = row["readback_bytes"] + row["upload_bytes"]
            if (case["transport"] == "auto") != (transferred == 0):
                raise ValueError(f"Wrong atlas transport: {path}")
    elif refiner:
        raise ValueError(f"Refiner still active: {path}")
    if any((case["profile"] == "quality") != (row["stage2_ms"] > 0) for row in stages):
        raise ValueError(f"Wrong Stage 2 activation: {path}")
    before, after = data["obs_before"], data["obs_after"]
    result = dict(case=case, started_utc=data["started_utc"], ended_utc=data["ended_utc"],
                  gpu_condition=data["gpu_condition"], video=data["video"], notes=data["notes"],
                  published_keys_per_second=stats.mean(row["keyed_fps"] for row in stages),
                  displayed_keys_per_second=stats.median(row["keyed_fps"] for row in age),
                  displayed_age_ms=stats.median(row["age_median_ms"] for row in age),
                  displayed_age_p95_ms=stats.median(row["age_p95_ms"] for row in age),
                  gpu_mib_median=data["gpu_mib_median"], gpu_mib_peak=data["gpu_mib_peak"],
                  gpu_utilization_median=stats.median(row["utilization"] for row in data["gpu_samples"]),
                  gpu_process_ids=sorted({pid for row in data["gpu_samples"] for pid in row["process_ids"]}),
                  render_skipped_frames=after["renderSkippedFrames"] - before["renderSkippedFrames"],
                  render_total_frames=after["renderTotalFrames"] - before["renderTotalFrames"],
                  stage_cuda_interval_ms={key: stats.mean(row[key] for row in stages)
                                          for key in ("moge_ms", "rvm_ms", "stage1_ms", "stage2_ms")},
                  worker_ms=stats.mean(row["worker_ms"] for row in stages),
                  telemetry_rows=dict(age=len(age), stages=len(stages), refiner=len(refiner)))
    if "selected_transport" in data:
        result["selected_transport"] = data["selected_transport"]
    if refiner:
        result["refiner_median"] = {key: stats.median(row[key] for row in refiner) for key in refiner[0]}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-incomplete", action="store_true")
    args = parser.parse_args()
    rows, missing = [], []
    for case in json.loads(args.cases.read_text()):
        name = f"{case['profile']}-{case['transport']}-{'on' if case['refiner'] else 'off'}-{case['size']}"
        path = args.directory / (name + ".json")
        if not path.exists():
            missing.append(name)
            continue
        row = summarize(path)
        if row["case"] != case:
            raise ValueError(f"Case differs from manifest: {path}")
        rows.append(row)
    if missing and not args.allow_incomplete:
        raise ValueError("Missing cases: " + ", ".join(missing))
    # REUSE-IgnoreStart
    result = {"$comment": "SPDX-License-Identifier: GPL-3.0-or-later",
    # REUSE-IgnoreEnd
              "aggregation": "Mean of W09 publication/stage intervals; median of W15 five-second display windows. "
                             "MoGe is per refresh. CUDA intervals include synchronized host submission gaps. "
                             "VRAM is whole-device usage, including OBS and desktop apps.",
              "missing_cases": missing, "cases": rows}
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"Saved {len(rows)} cases; {len(missing)} missing")


if __name__ == "__main__":
    main()
