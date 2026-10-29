# SPDX-License-Identifier: GPL-3.0-or-later
"""Production shader checks for ties, padding, borders, ROI and neighbour feathering."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import numpy as np


def run(args):
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PATH"] = str(args.obs / "bin/64bit") + os.pathsep + env["PATH"]
    env["OBS_TEST_DATA"] = (args.obs / "data/libobs").as_posix() + "/"
    env["OBS_TEST_GRAPHICS"] = str(args.obs / f"bin/64bit/libobs-{args.renderer}.dll")
    evidence = []
    for h, w, crop in [(1, 1, None), (65, 97, None), (577, 769, None), (65, 97, (8, 12, 88, 60))]:
        folder = root / f"{w}x{h}{'-crop' if crop else ''}"
        folder.mkdir(exist_ok=True)
        y, x = np.mgrid[:h, :w]
        native = np.stack([x / max(w, 1), y / max(h, 1), np.full_like(x, .2, dtype=np.float32), np.ones_like(x)], -1).astype('f4')
        guide = np.broadcast_to(np.array([.2, .7, .1, 1], 'f4'), (17, 23, 4)).copy()
        model = np.broadcast_to(np.array([.3, .5, .8, .5], 'f4'), (17, 23, 4)).copy()
        for name, array in [('native', native), ('guide', guide), ('model', model)]:
            np.save(folder / f"{name}.npy", array)
        item = {k: str(folder / f"{k}.npy") for k in ['native', 'guide', 'model']}
        item.update(output=str(folder / 'base'), refiner_budget=256, despill=0., contrast=1.)
        left, top, right, bottom = crop or (0, 0, w, h)
        if crop:
            item['roi'] = [left / w, top / h, right / w, bottom / h]
        manifest = folder / 'manifest.json'

        def probe():
            manifest.write_text(json.dumps([item]))
            with (folder / f"{Path(item['output']).name}.log").open('w') as log:
                subprocess.run([str(args.probe), 'unused', str(args.data), '1920', 'offline', str(manifest)],
                               env=env, stdout=log, stderr=log, check=True)

        probe()
        base = np.fromfile(folder / 'base.raw.f32', 'f4').reshape(h, w, 4)
        th, tw = (bottom - top + 31) // 32, (right - left + 31) // 32
        n = min(256, th * tw)
        chosen = np.fromfile(folder / 'base.selection.f32', 'f4').reshape(-1, 4)[:n, 1].astype(int)
        assert np.array_equal(chosen, np.arange(n)), chosen
        inputs = np.fromfile(folder / 'base.x.f32', 'f4').reshape(256, 11, 52, 52)
        for slot in range(256):
            index = slot if slot < n else 0
            yy = np.clip(index // tw * 32 + np.arange(52) - 10, 0, bottom - top - 1) + top
            xx = np.clip(index % tw * 32 + np.arange(52) - 10, 0, right - left - 1) + left
            expected = native[yy[:, None], xx[None, :], :3].transpose(2, 0, 1).astype('f2').astype('f4')
            assert np.array_equal(inputs[slot, :3], expected)
        # Every padded output has a different value; no duplicate slot may overwrite a valid tile.
        centres = np.zeros((512, 512, 4), 'f4')
        for slot in range(256):
            centres[slot // 16 * 32:(slot // 16 + 1) * 32, slot % 16 * 32:(slot % 16 + 1) * 32] = [1, 0, 1, .8] if slot < n else [0, 1, 0, 0]
        centres.tofile(folder / 'centres.f32')
        item.update(output=str(folder / 'blend'), refiner_centres=str(folder / 'centres.f32'))
        probe()
        actual = np.fromfile(folder / 'blend.raw.f32', 'f4').reshape(h, w, 4)
        expected = base.copy()
        mask = (np.arange(th * tw).reshape(th, tw) < n).astype('f4')
        for yy in range(top, bottom):
            for xx in range(left, right):
                cy, cx = (yy-top)//32, (xx-left)//32
                if not mask[cy, cx]:
                    continue
                sy, sx = (yy-top+.5)/32-.5, (xx-left+.5)/32-.5
                iy, ix = int(np.floor(sy)), int(np.floor(sx))
                wy, wx = sy-iy, sx-ix
                sample = lambda dy, dx: mask[np.clip(iy+dy, 0, th-1), np.clip(ix+dx, 0, tw-1)]
                selected = (1-wy)*((1-wx)*sample(0,0)+wx*sample(0,1))+wy*((1-wx)*sample(1,0)+wx*sample(1,1))
                feather = np.clip(2*selected-1,0,1)
                expected[yy,xx] = feather*np.array([1,0,1,.8])+(1-feather)*base[yy,xx]
        error = float(np.abs(actual-expected).max())
        assert error < 2e-5, error
        evidence.append(dict(width=w,height=h,crop=crop,valid_tiles=n,blend_max=error))
        print(evidence[-1], flush=True)
    (root/'results.json').write_text(json.dumps(evidence,indent=2))


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--probe',type=Path,required=True)
    p.add_argument('--data',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--obs',type=Path,default=Path('C:/Program Files/obs-studio'))
    p.add_argument('--renderer',choices=['d3d11','opengl'],default='d3d11')
    run(p.parse_args())
