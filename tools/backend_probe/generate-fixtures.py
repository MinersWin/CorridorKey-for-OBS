# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate W01's fixture and exact FP16/BF16 variants for backend capability tests."""
import argparse
import importlib.util
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, numpy_helper
import ml_dtypes


def generate(destination):
    original = Path(__file__).resolve().parents[1] / "generate-test-graph.py"
    spec = importlib.util.spec_from_file_location("w01_fixture", original)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.generate(destination / "fp32")
    for name, dtype, tensor_type in [("fp16", np.float16, TensorProto.FLOAT16),
                                     ("bf16", ml_dtypes.bfloat16, TensorProto.BFLOAT16)]:
        model = onnx.load(destination / "fp32" / "tiny.onnx")
        for value in [*model.graph.input, *model.graph.output]:
            value.type.tensor_type.elem_type = tensor_type
        for initializer in model.graph.initializer:
            data = numpy_helper.to_array(initializer).astype(dtype)
            initializer.CopyFrom(numpy_helper.from_array(data, initializer.name))
        folder = destination / name
        folder.mkdir(parents=True, exist_ok=True)
        # Embedded synthetic constants avoid sharing FP32 external weights.
        onnx.save_model(model, folder / "tiny.onnx")
        onnx.checker.check_model(str(folder / "tiny.onnx"), full_check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    generate(parser.parse_args().destination)
