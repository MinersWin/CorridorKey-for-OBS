# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
"""Small anonymous HTTP fixture. Payloads stay under 1 MiB per request."""
import collections
import http.server
import json
import pathlib
import shutil
import socket
import subprocess
import sys
import threading
import zipfile

EXE = sys.argv[1]
ROOT = pathlib.Path(sys.argv[2]).resolve()
ROOT.mkdir(parents=True, exist_ok=True)
DATA = bytes(i % 251 for i in range(256 * 1024))
requests = collections.defaultdict(list)
request_lock = threading.Lock()
errors = []


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def end_headers(self):
        self.send_header("Connection", "close")
        self.close_connection = True
        super().end_headers()

    def do_GET(self):
        mode = self.path.strip("/")
        with request_lock:
            index = len(requests[mode])
            requests[mode].append(dict(self.headers))
        if self.headers.get("Authorization") or self.headers.get("Cookie"):
            errors.append("Credentials reached fixture")
        raw_range = self.headers.get("Range", "")
        start = int(raw_range.removeprefix("bytes=").removesuffix("-")) if raw_range else 0
        if raw_range and self.headers.get("If-Range") != '"v1"':
            errors.append("Missing/wrong resume validator")
        if mode in ("redirect", "downgrade", "redirect-loop"):
            self.send_response(302)
            location = {"redirect": "https://fixture.invalid/redirect-target", "downgrade": "http://example.invalid/payload", "redirect-loop": "https://fixture.invalid/redirect-loop"}[mode]
            self.send_header("Location", location)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if mode == "cancel-backoff" or (mode == "rate" and index == 0):
            self.send_response(429)
            self.send_header("Retry-After", "2")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if mode == "range416" and raw_range:
            self.send_response(416)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if mode == "ignore":
            start = 0
        payload = DATA[start:]
        if mode == "hash":
            payload = b"x" * len(payload)
        self.send_response(206 if start else 200)
        if mode != "no-validator":
            self.send_header("ETag", '"v2"' if mode == "changed" else '"v1"')
        if start:
            begin = start + 1 if mode == "bad-range" else start
            self.send_header("Content-Range", f"bytes {begin}-{len(DATA)-1}/{len(DATA)}")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        try:
            if mode == "loss" and index == 0:
                self.wfile.write(payload[:65536])
                self.wfile.flush()
                self.connection.shutdown(socket.SHUT_RDWR)
                self.connection.close()
            else:
                for offset in range(0, len(payload), 16384):
                    self.wfile.write(payload[offset:offset + 16384])
                    self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            pass


def run(*args, expected=0):
    result = subprocess.run([EXE, *map(str, args)], capture_output=True, text=True, timeout=30)
    if result.returncode != expected:
        raise AssertionError(f"{args}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")


def clean(path):
    assert path.resolve().parent == ROOT, "Cleanup escaped the fixture directory"
    if path.exists():
        shutil.rmtree(path)


def ready(process):
    # A reader thread prevents a child startup error from hanging CI.
    future = []
    reader = threading.Thread(target=lambda: future.append(process.stdout.readline()), daemon=True)
    reader.start()
    reader.join(10)
    if not future or future[0].strip() != "READY":
        process.kill()
        raise AssertionError("Lock/lease child did not become ready")


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
server.daemon_threads = True
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
base = f"http://127.0.0.1:{server.server_port}"
try:
    modes = ["normal", "resume", "ignore", "changed", "rate", "loss", "cancel", "hash", "disk", "disk-mid", "unaccepted", "bad-range", "range416", "no-validator", "redirect", "downgrade", "redirect-loop", "cancel-backoff"]
    for mode in modes:
        directory = ROOT / mode
        clean(directory)
        run("--http-case", base, mode, directory)
        print("PASS", mode, flush=True)
    for mode in ("resume", "ignore", "changed", "bad-range", "range416", "no-validator"):
        assert requests[mode][0].get("Range") == "bytes=65536-", mode
    for mode in ("changed", "range416", "no-validator"):
        assert "Range" not in requests[mode][1], mode
    assert requests["loss"][1]["Range"] == "bytes=65536-"
    assert requests["cancel"][1].get("Range"), "Cancellation did not resume"
    assert len(requests["ignore"]) == 1
    assert len(requests["rate"]) == 2
    assert len(requests["bad-range"]) == 5
    assert len(requests["redirect-target"]) == 1
    assert not requests["unaccepted"] and not requests["disk"]
    assert not errors, errors

    dedup = ROOT / "dedup"
    clean(dedup)
    children = [subprocess.Popen([EXE, "--http-case", base, "dedup", str(dedup)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) for _ in range(2)]
    for child in children:
        stdout, stderr = child.communicate(timeout=30)
        assert child.returncode == 0, (stdout, stderr)
    assert len(requests["dedup"]) == 1
    print("PASS cross-process download deduplication", flush=True)

    lock = ROOT / "process.lock"
    child = subprocess.Popen([EXE, "--hold-lock", str(lock)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        ready(child)
        run("--try-lock", lock, expected=3)
    finally:
        child.kill()
        child.wait(timeout=10)
    run("--try-lock", lock)
    print("PASS cross-process lock release after crash", flush=True)

    lease_root = ROOT / "leases"
    clean(lease_root)
    child = subprocess.Popen([EXE, "--hold-lease", str(lease_root)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        ready(child)
        run("--remove-model", lease_root, expected=3)
    finally:
        child.kill()
        child.wait(timeout=10)
    run("--remove-model", lease_root)
    print("PASS cross-process reader lease and crash release", flush=True)

    folder = ROOT / "bundle"
    clean(folder)
    run("--write-fixture", folder)
    run("--offline-case", folder, ROOT / "import-folder", "pass")
    for compression in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
        archive = ROOT / f"bundle-{compression}.zip"
        with zipfile.ZipFile(archive, "w", compression=compression, allowZip64=True) as z:
            for p in (folder / "blobs").iterdir():
                # Force ZIP64 local header handling on a tiny payload.
                with z.open("blobs/" + p.name, "w", force_zip64=True) as dest:
                    dest.write(p.read_bytes())
        run("--offline-case", archive, ROOT / f"import-zip-{compression}", "pass")
    bad = ROOT / "bad.zip"
    with zipfile.ZipFile(bad, "w") as z:
        for p in (folder / "blobs").iterdir():
            z.writestr("blobs/" + p.name, p.read_bytes())
        z.writestr("../outside", b"no")
    run("--offline-case", bad, ROOT / "import-traversal", "fail")
    with zipfile.ZipFile(bad, "w") as z:
        for p in (folder / "blobs").iterdir():
            z.writestr("blobs/" + p.name, b"x" * p.stat().st_size)
    run("--offline-case", bad, ROOT / "import-bad-hash", "fail")
    with zipfile.ZipFile(bad, "w") as z:
        for p in (folder / "blobs").iterdir():
            z.writestr("blobs/" + p.name, p.read_bytes() + b"excess")
    run("--offline-case", bad, ROOT / "import-size", "fail")
    with zipfile.ZipFile(bad, "w") as z:
        p = next((folder / "blobs").iterdir())
        z.writestr("blobs/" + p.name, p.read_bytes())
        z.writestr("blobs/" + p.name, p.read_bytes())
    run("--offline-case", bad, ROOT / "import-duplicate", "fail")
    print("PASS folder and ZIP imports, traversal, hashes, sizes, duplicates", flush=True)
finally:
    server.shutdown()
    server.server_close()
