# SPDX-License-Identifier: GPL-3.0-or-later
"""Download verified development artifacts and assemble private Windows test bundles.

These are test bundles. Read docs/dev/runtime-bundles.md before redistributing.
Python is a development tool; native inference has no Python dependency.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request
import zipfile


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def prepare(cache, offline):
    pins = json.loads(Path(__file__).with_name("windows-artifacts.json").read_text())
    downloads = cache / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    archives = {}
    for artifact in pins["artifacts"]:
        target = downloads / artifact["filename"]
        if not target.exists():
            if offline:
                raise FileNotFoundError(target)
            temporary = target.with_suffix(".part")
            print(f"Downloading {artifact['id']}: {artifact['bytes']:,} bytes", flush=True)
            with urllib.request.urlopen(artifact["url"], timeout=60) as source, temporary.open("wb") as out:
                shutil.copyfileobj(source, out)
            if digest(temporary) != artifact["sha256"]:
                temporary.unlink()
                raise ValueError(f"Download checksum mismatch: {artifact['id']}")
            temporary.replace(target)
        if target.stat().st_size != artifact["bytes"] or digest(target) != artifact["sha256"]:
            raise ValueError(f"Artifact checksum mismatch: {target.name}")
        archives[artifact["id"]] = target

    nv = cache / "bundles" / "nvidia-1.31.1-rtx0.5"
    dml = cache / "bundles" / "dml-1.24.4"
    sdk = cache / "ort-sdk-1.31.1" / "include"
    for folder in (nv, dml, sdk):
        folder.mkdir(parents=True, exist_ok=True)
    wanted = {
        "ort": {"onnxruntime.dll", "onnxruntime_providers_shared.dll", "onnxruntime_providers_cuda.dll"},
        "rtx": {"cudart64_13.dll", "tensorrt_rtx_1_7.dll", "tensorrt_onnxparser_rtx_1_7.dll", "onnxruntime_providers_nv_tensorrt_rtx.dll"},
        "cublas": {"cublas64_13.dll", "cublasLt64_13.dll"},
        "dml": {"onnxruntime.dll", "DirectML.dll"},
    }
    for name, path in archives.items():
        folder = dml if name == "dml" else nv
        found = set()
        with zipfile.ZipFile(path) as archive:
            for info in archive.infolist():
                basename = Path(info.filename).name
                destination = None
                if basename in wanted[name]:
                    if basename in found:
                        raise ValueError(f"Ambiguous library in {path.name}: {basename}")
                    found.add(basename)
                    destination = folder / basename
                elif name == "ort" and "/include/" in info.filename and basename.endswith(".h"):
                    # C API headers in this archive have no nested relative includes.
                    destination = sdk / basename
                elif "license" in basename.lower() or "thirdpartynotices" in basename.lower():
                    destination = folder / "licenses" / name / basename
                if destination and not info.is_dir():
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    with archive.open(info) as source, destination.open("wb") as out:
                        shutil.copyfileobj(source, out)
        if found != wanted[name]:
            raise ValueError(f"Missing runtime libraries: {wanted[name] - found}")

    definitions = [
        ("rtx", nv, "tensorrt-rtx", "plugin", "NvTensorRTRTXExecutionProvider", "onnxruntime_providers_nv_tensorrt_rtx.dll",
         ["cudart64_13.dll", "onnxruntime_providers_shared.dll", "tensorrt_rtx_1_7.dll", "tensorrt_onnxparser_rtx_1_7.dll"], "1.31.1", "0.5.0"),
        ("cuda", nv, "cuda", "classic", "CUDAExecutionProvider", "onnxruntime_providers_cuda.dll",
         ["cudart64_13.dll", "onnxruntime_providers_shared.dll", "cublasLt64_13.dll", "cublas64_13.dll"], "1.31.1", "1.31.1"),
        ("dml", dml, "directml", "classic", "DmlExecutionProvider", None, ["DirectML.dll"], "1.24.4", "1.24.4"),
        ("cpu", nv, "cpu", "cpu", "CPUExecutionProvider", None, [], "1.31.1", "1.31.1"),
    ]
    for name, folder, backend, registration, provider_name, provider, preload, version, ep_version in definitions:
        files = {"onnxruntime.dll", *preload}
        if provider:
            files.add(provider)
        descriptor = dict(schema=1, id=folder.name, backend=backend, registration=registration,
                          runtime="onnxruntime.dll", runtime_build=version, provider_build=ep_version,
                          provider_name=provider_name, preload=preload,
                          files={file: digest(folder / file) for file in sorted(files)})
        if provider:
            descriptor["provider"] = provider
        (folder / f"{name}.json").write_text(json.dumps(descriptor, indent=2) + "\n")
        print(f"{name}: {sum((folder / file).stat().st_size for file in files):,} bytes; {folder / (name + '.json')}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=Path(".cache"))
    parser.add_argument("--offline", action="store_true", help="Use already-downloaded, checksum-verified archives")
    args = parser.parse_args()
    prepare(args.cache.resolve(), args.offline)
