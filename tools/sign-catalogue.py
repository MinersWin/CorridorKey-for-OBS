# SPDX-License-Identifier: GPL-3.0-or-later
"""Sign exact catalogue bytes, or verify them without accessing a private key."""
import argparse
import json
import os
from pathlib import Path
import re
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

ROOT = Path(__file__).resolve().parents[1]


def public_key(path):
    text = path.read_text(encoding='ascii').strip()
    if not re.fullmatch(r'[0-9a-f]{128}', text):
        raise ValueError('Invalid public point')
    return ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), b'\x04' + bytes.fromhex(text))


def verify(payload, signature, trusted):
    if not re.fullmatch(r'[0-9a-f]{128}', signature):
        raise ValueError('Invalid raw signature')
    raw = bytes.fromhex(signature)
    der = utils.encode_dss_signature(int.from_bytes(raw[:32], 'big'), int.from_bytes(raw[32:], 'big'))
    trusted.verify(der, payload, ec.ECDSA(hashes.SHA256()))


def signature_json(pairs):
    if len(pairs) != 1 or pairs[0][0] != 'signature' or not isinstance(pairs[0][1], str):
        raise ValueError('Invalid signature envelope')
    return dict(pairs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalogue', type=Path, default=ROOT / 'data/models/catalogue-v1.json')
    parser.add_argument('--public-key', type=Path, default=ROOT / 'packaging/catalogue-public-key.txt')
    parser.add_argument('--signature', type=Path, default=Path('catalogue-signature.json'))
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    try:
        payload = args.catalogue.read_bytes()
        trusted = public_key(args.public_key)
        if args.verify_only:
            envelope = json.loads(args.signature.read_text(encoding='ascii'), object_pairs_hook=signature_json)
            if not isinstance(envelope, dict):
                raise ValueError('Invalid signature envelope')
            verify(payload, envelope['signature'], trusted)
        else:
            # Pop before any child process could inherit it. Never include PEM/parser errors in diagnostics.
            pem = os.environ.pop('CATALOGUE_SIGNING_KEY', '')
            if not pem.startswith('-----BEGIN PRIVATE KEY-----'):
                raise ValueError('Expected PKCS#8 PEM')
            private = serialization.load_pem_private_key(pem.encode('ascii'), password=None)
            del pem
            if not isinstance(private, ec.EllipticCurvePrivateKey) or not isinstance(private.curve, ec.SECP256R1):
                raise ValueError('Expected P-256')
            if private.public_key().public_numbers() != trusted.public_numbers():
                raise ValueError('Private key does not match committed public point')
            der = private.sign(payload, ec.ECDSA(hashes.SHA256()))
            del private
            r, s = utils.decode_dss_signature(der)
            signature = (r.to_bytes(32, 'big') + s.to_bytes(32, 'big')).hex()
            verify(payload, signature, trusted)
            args.signature.write_text(json.dumps({'signature': signature}) + '\n', encoding='ascii')
    except Exception:
        # Some serialization exceptions contain input details. Keep all failure output independent of the key.
        print('Catalogue signing/verification failed; check the public key, input files and signing configuration.',
              file=sys.stderr)
        return 1
    print('Catalogue signature verified.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
