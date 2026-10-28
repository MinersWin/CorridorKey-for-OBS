# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
"""Generate an independently authored Add graph and external-data binder fixture."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

import numpy as np
import onnx
from onnx import TensorProto, helper
from onnx.external_data_helper import set_external_data
from onnx.reference import ReferenceEvaluator


def generate(destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    shape = [1, 1, 2, 2]
    values = [0.0, 0.25, 0.5, 1.0]
    payload = struct.pack("<4f", *values)
    tensor = helper.make_tensor("bias", TensorProto.FLOAT, shape, payload, raw=True)
    set_external_data(tensor, location="tiny.weights", offset=0, length=len(payload))
    tensor.ClearField("raw_data")
    graph = helper.make_graph(
        [helper.make_node("Add", ["input", "bias"], ["output"])],
        "corridorkey_synthetic_add",
        [helper.make_tensor_value_info("input", TensorProto.FLOAT, shape)],
        [helper.make_tensor_value_info("output", TensorProto.FLOAT, shape)],
        [tensor],
    )
    model = helper.make_model(graph, producer_name="corridorkey-tests", opset_imports=[helper.make_opsetid("", 18)])
    model.ir_version = 8
    graph_path = destination / "tiny.onnx"
    graph_path.write_bytes(model.SerializeToString(deterministic=True))
    (destination / "tiny.weights").write_bytes(payload)

    # A minimal safetensors file lets a binder reproduce tiny.weights without models.
    header = json.dumps({"bias": {"dtype": "F32", "shape": shape, "data_offsets": [0, 16]}}, separators=(",", ":")).encode()
    header += b" " * ((-len(header)) % 8)
    (destination / "tiny.safetensors").write_bytes(struct.pack("<Q", len(header)) + header + payload)
    recipe = {
        "fixture_version": 1,
        "inputs": [{"name": "input", "dtype": "float32", "shape": shape}],
        "outputs": [{"name": "output", "dtype": "float32", "shape": shape}],
        "weights": [{"name": "bias", "dtype": "float32", "shape": shape, "offset": 0, "length": 16,
                     "source": {"file": "tiny.safetensors", "key": "bias", "ops": []}}],
        "graph_sha256": hashlib.sha256(graph_path.read_bytes()).hexdigest(),
        "weights_sha256": hashlib.sha256(payload).hexdigest(),
    }
    (destination / "tiny.json").write_text(json.dumps(recipe, indent=2) + "\n", encoding="utf-8")
    onnx.checker.check_model(str(graph_path), full_check=True)
    loaded = onnx.load(str(graph_path))
    sample = np.array([1, 2, 3, 4], dtype=np.float32).reshape(shape)
    result = ReferenceEvaluator(loaded).run(None, {"input": sample})[0]
    np.testing.assert_array_equal(result, sample + np.array(values, dtype=np.float32).reshape(shape))
    print(f"Validated synthetic Add graph, external weights and reference output: {graph_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    generate(parser.parse_args().destination)
