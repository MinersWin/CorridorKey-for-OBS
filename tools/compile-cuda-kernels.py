# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile our kernels to redistributable PTX source using a local NVRTC SDK."""
import argparse
import ctypes as c
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("nvrtc", type=Path)
args = p.parse_args()
root = Path(__file__).resolve().parent.parent
dll = c.WinDLL(str(args.nvrtc.resolve()))
program = c.c_void_p()
source = (root / "src/gpu/cuda_kernels.cu").read_bytes()
assert dll.nvrtcCreateProgram(c.byref(program), source, b"cuda_kernels.cu", 0, None, None) == 0
options = (c.c_char_p * 3)(b"--gpu-architecture=compute_75", b"--fmad=false", b"--std=c++11")
result = dll.nvrtcCompileProgram(program, len(options), options)
length = c.c_size_t()
dll.nvrtcGetProgramLogSize(program, c.byref(length))
log = c.create_string_buffer(length.value)
dll.nvrtcGetProgramLog(program, log)
if result:
    raise RuntimeError(log.value.decode())
assert dll.nvrtcGetPTXSize(program, c.byref(length)) == 0
ptx = c.create_string_buffer(length.value)
assert dll.nvrtcGetPTX(program, ptx) == 0
dll.nvrtcDestroyProgram(c.byref(program))
text = '// SPDX-License-Identifier: GPL-3.0-or-later\n// Generated from cuda_kernels.cu; do not edit.\n'
text += 'static constexpr char kernel_ptx[] = R"CKPTX(\n' + ptx.value.decode() + ')CKPTX";\n'
(root / "src/gpu/cuda_kernels.inc").write_text(text, encoding="utf-8", newline="\n")
