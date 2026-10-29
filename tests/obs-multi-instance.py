# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
"""Drive obs-w16.lua and retain per-instance logs and Windows memory samples."""
import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import re
import subprocess
import time


def command(directory, action):
    identity = str(time.time_ns())
    temporary = directory / "command.tmp"
    temporary.write_text(identity + " " + action, encoding="utf-8")
    temporary.replace(directory / "command.txt")
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        ack = directory / "ack.txt"
        if ack.exists() and ack.read_text(encoding="utf-8").startswith(identity + " "):
            return
        time.sleep(0.1)
    raise RuntimeError("OBS did not acknowledge " + action)


def process_memory(pid):
    class Counters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in (
                "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
                "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    process = kernel.OpenProcess(0x1000 | 0x10, False, pid)
    if not process:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        counters = Counters()
        counters.cb = ctypes.sizeof(counters)
        query = ctypes.WinDLL("psapi").GetProcessMemoryInfo
        query.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
        if not query(process, ctypes.byref(counters), counters.cb):
            raise ctypes.WinError(ctypes.get_last_error())
        return {"private_mib": counters.PrivateUsage / 1048576,
                "working_set_mib": counters.WorkingSetSize / 1048576}
    finally:
        kernel.CloseHandle(process)


def latest_instances(log):
    instances = {}
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        if "[corridorkey:w03]" not in line and "[corridorkey:w08] instance=" not in line:
            continue
        values = dict(re.findall(r"(\w+)=([^ ]+)", line))
        identity = values.pop("instance", None)
        if identity:
            instances.setdefault(identity, {}).update(values)
    return instances


def close_host(args):
    """Post WM_CLOSE to the specified portable host, including a hidden window."""
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                                wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    process = kernel.OpenProcess(0x1000 | 0x100000, False, args.pid)
    if not process:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        path = ctypes.create_unicode_buffer(32768)
        length = wintypes.DWORD(len(path))
        if not kernel.QueryFullProcessImageNameW(process, 0, path, ctypes.byref(length)):
            raise ctypes.WinError(ctypes.get_last_error())
        executable = Path(path.value)
        if executable.name.lower() != "obs64.exe" or "obs-test" not in executable.parts:
            raise RuntimeError("Refusing to close a host outside the dedicated obs-test tree")
        user = ctypes.WinDLL("user32", use_last_error=True)
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
        user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        posted = []

        @callback_type
        def visit(window, unused):
            owner = wintypes.DWORD()
            user.GetWindowThreadProcessId(window, ctypes.byref(owner))
            if owner.value == args.pid:
                title = ctypes.create_unicode_buffer(512)
                user.GetWindowTextW(window, title, len(title))
                if title.value.startswith("OBS ") and "Portable Mode" in title.value:
                    posted.append(bool(user.PostMessageW(window, 0x10, 0, 0)))
            return True

        user.EnumWindows(visit, 0)
        if not any(posted) or kernel.WaitForSingleObject(process, 30000) != 0:
            raise RuntimeError("Portable OBS did not exit cleanly within 30 seconds")
        log = args.log.read_text(encoding="utf-8", errors="replace")
        if "[corridorkey] unloaded" not in log or "Number of memory leaks: 0" not in log:
            raise RuntimeError("Missing plugin unload or zero-leak shutdown report")
        print("Portable OBS exited; plugin unloaded; OBS reports zero memory leaks")
    finally:
        kernel.CloseHandle(process)


def sample(args):
    start = time.monotonic()
    with args.output.open("w", encoding="utf-8") as output:
        while True:
            elapsed = time.monotonic() - start
            gpu = subprocess.check_output([
                "nvidia-smi", "--query-gpu=memory.used,utilization.gpu,temperature.gpu",
                "--format=csv,noheader,nounits"], text=True,
                creationflags=subprocess.CREATE_NO_WINDOW).strip().split(",")
            record = {"elapsed_s": elapsed, "utc": time.time(), **process_memory(args.pid),
                      "gpu_mib": float(gpu[0]), "gpu_percent": float(gpu[1]),
                      "gpu_celsius": float(gpu[2]), "instances": latest_instances(args.log)}
            record["gpu_processes"] = subprocess.check_output([
                "nvidia-smi", "--query-compute-apps=pid,process_name", "--format=csv,noheader"],
                text=True, creationflags=subprocess.CREATE_NO_WINDOW).splitlines()
            output.write(json.dumps(record) + "\n")
            output.flush()
            if elapsed >= args.seconds:
                break
            time.sleep(min(10, args.seconds - elapsed))
    records = [json.loads(line) for line in args.output.read_text().splitlines()]
    print(json.dumps({"samples": len(records), "duration_s": records[-1]["elapsed_s"],
                      "private_growth_mib": records[-1]["private_mib"] - records[0]["private_mib"],
                      "gpu_min_mib": min(r["gpu_mib"] for r in records),
                      "gpu_max_mib": max(r["gpu_mib"] for r in records)}, indent=2))


def lifecycle(args):
    steps = [("fake2", 15), ("add3", 10), ("duplicate", 5), ("unduplicate", 1), ("hide1", 3), ("show1", 5),
             ("away", 3), ("back", 5), ("portrait1", 5), ("restore1", 5),
             ("crop1", 3), ("disable1", 3), ("enable1", 5), ("canvas", 8),
             ("oom1", 3), ("normal1", 5), ("lost1", 3), ("normal1", 5),
             ("nan1", 3), ("normal1", 5), ("slow1", 1), ("remove1", 3),
             ("readd1", 10), ("remove3", 3), ("status", 1)]
    events = []
    for action, pause in steps:
        command(args.directory, action)
        events.append({"action": action, "utc": time.time()})
        time.sleep(pause)
        print(action, flush=True)
    text = args.log.read_text(encoding="utf-8", errors="replace")
    if "W16 FAILED" in text or "Error loading file" in text:
        raise RuntimeError("Lua driver failed; inspect OBS log")
    instances = latest_instances(args.log)
    if len(instances) < 3:
        raise RuntimeError("Missing telemetry for three instances")
    for identity, values in instances.items():
        if int(values.get("completed", 0)) == 0:
            raise RuntimeError("No completion for instance " + identity)
    args.output.write_text(json.dumps({"events": events, "instances": instances}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["lifecycle", "sample", "command", "close"])
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--pid", type=int)
    parser.add_argument("--seconds", type=int, default=1800)
    parser.add_argument("--action")
    args = parser.parse_args()
    if args.mode == "command":
        command(args.directory, args.action)
    elif args.mode == "lifecycle":
        lifecycle(args)
    elif args.mode == "close":
        close_host(args)
    else:
        sample(args)
