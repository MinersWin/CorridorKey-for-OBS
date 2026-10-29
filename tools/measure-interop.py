# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure an explicitly configured portable OBS over its local test websocket.

Requires websocket-client and nvidia-smi. Does not start/stop OBS or install models.
The caller supplies a JSON list of profile/transport/refiner/size cases and two
image fixtures. Logs and JSON measurements remain in the selected output folder.
"""
import argparse
import json
import re
import statistics
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

import websocket


def metrics(text, tag):
    return [{k: float(v) for k, v in re.findall(r"(\w+)=(-?\d+(?:\.\d+)?)", line)}
            for line in text.splitlines() if tag in line]


def gpu_sample():
    def query(value):
        return subprocess.run(["nvidia-smi", value, "--format=csv,noheader,nounits"],
                              capture_output=True, text=True, check=True).stdout.strip()
    memory, utilization = query("--query-gpu=memory.used,utilization.gpu").splitlines()[0].split(",")
    # PIDs allow comparison with the idle baseline without exporting personal paths.
    pids = query("--query-compute-apps=pid")
    return dict(memory_mib=float(memory), utilization=float(utilization),
                process_ids=sorted({int(x) for x in pids.splitlines() if x.strip().isdigit()}))


def main(args):
    args.output.mkdir(parents=True, exist_ok=True)
    log = max((args.obs / "config/obs-studio/logs").glob("*.txt"), key=lambda x: x.stat().st_mtime)
    read = lambda: log.read_text(encoding="utf-8", errors="replace")
    connection = websocket.create_connection(f"ws://127.0.0.1:{args.port}", timeout=30)
    hello = json.loads(connection.recv())
    if "authentication" in hello["d"]:
        raise RuntimeError("Use a dedicated local test instance with its test websocket configured")
    connection.send(json.dumps({"op": 1, "d": {"rpcVersion": 1, "eventSubscriptions": 0}}))
    connection.recv()

    def request(kind, data=None):
        connection.send(json.dumps({"op": 6, "d": dict(requestType=kind, requestId=kind,
                                                       requestData=data or {})}))
        response = json.loads(connection.recv())["d"]
        if not response["requestStatus"]["result"]:
            raise RuntimeError(response)
        return response.get("responseData", {})

    try:
        for case in json.loads(args.cases.read_text()):
            profile, transport, refine, size = (case[x] for x in ("profile", "transport", "refiner", "size"))
            if profile not in ("facecam", "balanced", "quality") or transport not in ("auto", "staged"):
                raise ValueError(case)
            if size not in ("1080", "4k") or refine not in (False, True):
                raise ValueError(case)
            name = f"{profile}-{transport}-{'on' if refine else 'off'}-{size}"
            destination = args.output / f"{name}.json"
            if destination.exists():
                print(f"Retaining existing {name}", flush=True)
                continue
            print(f"Preparing {name}", flush=True)
            start = len(read())
            request("SetInputSettings", dict(inputName=args.source, inputSettings={
                "file": str((args.image_4k if size == "4k" else args.image_1080).resolve())}))
            settings = dict(profile="facecam-60" if profile == "facecam" else profile,
                            grid="256x512" if profile == "facecam" else "512x512",
                            roi_enabled=profile != "facecam", backend="tensorrt",
                            transport="staged_grid" if transport == "staged" else "auto",
                            refiner_enabled=refine, edge_refiner=refine,
                            stale_timeout_ms=1000,
                            refiner_max_tiles=dict(facecam=384, balanced=512, quality=768)[profile])
            request("SetSourceFilterSettings", dict(sourceName=args.source, filterName=args.filter,
                                                    filterSettings=settings))
            deadline = time.monotonic() + 900
            while time.monotonic() < deadline:
                rows = metrics(read()[start:], "[corridorkey:w09]")
                ages = metrics(read()[start:], "[corridorkey:w08] age_")
                if rows and ages and rows[-1].get("keyed_fps", 0) > 0 and ages[-1].get("age_median_ms", -1) >= 0:
                    break
                time.sleep(2)
            else:
                raise RuntimeError(f"No keyed output for {name} within preparation deadline")
            time.sleep(args.settle)
            print(f"Measuring {name}", flush=True)
            started = datetime.now(timezone.utc).isoformat()
            start = len(read())
            before = request("GetStats")
            samples = []
            for _ in range(args.seconds):
                samples.append(gpu_sample())
                time.sleep(1)
            raw = read()[start:]
            age = metrics(raw, "[corridorkey:w08] age_")
            stages = metrics(raw, "[corridorkey:w09]")
            refiner = metrics(raw, "[corridorkey:w13] tiles=")
            if not age or not stages or (refine and not refiner):
                raise RuntimeError(f"Incomplete measurement for {name}")
            report = dict(case=case, settings=settings, seconds=args.seconds, log=log.name,
                          gpu_condition=args.gpu_condition, notes=args.notes,
                          started_utc=started, ended_utc=datetime.now(timezone.utc).isoformat(),
                          video=request("GetVideoSettings"), age=age, stages=stages, refiner=refiner,
                          gpu_samples=samples, gpu_mib_median=statistics.median(x["memory_mib"] for x in samples),
                          gpu_mib_peak=max(x["memory_mib"] for x in samples),
                          obs_before=before, obs_after=request("GetStats"))
            destination.write_text(json.dumps(report, indent=2))
            destination.with_suffix(".log").write_text(raw)
            print(f"Saved {name}: {statistics.mean(x['keyed_fps'] for x in stages):.1f} keys/s, "
                  f"{statistics.median(x['age_median_ms'] for x in age):.1f} ms age", flush=True)
    finally:
        connection.close()


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--obs", type=Path, required=True)
    p.add_argument("--cases", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--image-1080", type=Path, required=True)
    p.add_argument("--image-4k", type=Path, required=True)
    p.add_argument("--source", default="W09 Facecam")
    p.add_argument("--filter", default="CorridorKey")
    p.add_argument("--port", type=int, default=18110)
    p.add_argument("--seconds", type=int, default=35)
    p.add_argument("--settle", type=int, default=15)
    p.add_argument("--gpu-condition", choices=["shared", "reserved-window"], default="shared")
    p.add_argument("--notes", default="")
    main(p.parse_args())
