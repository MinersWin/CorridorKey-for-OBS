# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the compiled catalogue trust metadata before completing a release stage."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import shutil

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('models_dir', type=Path)
    parser.add_argument('--public-key', type=Path, default=ROOT / 'packaging/catalogue-public-key.txt')
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--signed-dir', type=Path)
    mode.add_argument('--unsigned-test', action='store_true')
    args = parser.parse_args()
    catalogue = args.models_dir / 'catalogue-v1.json'
    compiled = json.loads((args.models_dir / 'catalogue-build.json').read_text(encoding='utf-8'))
    payload = catalogue.read_bytes()
    if compiled['catalogue_sha256'] != hashlib.sha256(payload).hexdigest():
        raise ValueError('Staged catalogue differs from compiled digest')
    if payload != (ROOT / 'data/models/catalogue-v1.json').read_bytes():
        raise ValueError('Staged catalogue differs from release checkout')
    signature = args.models_dir / 'catalogue-signature.json'
    if args.signed_dir:
        if compiled['public_key'] != args.public_key.read_text().strip():
            raise ValueError('Build did not embed the release public key')
        if payload != (args.signed_dir / 'catalogue-v1.json').read_bytes():
            raise ValueError('Staged catalogue differs from signed catalogue')
        signer = runpy.run_path(str(ROOT / 'tools/sign-catalogue.py'))
        envelope = json.loads((args.signed_dir / 'catalogue-signature.json').read_text(encoding='ascii'),
                              object_pairs_hook=signer['signature_json'])
        signer['verify'](payload, envelope['signature'], signer['public_key'](args.public_key))
        shutil.copyfile(args.signed_dir / 'catalogue-signature.json', signature)
    else:
        if compiled['public_key'] or signature.exists():
            raise ValueError('Unsigned test stage must use the compiled digest path without a signature')
        (args.models_dir.parent / 'UNSIGNED-TEST.txt').write_text(
            'Unsigned test build. The bundled model catalogue uses the digest pinned in the plugin.\n'
            'This artifact is for testing and is not a production release.\n', encoding='utf-8')


if __name__ == '__main__':
    main()
