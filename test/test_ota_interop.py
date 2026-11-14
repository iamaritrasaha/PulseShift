"""
Cross-Language Cryptographic Interoperability and Fault-Injection Tests for PulseShift OTA
Copyright (c) 2026 Aritra Saha. All rights reserved.

Verifies:
1. Exact struct packing and layout agreement between Python (<4sBBIH64s) and C++ (OtaAuthTrailer).
2. End-to-end signing and verification using tools/sign_firmware.py.
3. Prehashed SHA-256 ECDSA P-256 signature scheme consistency.
4. Fault injection: tampered payload, tampered CRC, corrupted signature, wrong key, bad magic, bad version, bad sigLen.
5. Exact cryptographic agreement between C++ embedded test vectors and Python cryptography library.
"""

import hashlib
import os
import re
import struct
import sys
import tempfile
import unittest
import zlib

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

import sign_firmware


class OtaCrossLanguageInteropTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        # Generate an ephemeral NIST P-256 keypair for isolated testing
        self.priv_key = ec.generate_private_key(ec.SECP256R1())
        self.priv_pem_path = os.path.join(self.temp_dir.name, "test_key.pem")
        with open(self.priv_pem_path, "wb") as f:
            f.write(self.priv_key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.PKCS8,
                encryption_algorithm=serialization.NoEncryption()
            ))

        self.pub_key = self.priv_key.public_key()
        self.pub_pem_path = os.path.join(self.temp_dir.name, "test_pub.pem")
        with open(self.pub_pem_path, "wb") as f:
            f.write(self.pub_key.public_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PublicFormat.SubjectPublicKeyInfo
            ))

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_trailer_wire_format_and_offsets(self):
        """Verify that Python TRAILER_STRUCT_FORMAT exactly matches C++ OtaAuthTrailer layout."""
        self.assertEqual(sign_firmware.TRAILER_STRUCT_FORMAT, "<4sBBIH64s")
        self.assertEqual(sign_firmware.TRAILER_SIZE, 76)
        self.assertEqual(struct.calcsize(sign_firmware.TRAILER_STRUCT_FORMAT), 76)

        dummy_sig = b"\xAA" * 64
        packed = struct.pack(sign_firmware.TRAILER_STRUCT_FORMAT,
                             b"PSOT", 1, 0, 0x12345678, 64, dummy_sig)
        self.assertEqual(len(packed), 76)

        # Field offset assertions matching static_assert in ota_manager.h
        self.assertEqual(packed[0:4], b"PSOT")        # offset 0..3: magic
        self.assertEqual(packed[4], 1)               # offset 4: version
        self.assertEqual(packed[5], 0)               # offset 5: flags
        self.assertEqual(packed[6:10], b"\x78\x56\x34\x12") # offset 6..9: appCrc32 (LE)
        self.assertEqual(packed[10:12], b"\x40\x00") # offset 10..11: sigLen = 64 (LE)
        self.assertEqual(packed[12:76], dummy_sig)   # offset 12..75: raw signature

    def test_end_to_end_signing_and_verification(self):
        """Sign a binary with sign_firmware and verify using public PEM, private PEM, and hex string."""
        raw_payload = bytes([0xE9] + [((i * 7 + 13) & 0xFF) for i in range(1, 1024)])
        input_bin = os.path.join(self.temp_dir.name, "test.bin")
        output_bin = os.path.join(self.temp_dir.name, "test.signed.bin")

        with open(input_bin, "wb") as f:
            f.write(raw_payload)

        # 1. Sign binary
        sign_firmware.sign_binary(self.priv_pem_path, input_bin, output_bin)
        self.assertTrue(os.path.exists(output_bin))
        self.assertEqual(os.path.getsize(output_bin), len(raw_payload) + 76)

        # 2. Verify with public key PEM
        sign_firmware.verify_binary(self.pub_pem_path, output_bin)

        # 3. Verify with private key PEM (auto-extracts public key)
        sign_firmware.verify_binary(self.priv_pem_path, output_bin)

        # 4. Verify with raw 130-char SEC1 hex string
        sec1_bytes = self.pub_key.public_bytes(
            encoding=serialization.Encoding.X962,
            format=serialization.PublicFormat.UncompressedPoint
        )
        hex_key = sec1_bytes.hex()
        self.assertEqual(len(hex_key), 130)
        sign_firmware.verify_binary(hex_key, output_bin)

    def test_fault_injection_invalid_esp32_header_magic(self):
        """Image without Espressif ESP_IMAGE_HEADER_MAGIC (0xE9) is rejected."""
        # 1. Test rejection of old erroneous magic 0xE7
        bad_payload_e7 = bytes([0xE7] + [0x42] * 200)
        input_bin = os.path.join(self.temp_dir.name, "bad_magic_e7.bin")
        output_bin = os.path.join(self.temp_dir.name, "bad_magic_e7.signed.bin")
        with open(input_bin, "wb") as f:
            f.write(bad_payload_e7)

        with self.assertRaises(SystemExit) as cm:
            sign_firmware.sign_binary(self.priv_pem_path, input_bin, output_bin)
        self.assertEqual(cm.exception.code, 1)

        # 2. Test rejection of 0x00 magic
        bad_payload_00 = bytes([0x00] + [0x42] * 200)
        input_bin_00 = os.path.join(self.temp_dir.name, "bad_magic_00.bin")
        output_bin_00 = os.path.join(self.temp_dir.name, "bad_magic_00.signed.bin")
        with open(input_bin_00, "wb") as f:
            f.write(bad_payload_00)

        with self.assertRaises(SystemExit) as cm:
            sign_firmware.sign_binary(self.priv_pem_path, input_bin_00, output_bin_00)
        self.assertEqual(cm.exception.code, 1)

    def test_fault_injection_tampered_payload_crc_mismatch(self):
        """Tampering with image payload causes CRC32 mismatch error."""
        raw_payload = bytes([0xE9] + [((i * 3 + 5) & 0xFF) for i in range(1, 512)])
        input_bin = os.path.join(self.temp_dir.name, "crc_test.bin")
        output_bin = os.path.join(self.temp_dir.name, "crc_test.signed.bin")

        with open(input_bin, "wb") as f:
            f.write(raw_payload)

        sign_firmware.sign_binary(self.priv_pem_path, input_bin, output_bin)

        # Flip a bit in the payload
        with open(output_bin, "r+b") as f:
            f.seek(50)
            b = f.read(1)[0]
            f.seek(50)
            f.write(bytes([b ^ 0x01]))

        # Should exit with code 2 on CRC32 mismatch
        with self.assertRaises(SystemExit) as cm:
            sign_firmware.verify_binary(self.pub_pem_path, output_bin)
        self.assertEqual(cm.exception.code, 2)

    def test_fault_injection_tampered_signature(self):
        """Tampering with signature in trailer causes cryptographic verification failure."""
        raw_payload = bytes([0xE9] + [((i * 11 + 3) & 0xFF) for i in range(1, 512)])
        input_bin = os.path.join(self.temp_dir.name, "sig_test.bin")
        output_bin = os.path.join(self.temp_dir.name, "sig_test.signed.bin")

        with open(input_bin, "wb") as f:
            f.write(raw_payload)

        sign_firmware.sign_binary(self.priv_pem_path, input_bin, output_bin)

        # Corrupt a byte in the signature (within last 64 bytes)
        with open(output_bin, "r+b") as f:
            f.seek(-10, os.SEEK_END)
            b = f.read(1)[0]
            f.seek(-10, os.SEEK_END)
            f.write(bytes([b ^ 0xFF]))

        with self.assertRaises(SystemExit) as cm:
            sign_firmware.verify_binary(self.pub_pem_path, output_bin)
        self.assertEqual(cm.exception.code, 3)

    def test_fault_injection_wrong_key(self):
        """Verification with an untrusted / different public key fails."""
        raw_payload = bytes([0xE9] + [0x42] * 200)
        input_bin = os.path.join(self.temp_dir.name, "wrong_key.bin")
        output_bin = os.path.join(self.temp_dir.name, "wrong_key.signed.bin")

        with open(input_bin, "wb") as f:
            f.write(raw_payload)

        sign_firmware.sign_binary(self.priv_pem_path, input_bin, output_bin)

        # Generate a second, different key
        other_key = ec.generate_private_key(ec.SECP256R1())
        other_pub_pem = os.path.join(self.temp_dir.name, "other_pub.pem")
        with open(other_pub_pem, "wb") as f:
            f.write(other_key.public_key().public_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PublicFormat.SubjectPublicKeyInfo
            ))

        with self.assertRaises(SystemExit) as cm:
            sign_firmware.verify_binary(other_pub_pem, output_bin)
        self.assertEqual(cm.exception.code, 3)

    def test_fault_injection_bad_trailer_magic(self):
        """Invalid trailer magic is rejected."""
        raw_payload = bytes([0xE9] + [0x11] * 100)
        input_bin = os.path.join(self.temp_dir.name, "bad_magic.bin")
        output_bin = os.path.join(self.temp_dir.name, "bad_magic.signed.bin")

        with open(input_bin, "wb") as f:
            f.write(raw_payload)

        sign_firmware.sign_binary(self.priv_pem_path, input_bin, output_bin)

        with open(output_bin, "r+b") as f:
            f.seek(-76, os.SEEK_END)
            f.write(b"FAIL")

        with self.assertRaises(SystemExit) as cm:
            sign_firmware.verify_binary(self.pub_pem_path, output_bin)
        self.assertEqual(cm.exception.code, 1)

    def test_fault_injection_bad_trailer_version(self):
        """Unsupported trailer version is rejected."""
        raw_payload = bytes([0xE9] + [0x22] * 100)
        input_bin = os.path.join(self.temp_dir.name, "bad_ver.bin")
        output_bin = os.path.join(self.temp_dir.name, "bad_ver.signed.bin")

        with open(input_bin, "wb") as f:
            f.write(raw_payload)

        sign_firmware.sign_binary(self.priv_pem_path, input_bin, output_bin)

        with open(output_bin, "r+b") as f:
            f.seek(-72, os.SEEK_END)
            f.write(bytes([2])) # version 2

        with self.assertRaises(SystemExit) as cm:
            sign_firmware.verify_binary(self.pub_pem_path, output_bin)
        self.assertEqual(cm.exception.code, 1)

    def test_fault_injection_bad_sig_len(self):
        """Invalid signature length in trailer is rejected."""
        raw_payload = bytes([0xE9] + [0x33] * 100)
        input_bin = os.path.join(self.temp_dir.name, "bad_siglen.bin")
        output_bin = os.path.join(self.temp_dir.name, "bad_siglen.signed.bin")

        with open(input_bin, "wb") as f:
            f.write(raw_payload)

        sign_firmware.sign_binary(self.priv_pem_path, input_bin, output_bin)

        with open(output_bin, "r+b") as f:
            f.seek(-66, os.SEEK_END)
            f.write((32).to_bytes(2, byteorder="little")) # sig_len = 32

        with self.assertRaises(SystemExit) as cm:
            sign_firmware.verify_binary(self.pub_pem_path, output_bin)
        self.assertEqual(cm.exception.code, 1)

    def test_cpp_test_vectors_cryptographic_interop(self):
        """Verify that the test vectors in test_ota_verification.cpp verify against ota_public_key.h."""
        pubkey_header_path = os.path.join(REPO_ROOT, "PulseShift", "src", "ota_public_key.h")
        with open(pubkey_header_path, "r", encoding="utf-8") as f:
            content = f.read()

        match = re.search(r"PULSESHIFT_OTA_PUBLIC_KEY\[65\]\s*=\s*\{([^}]+)\};", content)
        self.assertIsNotNone(match, "Could not find PULSESHIFT_OTA_PUBLIC_KEY in ota_public_key.h")

        hex_bytes = [int(x.strip(), 16) for x in match.group(1).split(",") if x.strip()]
        self.assertEqual(len(hex_bytes), 65)
        self.assertEqual(hex_bytes[0], 0x04)

        pub_key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), bytes(hex_bytes))

        # Reconstruct the 65536-byte deterministic test payload from test_ota_verification.cpp
        payload = bytearray([0xE9] + [((i * 7 + 13) & 0xFF) for i in range(1, 65536)])
        expected_crc = 0x9BA054C9
        actual_crc = zlib.crc32(payload) & 0xFFFFFFFF
        self.assertEqual(actual_crc, expected_crc)

        digest = hashlib.sha256(payload).digest()
        self.assertEqual(digest.hex(), "7c17146cfc62aebe04f2e705c60a9eb09f8c86c05b61f9c1044fad1d9dd07d13")

        # VALID_TEST_SIG from test_ota_verification.cpp
        valid_test_sig_hex = (
            "5cc6e275516be9d3c2a4aa3a78f1b3524fb366af1de716d6f9bf190554c74c21"
            "f2ce122ab228340fdcd7393be56966a05f962d5bae8e8fc02a78b4bfd8ba57aa"
        )
        raw_sig = bytes.fromhex(valid_test_sig_hex)
        self.assertEqual(len(raw_sig), 64)

        r = int.from_bytes(raw_sig[:32], byteorder="big")
        s = int.from_bytes(raw_sig[32:64], byteorder="big")
        der_sig = utils.encode_dss_signature(r, s)

        # Cryptographically verify using Prehashed SHA-256
        pub_key.verify(der_sig, digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))


if __name__ == "__main__":
    unittest.main()
