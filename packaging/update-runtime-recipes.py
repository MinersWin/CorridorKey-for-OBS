# SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh installer-owned NVIDIA recipes from verified official vendor archives.

Development only. No signing or key operations. Inputs stay in an ignored cache.
Review archive licences and docs/dev/packaging.md before admitting another pin.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PINS = {
    'nvrtc': ('nvrtc.zip', 'https://files.pythonhosted.org/packages/64/5b/aa91896f64444eff1dd7f189f4db05046c7f0ba08c0f531283c1c1aac581/nvidia_cuda_nvrtc-13.4.92-py3-none-win_amd64.whl', 46972283, '6af7ac5372920f6a7a560d0699348560fe44cb52c1d722d6afd3119f8af948c4'),
    'nvjitlink': ('nvjitlink.zip', 'https://files.pythonhosted.org/packages/72/7c/c44bd7277afafed9de262a23baec4dc8dcf7ecbb1b9ecb228821442d1e9b/nvidia_nvjitlink-13.4.92-py3-none-win_amd64.whl', 39053950, 'b286f3a4f227a9363efdec263c7b91788cef1478d2b8a5fa8bab7f3e82ff82fd'),
    'ort': ('ort.zip', 'https://github.com/microsoft/onnxruntime/releases/download/v1.31.1/onnxruntime-win-x64-gpu_cuda13-1.31.1.zip', 309101542, '6c64508de9c04f9d938297287e9d81fa47a2fbbb30cc04e6fac0db88f6cb68c1'),
    'trt': ('trt.zip', 'https://pypi.nvidia.com/tensorrt-cu13-libs/tensorrt_cu13_libs-10.15.1.29-py2.py3-none-win_amd64.whl', 1906663644, '930cf4917b7a3b8174400c1cf9a2d40f81f0c97a844077b18ed308930806373d'),
    'cudnn': ('cudnn.zip', 'https://files.pythonhosted.org/packages/87/6a/e55ff0ac26a5c6e2b21f41c9d04ad096b4ed6da593fba7e25845c61b0532/nvidia_cudnn_cu13-9.27.0.42-py3-none-win_amd64.whl', 436469905, '7d96f634adafd55c72231eb0500ca77ab109ec8ebff7b33000b76e081bc4558e'),
    'cublas': ('cublas.zip', 'https://files.pythonhosted.org/packages/f3/91/bda39230b7a4c102bb724c21b50823efc63544490a885fea0b604911c0ab/nvidia_cublas-13.0.2.14-py3-none-win_amd64.whl', 401887280, '5f7a90456b1392db60e28719547ba6e00696bfa81304b6887cf287f9edf2c1d7'),
    'cudart': ('cudart.whl', 'https://files.pythonhosted.org/packages/86/00/d5436004268f049214193659ebc36550b5ef3925c3d13b4cc980e13be6f5/nvidia_cuda_runtime-13.4.92-py3-none-win_amd64.whl', 2778543, '08dca5e4aba480c2fd5b55075c0fa71b84ef9dcf0521f2d58baa14a803a7311c'),
}
LINUX_PINS = {
    'nvrtc-linux': ('nvrtc-linux.zip', 'https://files.pythonhosted.org/packages/56/9c/1342ebb460ce2afd014ec5a002adc99108e3c53a6b70f399cf64dd1f267d/nvidia_cuda_nvrtc-13.4.92-py3-none-manylinux2010_x86_64.manylinux_2_12_x86_64.whl', 53301733, '5ce8c97b00b232c4f50c8c4b5a3b68cafee08bdb82ea86f2052ff01d03194f4a'),
    'nvjitlink-linux': ('nvjitlink-linux.zip', 'https://files.pythonhosted.org/packages/1d/6b/eef7a9e32872b8f41e145bf10cddc9af26e153c338852811fe9a9baddf9e/nvidia_nvjitlink-13.4.92-py3-none-manylinux2010_x86_64.manylinux_2_12_x86_64.whl', 42452378, 'e0391f24ed94ec879b84e3da4d4ec320c879aff681f2c7a638462f7199284323'),
    'curand-linux': ('curand-linux.zip', 'https://files.pythonhosted.org/packages/07/73/3ee8e5b4cb891401e603ffd3a59b35c6afe785fd2de123afe7c7029603dc/nvidia_curand-10.4.4.72-py3-none-manylinux_2_27_x86_64.whl', 61498332, '25c3457ae7a224fdd484dab90b0fc5dc0e842fab5db3012afa4a5bd2af4eb7e5'),
    'ort-linux': ('ort-linux.zip', 'https://files.pythonhosted.org/packages/22/97/9531b4d9e566cf2a0f04b4b5c26a3bc0ef2e2e0b6bc943a02c0eead98cf7/onnxruntime_gpu-1.31.1-cp312-cp312-manylinux_2_28_x86_64.whl', 255569273, '05d548956197b9d01f59b625de740e0d6be3593f874e241a12dc0362650ecdf7'),
    'trt-linux': ('trt-linux.zip', 'https://pypi.nvidia.com/tensorrt-cu13-libs/tensorrt_cu13_libs-10.15.1.29-py2.py3-none-manylinux_2_28_x86_64.whl', 3713901680, '0619b60ba97425909c18442eb36cbe7f99b1a7eeb4ebf9c6b278e6e2c5ac6c3b'),
    'cublas-linux': ('cublas-linux.zip', 'https://files.pythonhosted.org/packages/28/d4/30e630055d8e0f7058cc0d39e562c965d692f4735537d3f02f45d579a280/nvidia_cublas-13.0.2.14-py3-none-manylinux_2_27_x86_64.whl', 420882912, '0cf238ffdbe46c00cb9aa98e7bca745c223a5ca5a53e686ac8b3e5a08fe80d6c'),
    'cudart-linux': ('cudart-linux.zip', 'https://files.pythonhosted.org/packages/98/8a/3431271f6344874b8f1ac03f16b3d679c91493f8da63f716160403e6d0a0/nvidia_cuda_runtime-13.4.92-py3-none-manylinux2014_x86_64.manylinux_2_17_x86_64.whl', 2494438, '9641f797da20ce1dd8e779b6e96d08cf9ba564cec8e8225458811ee26423f3a5'),
    'cudnn-linux': ('cudnn-linux.zip', 'https://files.pythonhosted.org/packages/af/75/96ea5c5368eb595c39d629cde08a66227a864e71ccb0e593add2612bb952/nvidia_cudnn_cu13-9.27.0.42-py3-none-manylinux_2_27_x86_64.whl', 536771498, '9677e76f21862eb5da7ee5ed69d544738b2d8b5c3ce7e5ec125c5592e6cdbdc8'),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def bundle(cache, pins, platform):
    licenses, files, artifacts = [], [], []
    for key, (filename, url, size, checksum) in pins.items():
        path = cache / filename
        with path.open('rb') as stream:
            assert path.stat().st_size == size and hashlib.file_digest(stream, 'sha256').hexdigest() == checksum, key
        artifacts.append(dict(id=key, url=url, bytes=size, sha256=checksum, delivery='vendor-direct'))
        with zipfile.ZipFile(path) as archive:
            for member in archive.infolist():
                basename = Path(member.filename).name
                legal = basename.lower() in ('license', 'license.txt', 'thirdpartynotices.txt', 'acknowledgements.txt')
                legal = legal or (key == 'rtx-sdk' and basename == 'README.txt')
                native = (basename.endswith('.dll') or '.so' in basename) and 'nvblas' not in basename
                native = native and not basename.startswith('onnxruntime_pybind') and '_win_' not in basename
                if not legal and not native:
                    continue
                with archive.open(member) as stream:
                    checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
                target = f'licenses/{key}/{basename}' if legal else basename
                files.append(dict(artifact=key, member=member.filename, path=target,
                                  bytes=member.file_size, sha256=checksum))
                if legal:
                    text = archive.read(member).decode('utf-8-sig')
                    # W04 bounds each text to 256 KiB. Split long notices at newlines;
                    # all chunks are displayed and accepted, with no omitted text.
                    chunks, chunk = [], ''
                    for line in text.splitlines(keepends=True):
                        if len((chunk + line).encode()) > 200000:
                            chunks.append(chunk)
                            chunk = ''
                        chunk += line
                    if chunk:
                        chunks.append(chunk)
                    assert ''.join(chunks) == text
                    for n, chunk in enumerate(chunks):
                        licenses.append(dict(id=f'{key}-{basename.lower()}-{n}', text=chunk, text_sha256=sha(chunk.encode()),
                                             source_url=url, attribution='Microsoft Corporation' if key.startswith('ort') else 'NVIDIA Corporation',
                                             scope=f'{key} original vendor licence/notices ({n + 1}/{len(chunks)})'))
    acceptance = [dict(id=l['id'], text_sha256=l['text_sha256']) for l in licenses]
    windows = platform == 'windows-x64'
    common = dict(schema=1, registration='classic', runtime='onnxruntime.dll' if windows else 'libonnxruntime.so.1.31.1', runtime_build='1.31.1', provider_build='1.31.1')
    preload = (['cudart64_13.dll', 'onnxruntime_providers_shared.dll', 'nvrtc-builtins64_134.dll', 'nvrtc64_130_0.dll', 'nvJitLink_130_0.dll', 'cublasLt64_13.dll', 'cublas64_13.dll', 'cudnn64_9.dll'] if windows else
               ['libcudart.so.13', 'libonnxruntime_providers_shared.so', 'libnvrtc-builtins.so.13.4', 'libnvrtc.so.13', 'libnvJitLink.so.13', 'libcublasLt.so.13', 'libcublas.so.13', 'libcurand.so.10', 'libcudnn.so.9'])
    descriptors = [dict(common, filename='cuda.json', backend='cuda', provider_name='CUDAExecutionProvider',
                        provider='onnxruntime_providers_cuda.dll' if windows else 'libonnxruntime_providers_cuda.so', preload=preload),
                   dict(common, filename='tensorrt.json', backend='tensorrt', provider_name='TensorrtExecutionProvider',
                        provider='onnxruntime_providers_tensorrt.dll' if windows else 'libonnxruntime_providers_tensorrt.so',
                        preload=preload + (['nvinfer_10.dll', 'nvinfer_plugin_10.dll', 'nvonnxparser_10.dll'] if windows else
                                           ['libnvinfer.so.10', 'libnvinfer_plugin.so.10', 'libnvonnxparser.so.10']))]
    recipe = dict(enabled=True, reason='', platform=platform,
                  display_name='NVIDIA TensorRT 10 and CUDA 13 runtime (optional)' + ('' if windows else ' — Linux, hardware validation pending'),
                  artifacts=artifacts, licenses=acceptance, files=files, descriptors=descriptors,
                  expanded_bytes=sum(f['bytes'] for f in files))
    print(platform, f"{sum(a['bytes'] for a in artifacts):,} bytes download; {recipe['expanded_bytes']:,} bytes installed")
    return recipe, licenses


def generate(cache, output):
    windows, win_licenses = bundle(cache, PINS, 'windows-x64')
    linux, linux_licenses = bundle(cache, LINUX_PINS, 'linux-x86_64')
    rtx_pins = {'ort': PINS['ort'], 'cudart': PINS['cudart'],
        'rtx': ('rtx.zip', 'https://files.pythonhosted.org/packages/ef/64/6a704b972f2c6806b3a13abce3e0d827deb0e4192e7f4240ffdafa8d099c/onnxruntime_ep_nv_tensorrt_rtx_cu13-0.5.0-py3-none-win_amd64.whl', 110875664, '609b19f7236c8522ef005a9f763003a78f492e5d871afad39fe1b247760eb3c7'),
        'rtx-sdk': ('rtx-sdk.zip', 'https://developer.nvidia.com/downloads/trt/rtx_sdk/secure/1.7/tensorrt-rtx-1.7.1.116-windows-amd64-cuda-13.4-release-external.zip', 99783796, '2002294a19f81135e4cfd074faddf1f842eb46979f36f7e6db57ff83016ba71d')}
    rtx, rtx_licenses = bundle(cache, rtx_pins, 'windows-x64')
    # The SDK supplies matching acknowledgements. Runtime DLLs come from the EP
    # wheel, except cudart, which comes from its separately licensed CUDA wheel.
    rtx['files'] = [f for f in rtx['files'] if
        (f['artifact'] == 'ort' and (f['path'] in ['onnxruntime.dll', 'onnxruntime_providers_shared.dll'] or f['path'].startswith('licenses/'))) or
        f['artifact'] == 'cudart' or
        (f['artifact'] == 'rtx' and f['path'] != 'cudart64_13.dll') or
        (f['artifact'] == 'rtx-sdk' and f['path'].startswith('licenses/'))]
    rtx['descriptors'] = [dict(schema=1, filename='rtx.json', backend='tensorrt-rtx', registration='plugin',
        runtime='onnxruntime.dll', runtime_build='1.31.1', provider_name='NvTensorRTRTXExecutionProvider',
        provider_build='0.5.0', provider='onnxruntime_providers_nv_tensorrt_rtx.dll',
        preload=['cudart64_13.dll', 'onnxruntime_providers_shared.dll', 'tensorrt_rtx_1_7.dll', 'tensorrt_onnxparser_rtx_1_7.dll'])]
    rtx['display_name'] = 'NVIDIA TensorRT-RTX 0.5.0 / RTX 1.7 (optional)'
    rtx['extra_files'] = []
    for filename, source, expected in [
        ('rtx-sla.txt', 'https://docs.nvidia.com/deeplearning/tensorrt-rtx/latest/reference/sla.html', 'd3fd540b15d7d1689adaa92c5c1d9bc7c4b57d452b80528de1c3fb65a8ea88cb'),
        ('rtx-ep-LICENSE', 'https://raw.githubusercontent.com/NVIDIA/TensorRT-RTX-EP-ABI/v0.5.0/LICENSE', '0ac75ba7b6af62309a2b5eeb4d10bd0b264d198075a51826db8ec3a7053d4115')]:
        text = (cache / filename).read_text(encoding='utf-8')
        assert sha(text.encode()) == expected, filename
        record = dict(id=filename.lower(), text=text, text_sha256=expected, source_url=source,
                      attribution='NVIDIA Corporation', scope='TensorRT-RTX runtime' if filename == 'rtx-sla.txt' else 'TensorRT-RTX execution provider (Apache-2.0 and MIT notices)')
        rtx_licenses.append(record)
        rtx['licenses'].append(dict(id=record['id'], text_sha256=expected))
        rtx['extra_files'].append(dict(path=f'licenses/rtx/{filename}', text=text, sha256=expected))
    rtx['expanded_bytes'] = sum(f['bytes'] for f in rtx['files']) + sum(len(f['text'].encode()) for f in rtx['extra_files'])
    rtx['licenses'].sort(key=lambda record: (0 if record['id'] == 'rtx-sla.txt' else 1 if record['id'].startswith('rtx') else 2 if record['id'].startswith('cudart') else 3))
    print('RTX final', sum(a['bytes'] for a in rtx['artifacts']), rtx['expanded_bytes'])
    registry = {}
    for record in win_licenses + linux_licenses + rtx_licenses:
        assert record['id'] not in registry or registry[record['id']]['text_sha256'] == record['text_sha256']
        registry[record['id']] = record
    catalogue = dict(_spdx='SPDX-License-Identifier: GPL-3.0-or-later', schema_version=1,
                     licenses=dict(schema_version=1, licenses=list(registry.values())), bundles={
        'nvidia-ort1.31.1-trt10.15-cu13': windows,
        'nvidia-linux-ort1.31.1-trt10.15-cu13': linux,
        'nvidia-rtx-0.5.0': rtx,
        'migraphx-linux': dict(enabled=False, platform='linux-x86_64', reason='MIGraphX bundle pins and the matching ROCm dependencies have not been validated. AMD Linux acceleration is not included yet.')})
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(catalogue, indent=2, ensure_ascii=True) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'data/runtimes/runtime-recipes-v1.json')
    args = parser.parse_args()
    generate(args.cache, args.output)
