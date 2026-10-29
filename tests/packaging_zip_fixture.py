# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent vendor-shaped ZIP fixtures for the native runtime extractor."""
import os
from pathlib import Path
import subprocess
import sys
import warnings
import zipfile
import random

root = Path(sys.argv[2]).resolve()
root.mkdir(parents=True, exist_ok=True)
payload = b'x' * 100000
with zipfile.ZipFile(root / 'valid.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    archive.writestr('vendor/bin/library.dll', payload)
    archive.writestr('../escape.dll', b'archive paths must not become destinations')
    archive.writestr('large.dll', random.Random(41).randbytes(2 * 1024 * 1024))
with warnings.catch_warnings():
    warnings.simplefilter('ignore', UserWarning)
    with zipfile.ZipFile(root / 'duplicate.zip', 'w') as archive:
        archive.writestr('vendor/bin/library.dll', payload)
        archive.writestr('vendor/bin/library.dll', payload)
data = bytearray((root / 'valid.zip').read_bytes())
central = data.index(b'PK\x01\x02')
data[central + 16] ^= 1  # Broken central-directory CRC; compressed data stays valid.
(root / 'corrupt.zip').write_bytes(data)
env = dict(os.environ, CK_RUNTIME_ZIP_FIXTURE=str(root))
subprocess.run([sys.argv[1]], check=True, env=env)
