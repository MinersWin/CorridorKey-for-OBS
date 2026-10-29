# SPDX-License-Identifier: GPL-3.0-or-later
"""Check package inventories against ZIP contents and reject learned artifacts."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import zipfile

spec = importlib.util.spec_from_file_location('package', Path(__file__).resolve().parents[1] / 'packaging/package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
with tempfile.TemporaryDirectory(dir=sys.argv[1]) as temporary:
    root = Path(temporary)
    stage = root / 'stage'
    data = stage / 'corridorkey/data'
    (data / 'tools').mkdir(parents=True)
    (stage / 'corridorkey/corridorkey.dll').write_bytes(b'independent fake module')
    (data / 'tools/corridorkey_backend_probe.exe').write_bytes(b'independent fake helper')
    (data / 'packaging-build.json').write_text(json.dumps(dict(zlib='test', nlohmann_json='test', qt='test')))
    package.inventory(stage, 'windows-x64', {})
    first = (data / 'BUNDLE_CONTENTS.json').read_bytes()
    package.inventory(stage, 'windows-x64', {})
    assert (data / 'BUNDLE_CONTENTS.json').read_bytes() == first, 'Inventory must be stable on rerun'
    out = root / 'out'
    out.mkdir()
    package.windows(stage, out, 'test')
    for layout in ['modern', 'legacy']:
        with zipfile.ZipFile(out / f'corridorkey-test-windows-x64-{layout}.zip') as archive:
            inventory = json.loads(archive.read('corridorkey/data/BUNDLE_CONTENTS.json'))
            for entry in inventory['files']:
                payload = archive.read(entry['path'])
                assert len(payload) == entry['bytes']
                import hashlib
                assert hashlib.sha256(payload).hexdigest() == entry['sha256']
    for name in ['model.onnx', 'model.weights', 'model.safetensors', 'cached.engine']:
        path = data / name
        path.write_bytes(b'forbidden package fixture')
        try:
            package.inventory(stage, 'windows-x64', {})
        except ValueError:
            pass
        else:
            raise AssertionError(f'Package admitted {name}')
        path.unlink()
print('Package inventory, both Windows layouts, rerun stability and model exclusion passed')
