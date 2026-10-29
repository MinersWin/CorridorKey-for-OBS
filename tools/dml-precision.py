# SPDX-License-Identifier: GPL-3.0-or-later
"""Screen local FP16 graph conversions against the exporter's FP32 references.

This is a precision experiment, not an exporter or a release qualification.
Passing the decoded-output screen still requires the complete Q1/W13 checks.
Run one graph per process to release ORT and conversion memory between graphs.
"""
import argparse
import json
import time
from pathlib import Path

import numpy as np


def main(args):
    source = args.exports.resolve()
    destination = (args.output / args.graph).resolve()
    if destination == source or source in destination.parents:
        raise ValueError("Precision artifacts must be outside the export directory")
    destination.mkdir(parents=True, exist_ok=True)
    graph = destination / (args.graph + ".onnx")
    if args.prepare:
        import onnx
        from onnxconverter_common.float16 import convert_float_to_float16
        model = onnx.load(str(source / (args.graph + ".onnx")))
        model = convert_float_to_float16(model, keep_io_types=True, disable_shape_infer=True)
        # Preserve FP16 internal consumers of outputs such as Stage 1's baton.
        # The converter adds an FP32 boundary cast but leaves those consumers on it.
        outputs = {value.name for value in model.graph.output}
        for cast in list(model.graph.node):
            if cast.op_type == "Cast" and cast.output[0] in outputs and "cast_to_" in cast.name:
                for consumer in model.graph.node:
                    if consumer is cast:
                        continue
                    for index, name in enumerate(consumer.input):
                        if name == cast.output[0]:
                            consumer.input[index] = cast.input[0]
        onnx.save_model(model, str(graph), save_as_external_data=True,
                        all_tensors_to_one_file=True, location="fp16.weights", size_threshold=1024)
        print(f"Prepared {graph}", flush=True)
        return
    import onnxruntime as ort
    report = {"graph": args.graph, "runtime": ort.__version__, "precision": "fp16",
              "decoded_mean_abs_screen": .002, "qualified_for_live_use": False,
              "qualification": "Requires Q1/W13 full-pipeline parity, including edge and temporal metrics"}
    try:
        options = ort.SessionOptions()
        options.enable_mem_pattern = False
        options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        options.add_session_config_entry("session.disable_cpu_ep_fallback", "1")
        session = ort.InferenceSession(str(graph), options, providers=["DmlExecutionProvider"])
        reference = np.load(source / (args.graph + ".ref.npz"))
        feeds = {item.name: reference["in_" + item.name].astype(np.float32) for item in session.get_inputs()}
        times = []
        for _ in range(5):
            start = time.perf_counter()
            outputs = session.run(None, feeds)
            times.append((time.perf_counter() - start) * 1000)
        report["warm_ms_including_copy"] = float(np.median(times[2:]))
        report["outputs"] = {}
        for spec, output in zip(session.get_outputs(), outputs):
            expected = reference["out_" + spec.name]
            difference = np.abs(output.astype(np.float64) - expected)
            finite = bool(np.isfinite(output).all())
            report["outputs"][spec.name] = dict(finite=finite,
                max_abs=float(difference.max()) if finite else None,
                mean_abs=float(difference.mean()) if finite else None)
        decoded = [report["outputs"][name] for name in ("alpha", "fg")]
        report["screen_passed"] = all(x["finite"] and x["mean_abs"] <= .002 for x in decoded)
    except Exception as error:
        report.update(screen_passed=False, error=str(error))
    (destination / "precision.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exports", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--graph", required=True)
    parser.add_argument("--prepare", action="store_true")
    main(parser.parse_args())
