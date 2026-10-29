# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
"""Generate small untrusted-container fixtures and exercise the native binder."""
import copy
import hashlib
import io
import json
import random
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import zipfile
import zlib

BINDER = Path(sys.argv.pop(1)).resolve()
ROOT = Path(sys.argv.pop(1)).resolve()
API_TEST = Path(sys.argv.pop(1)).resolve()
ROOT.mkdir(parents=True, exist_ok=True)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def safe(data, shape, dtype="F32", header=None):
    if header is None:
        header = {"a": {"dtype": dtype, "shape": shape, "data_offsets": [0, len(data)]}}
    text = json.dumps(header).encode() if isinstance(header, dict) else header
    text += b" " * (-len(text) % 8)
    return struct.pack("<Q", len(text)) + text + data


def npy(data, shape, dtype="<f4", version=1, header=None):
    text = (repr({"descr": dtype, "fortran_order": False, "shape": tuple(shape)}) if header is None else header).encode()
    prefix_len = 10 if version == 1 else 12
    text += b" " * (-(prefix_len + len(text) + 1) % 64) + b"\n"
    return b"\x93NUMPY" + bytes([version, 0]) + struct.pack("<H" if version == 1 else "<I", len(text)) + text + data


def archive(data, name="a.npy", method=zipfile.ZIP_STORED):
    result = io.BytesIO()
    with zipfile.ZipFile(result, "w", compression=method) as z:
        z.writestr(name, data)
    return result.getvalue()


def zip64(data, descriptor=False):
    name = b"a.npy"
    crc = zlib.crc32(data)
    extra = struct.pack("<HHQQ", 1, 16, len(data), len(data))
    flags = 8 if descriptor else 0
    local = struct.pack("<IHHHHHIIIHH", 0x04034B50, 45, flags, 0, 0, 0,
                        0 if descriptor else crc, 0xFFFFFFFF, 0xFFFFFFFF, len(name), len(extra)) + name + extra + data
    if descriptor:
        local += struct.pack("<IIQQ", 0x08074B50, crc, len(data), len(data))
    extra = struct.pack("<HHQQQ", 1, 24, len(data), len(data), 0)
    central = struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 45, 45, flags, 0, 0, 0, crc,
                          0xFFFFFFFF, 0xFFFFFFFF, len(name), len(extra), 0, 0, 0, 0, 0xFFFFFFFF) + name + extra
    end64 = struct.pack("<IQHHIIQQQQ", 0x06064B50, 44, 45, 45, 0, 0, 1, 1, len(central), len(local))
    locator = struct.pack("<IIQI", 0x07064B50, 0, len(local) + len(central), 1)
    end = struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 65535, 65535, 0xFFFFFFFF, 0xFFFFFFFF, 0)
    return local + central + end64 + locator + end


class Binder(unittest.TestCase):
    def setUp(self):
        self.dir = ROOT / self._testMethodName
        self.dir.mkdir(exist_ok=True)
        self.output = self.dir / "tiny.weights"
        self.data = struct.pack("<6f", 1, 2, 3, 4, 5, 6)
        self.source = safe(self.data, [2, 3])
        self.format = "safetensors"
        self.row = {"name": "a", "dtype": "float32", "shape": [2, 3], "offset": 0, "length": 24,
                    "source": {"file": "test", "key": "a", "ops": []}}
        self.manifest = {"graph": "tiny", "opset": 18, "inputs": [], "outputs": [], "weights": [self.row], "sources": {}}

    def run_bind(self, ok=True, expected="-", source_hash=None):
        path = self.dir / "source.bin"
        path.write_bytes(self.source)
        self.manifest["sources"] = {"test": {"format": self.format, "size": len(self.source), "sha256": source_hash or sha(self.source)}}
        meta = self.dir / "tiny.json"
        meta.write_text(json.dumps(self.manifest))
        result = subprocess.run([str(BINDER), str(meta), str(self.output), expected, "test", str(path)], capture_output=True, text=True, timeout=10)
        if ok:
            self.assertEqual(result.returncode, 0, result.stderr)
            receipt = json.loads(Path(str(self.output) + ".ready.json").read_text())
            self.assertEqual(receipt["weights_sha256"], sha(self.output.read_bytes()))
            self.assertEqual(receipt["manifest_sha256"], sha(meta.read_bytes()))
        else:
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(Path(str(self.output) + ".ready.json").exists())
        self.assertFalse(Path(str(self.output) + ".partial").exists())
        self.assertFalse(Path(str(self.output) + ".tensor.tmp").exists())
        return result

    def test_copy_padding_zeros_and_recorded_hash(self):
        zero = copy.deepcopy(self.row)
        zero.update(name="z", offset=64, shape=[2], length=8, source={"file": None, "key": None, "ops": [["zeros", None]]})
        self.manifest["weights"].insert(0, zero)  # Unsorted input table.
        self.run_bind(expected=sha(self.data + bytes(48)))
        self.assertEqual(self.output.read_bytes(), self.data + bytes(48))

    def test_numpy_transpose_merge_squeeze(self):
        self.format = "npz"
        self.source = archive(npy(self.data, [1, 2, 3]), "tree/a.npy")
        self.row["source"].update(key="tree/a", ops=[["squeeze", 0], ["transpose", [1, 0]], ["merge", 0, 2]])
        self.row["shape"] = [6]
        self.run_bind()
        self.assertEqual(self.output.read_bytes(), struct.pack("<6f", 1, 4, 2, 5, 3, 6))

    def test_composed_transposes_with_noncontiguous_merge(self):
        self.row["source"]["ops"] = [["transpose", [1, 0]], ["merge", 0, 2]]
        self.row["shape"] = [6]
        self.run_bind()
        self.assertEqual(self.output.read_bytes(), struct.pack("<6f", 1, 4, 2, 5, 3, 6))
        self.row["source"]["ops"] = [["transpose", [1, 0]], ["transpose", [1, 0]]]
        self.row["shape"] = [2, 3]
        self.run_bind()
        self.assertEqual(self.output.read_bytes(), self.data)

    def test_fp16_rounding_signed_zero_subnormal_and_infinity(self):
        values = [0.0, -0.0, 1.0, -2.0, 1 + 2**-11, 1 + 3 * 2**-11, 2**-24, 2**-25, 65504.0, float("inf"), -float("inf")]
        self.source = safe(struct.pack("<" + "f" * len(values), *values), [len(values)])
        self.row.update(dtype="float16", shape=[len(values)], length=2 * len(values))
        self.run_bind()
        self.assertEqual(self.output.read_bytes(), struct.pack("<" + "e" * len(values), *values))

    def test_all_npy_versions_and_stored_deflate_zip64(self):
        self.format = "npz"
        for version in (1, 2, 3):
            for make in (archive, lambda x: archive(x, method=zipfile.ZIP_DEFLATED), zip64, lambda x: zip64(x, descriptor=True)):
                with self.subTest(version=version, make=make):
                    self.source = make(npy(self.data, [2, 3], version=version))
                    self.run_bind()
                    self.assertEqual(self.output.read_bytes(), self.data)

    def test_numeric_identity_types_and_scalar(self):
        self.format = "npz"
        for dtype, output, data in (("<i4", "int32", struct.pack("<i", -7)), ("<i8", "int64", struct.pack("<q", -9)),
                                    ("<f2", "float16", struct.pack("<e", 1.5)), ("|u1", "uint8", b"\xff"), ("|b1", "bool", b"\x01")):
            with self.subTest(dtype=dtype):
                self.source = archive(npy(data, [], dtype))
                self.row.update(dtype=output, shape=[], length=len(data))
                self.run_bind()
                self.assertEqual(self.output.read_bytes(), data)

    def test_zero_sized_tensor(self):
        self.source = safe(b"", [0])
        self.row.update(shape=[0], length=0)
        self.run_bind()
        self.assertEqual(self.output.read_bytes(), b"")

    def test_safetensors_corrupt_header_lengths(self):
        for data in (b"", b"1234567", struct.pack("<Q", 2**64 - 1), struct.pack("<Q", 16 * 1024 * 1024 + 8),
                     struct.pack("<Q", 8) + b"{}", struct.pack("<Q", 7) + b"{}     "):
            with self.subTest(data=data):
                self.source = data
                self.run_bind(False)

    def test_safetensors_bad_shape_offsets_dtype_metadata(self):
        valid = {"a": {"dtype": "F32", "shape": [2, 3], "data_offsets": [0, 24]}}
        cases = []
        for key, value in (("shape", [2**63, 8]), ("shape", [-1]), ("shape", [2.0, 3]), ("shape", [2**32, 2**32]),
                           ("shape", [1] * 9), ("data_offsets", [0, 2**64 - 1]), ("data_offsets", [4, 28]),
                           ("data_offsets", [24, 0]), ("dtype", "OBJECT")):
            bad = copy.deepcopy(valid); bad["a"][key] = value; cases.append(bad)
        cases.extend(({**valid, "b": valid["a"]}, {**valid, "__metadata__": {"x": 42}}))
        for header in cases:
            with self.subTest(header=header):
                self.source = safe(self.data, [], header=header)
                self.run_bind(False)

    def test_safetensors_duplicate_json_and_truncated_data(self):
        for data in (safe(self.data, [], header=b'{"a":{},"a":{}}'), safe(self.data, [2, 3])[:-1], safe(self.data, [2, 3]) + b"x"):
            self.source = data; self.run_bind(False)

    def test_npy_rejects_objects_structures_endianness_and_fortran(self):
        self.format = "npz"
        for dtype in ("|O", "|O8", ">f4", "=f4", "<c8", "<f8", "|V4", "<U1"):
            with self.subTest(dtype=dtype):
                self.source = archive(npy(self.data, [2, 3], dtype)); self.run_bind(False)
        for header in ("{'descr': '<f4', 'fortran_order': True, 'shape': (2,3)}",
                       "{'descr': [('a','<f4')], 'fortran_order': False, 'shape': (2,3)}",
                       "{'descr': '<f4', 'fortran_order': False, 'shape': (18446744073709551616,)}",
                       "{'descr': '<f4', 'fortran_order': False, 'shape': (-1,)}",
                       "{'descr': '<f4', 'fortran_order': False, 'shape': (1)}",
                       "{'descr': '<f4', 'descr':'<f4', 'shape': (2,3)}",
                       "{'descr': '<f4', 'fortran_order': False, 'shape': (2,3)}; exec('x')"):
            self.source = archive(npy(self.data, [], header=header)); self.run_bind(False)

    def test_npy_bad_magic_header_size_shape_length(self):
        self.format = "npz"
        good = npy(self.data, [2, 3])
        for data in (b"garbage", good[:-1], good + b"x", b"\x93NUMPY\x02\0" + struct.pack("<I", 65537),
                     npy(self.data, [2**32, 2**32]), good[:6] + b"\x04\0" + good[8:]):
            self.source = archive(data); self.run_bind(False)

    def test_zip_path_traversal_and_non_arrays(self):
        self.format = "npz"
        for name in ("../a.npy", "/a.npy", "C:/a.npy", "a/../a.npy", "a\\a.npy", "a//a.npy", "CON.npy", "a.pkl"):
            with self.subTest(name=name):
                self.source = archive(npy(self.data, [2, 3]), name); self.run_bind(False)

    def test_zip_crc_corruption_and_truncation(self):
        self.format = "npz"
        for method in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            good = archive(npy(self.data, [2, 3]), method=method)
            bad = bytearray(good); bad[40] ^= 1
            for data in (good[:-1], bytes(bad), good[:20]):
                self.source = data; self.run_bind(False)

    def test_zip_bombs_encryption_and_size_overflow(self):
        self.format = "npz"
        good = archive(npy(self.data, [2, 3]), method=zipfile.ZIP_DEFLATED)
        cd = good.index(b"PK\x01\x02")
        for offset, fmt, value in ((cd + 24, "I", 256 * 1024 * 1024 + 1), (cd + 24, "I", 10_000_000),
                                   (cd + 8, "H", 1), (cd + 10, "H", 99), (cd + 42, "I", 0xFFFFFFF0),
                                   (cd + 34, "H", 1), (cd + 38, "I", 0o120777 << 16)):
            bad = bytearray(good); struct.pack_into("<" + fmt, bad, offset, value)
            self.source = bytes(bad); self.run_bind(False)

    def test_zip_duplicate_members_and_overlaps(self):
        self.format = "npz"
        out = io.BytesIO()
        with zipfile.ZipFile(out, "w") as z:
            z.writestr("a.npy", npy(self.data, [2, 3]))
            z.writestr("b.npy", npy(self.data, [2, 3]))
        good = out.getvalue()
        self.source = good.replace(b"b.npy", b"a.npy"); self.run_bind(False)
        bad = bytearray(good)
        second = good.index(b"PK\x01\x02", good.index(b"PK\x01\x02") + 4)
        struct.pack_into("<I", bad, second + 42, 0)
        self.source = bytes(bad); self.run_bind(False)

    def test_manifest_bad_spans_shape_ops_and_source(self):
        original = copy.deepcopy(self.row)
        for key, value in (("offset", 1), ("offset", 2**64 - 64), ("length", 25), ("shape", [2**63, 8]), ("dtype", "object")):
            self.row.clear(); self.row.update(copy.deepcopy(original)); self.row[key] = value; self.run_bind(False)
        for ops in ([["squeeze", 0]], [["merge", 2, 1]], [["transpose", [0, 0]]], [["transpose", [0, 2]]],
                    [["transpose", [0]]], [["eval", "1"]], [["zeros"]], [["merge", 0, 2]], [None]):
            self.row.clear(); self.row.update(copy.deepcopy(original)); self.row["source"]["ops"] = ops; self.run_bind(False)
        self.row.clear(); self.row.update(copy.deepcopy(original)); self.row["source"]["key"] = "missing"; self.run_bind(False)

    def test_manifest_overlap_duplicate_and_unknown_source(self):
        other = copy.deepcopy(self.row); other["name"] = "b"
        self.manifest["weights"].append(other); self.run_bind(False)
        other.update(name="a", offset=64); self.run_bind(False)
        self.manifest["weights"].pop(); self.row["source"]["file"] = "unknown"; self.run_bind(False)

    def test_hash_failure_invalidates_existing_ready_marker(self):
        self.run_bind()
        self.run_bind(False, expected="0" * 64)
        self.run_bind(False, source_hash="0" * 64)
        self.manifest["weights_sha256"] = "0" * 64
        self.run_bind(False)

    def test_process_exit_leaves_no_ready_output_and_restart_repairs(self):
        output = self.dir / "api-test.weights"
        result = subprocess.run([str(API_TEST), "--crash", str(output)], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 73)
        self.assertTrue(Path(str(output) + ".partial").exists())
        self.assertFalse(Path(str(output) + ".ready.json").exists())
        self.output = output
        self.manifest["graph"] = "api-test"
        self.run_bind()
        self.assertEqual(self.output.read_bytes(), self.data)

    def test_deterministic_mutation_corpus_never_crashes(self):
        rng = random.Random(405)
        seeds = [("safetensors", self.source), ("npz", archive(npy(self.data, [2, 3]))),
                 ("npz", archive(npy(self.data, [2, 3]), method=zipfile.ZIP_DEFLATED)),
                 ("npz", zip64(npy(self.data, [2, 3]), descriptor=True))]
        for fmt, seed in seeds:
            for _ in range(24):
                bad = bytearray(seed)
                bad[rng.randrange(len(bad))] ^= 1 << rng.randrange(8)
                path = self.dir / "mutation.bin"
                path.write_bytes(bad)
                self.manifest["sources"] = {"test": {"format": fmt, "size": len(bad), "sha256": sha(bad)}}
                meta = self.dir / "tiny.json"
                meta.write_text(json.dumps(self.manifest))
                result = subprocess.run([str(BINDER), str(meta), str(self.output), "-", "test", str(path)],
                                        capture_output=True, text=True, timeout=5)
                self.assertIn(result.returncode, (0, 1), result.stderr)
                self.assertEqual(Path(str(self.output) + ".ready.json").exists(), result.returncode == 0)


if __name__ == "__main__":
    unittest.main()
