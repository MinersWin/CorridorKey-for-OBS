# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the release signer and native verifier using keys generated in memory."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parents[2]
VERIFIER = Path(sys.argv.pop(1)).resolve()
BUILD = Path(sys.argv.pop(1)).resolve()


class CatalogueSigning(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='catalogue-signing-', dir=BUILD)
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.catalogue = self.directory / 'catalogue-v1.json'
        self.catalogue.write_bytes((ROOT / 'data/models/catalogue-v1.json').read_bytes())
        self.signature = self.directory / 'catalogue-signature.json'
        self.public = self.directory / 'public.txt'
        self.key = ec.generate_private_key(ec.SECP256R1())
        point = self.key.public_key().public_bytes(serialization.Encoding.X962,
                                                  serialization.PublicFormat.UncompressedPoint)
        self.public.write_text(point[1:].hex() + '\n', encoding='ascii')
        # The test always replaces this variable with a fresh throwaway key.
        self.env = dict(os.environ)
        self.env['CATALOGUE_SIGNING_KEY'] = self.pem(self.key)

    @staticmethod
    def pem(key, format=serialization.PrivateFormat.PKCS8):
        return key.private_bytes(serialization.Encoding.PEM, format,
                                 serialization.NoEncryption()).decode('ascii')

    def sign(self, *extra, success=True):
        result = subprocess.run([sys.executable, str(ROOT / 'tools/sign-catalogue.py'),
                                 '--catalogue', str(self.catalogue), '--public-key', str(self.public),
                                 '--signature', str(self.signature), *extra],
                                env=self.env, capture_output=True, text=True, timeout=15)
        self.assertTrue(self.env.get('CATALOGUE_SIGNING_KEY', 'never-echo-this') not in result.stdout + result.stderr,
                        'Signer disclosed key material')
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def native(self, success=True):
        result = subprocess.run([str(VERIFIER), str(self.catalogue), str(self.signature), str(self.public)],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)

    def test_exact_bytes(self):
        self.sign()
        envelope = json.loads(self.signature.read_text())
        self.assertEqual(set(envelope), {'signature'})
        self.assertRegex(envelope['signature'], r'^[0-9a-f]{128}$')
        self.native()
        # Verification mode needs no private key, even if an invalid value is present.
        self.env['CATALOGUE_SIGNING_KEY'] = 'invalid key that must not be parsed or printed'
        self.sign('--verify-only')

    def test_one_byte_tamper(self):
        self.sign()
        raw = bytearray(self.catalogue.read_bytes())
        index = raw.index(b'CorridorKey')
        raw[index] = ord('c')
        self.catalogue.write_bytes(raw)
        self.native(success=False)
        self.sign('--verify-only', success=False)

    def test_whitespace_tamper(self):
        self.sign()
        self.catalogue.write_bytes(self.catalogue.read_bytes() + b'\n')
        self.native(success=False)

    def test_signature_tamper(self):
        self.sign()
        envelope = json.loads(self.signature.read_text())
        raw = bytearray.fromhex(envelope['signature'])
        raw[0] ^= 1
        self.signature.write_text(json.dumps({'signature': raw.hex()}))
        self.native(success=False)
        self.sign('--verify-only', success=False)

    def test_other_public_key(self):
        self.env['CATALOGUE_SIGNING_KEY'] = self.pem(ec.generate_private_key(ec.SECP256R1()))
        self.sign(success=False)
        self.assertFalse(self.signature.exists())

    def test_production_public_key_rejects_test_key(self):
        self.public.write_bytes((ROOT / 'packaging/catalogue-public-key.txt').read_bytes())
        self.sign(success=False)
        self.assertFalse(self.signature.exists())

    def test_missing_key(self):
        del self.env['CATALOGUE_SIGNING_KEY']
        self.sign(success=False)
        self.assertFalse(self.signature.exists())

    def test_invalid_key(self):
        self.env['CATALOGUE_SIGNING_KEY'] = '-----BEGIN PRIVATE KEY-----\nthrowaway-invalid-key\n'
        self.sign(success=False)
        self.assertFalse(self.signature.exists())

    def test_other_curve(self):
        self.env['CATALOGUE_SIGNING_KEY'] = self.pem(ec.generate_private_key(ec.SECP384R1()))
        self.sign(success=False)
        self.assertFalse(self.signature.exists())

    def test_sec1_key_rejected(self):
        self.env['CATALOGUE_SIGNING_KEY'] = self.pem(self.key, serialization.PrivateFormat.TraditionalOpenSSL)
        self.sign(success=False)
        self.assertFalse(self.signature.exists())

    def test_envelope_fields(self):
        self.sign()
        envelope = json.loads(self.signature.read_text())
        envelope['public_xy'] = self.public.read_text().strip()
        self.signature.write_text(json.dumps(envelope))
        self.native(success=False)
        self.sign('--verify-only', success=False)

    def stage(self, signed=True, success=True, public=None, digest=None):
        models = self.directory / 'stage/data/models'
        models.mkdir(parents=True, exist_ok=True)
        (models / 'catalogue-v1.json').write_bytes(self.catalogue.read_bytes())
        (models / 'catalogue-build.json').write_text(json.dumps({
            'public_key': public if public is not None else (self.public.read_text().strip() if signed else ''),
            'catalogue_sha256': digest if digest is not None else hashlib.sha256(self.catalogue.read_bytes()).hexdigest()
        }))
        mode = ['--signed-dir', str(self.directory)] if signed else ['--unsigned-test']
        result = subprocess.run([sys.executable, str(ROOT / 'tools/stage-catalogue.py'), str(models),
                                 '--public-key', str(self.public), *mode],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return models

    def test_signed_stage(self):
        self.sign()
        models = self.stage()
        self.assertEqual((models / 'catalogue-signature.json').read_bytes(), self.signature.read_bytes())
        self.assertFalse((models.parent / 'UNSIGNED-TEST.txt').exists())

    def test_unsigned_stage(self):
        models = self.stage(signed=False)
        self.assertTrue((models.parent / 'UNSIGNED-TEST.txt').is_file())
        self.assertFalse((models / 'catalogue-signature.json').exists())

    def test_stage_wrong_compiled_key(self):
        self.sign()
        self.stage(public='', success=False)

    def test_stage_wrong_compiled_digest(self):
        self.stage(signed=False, digest='0' * 64, success=False)

    def test_signed_build_cannot_be_marked_unsigned(self):
        self.stage(signed=False, public=self.public.read_text().strip(), success=False)

    def test_stage_signature_mismatch(self):
        self.sign()
        self.signature.write_text(json.dumps({'signature': '0' * 128}))
        self.stage(success=False)

    def test_stage_missing_signature(self):
        self.stage(success=False)


if __name__ == '__main__':
    unittest.main()
