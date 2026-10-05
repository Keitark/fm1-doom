import hashlib
import json
import math
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from make_sound_bank import (extract_whx, extract_wad, encode_ima, encode_pcm_sound,
                             validate_sound, reference_pcm, emit_source)


def fixture():
    sound = struct.pack("<HHIhBB", 0x8003, 11025, 9, 0, 0, 0) + bytes((0x17, 0x80, 0xF7, 0x11))
    whx = bytearray(56 + len(sound))
    struct.pack_into("<4sIII", whx, 0, b"IWHX", 1, 36, len(whx))
    struct.pack_into("<H", whx, 34, 1)
    struct.pack_into("<II", whx, 36, 56, len(whx))
    struct.pack_into("<10sH", whx, 44, b"dspistol", 0)
    whx[56:] = sound
    return sound, whx


def wad_fixture(names=(b"DSPISTOL",), sample=128):
    native = struct.pack("<HHI", 3, 11025, 49) + bytes([sample]) * 49
    directory = 12 + len(native) * len(names)
    wad = bytearray(struct.pack("<4sII", b"IWAD", len(names), directory))
    wad.extend(native * len(names))
    for i, name in enumerate(names):
        wad.extend(struct.pack("<II8s", 12 + i * len(native), len(native), name))
    return native, wad


class SoundBankTests(unittest.TestCase):
    def test_bounded_directory_and_private_source(self):
        sound, whx = fixture()
        extracted = extract_whx(bytes(whx), (b"DSPISTOL",))
        self.assertEqual(extracted, [(b"DSPISTOL", sound)])
        source = emit_source(extracted)
        self.assertIn('const uint8_t fm1_doom_sound_bank[]', source)
        self.assertIn('{"pistol", fm1_doom_sound_bank + 0u, 16u}', source)
        for broken in (whx[:35], whx[:-1], whx + b"x"):
            with self.assertRaises(ValueError):
                extract_whx(bytes(broken), (b"DSPISTOL",))
        for offset, value in ((8, 35), (36, 0xFFFFFF), (40, 0xFFFFFF)):
            bad = bytearray(whx)
            struct.pack_into("<I", bad, offset, value)
            with self.assertRaises(ValueError):
                extract_whx(bytes(bad), (b"DSPISTOL",))
        with self.assertRaises(ValueError):
            extract_whx(bytes(whx), (b"DSOOF",))

    def test_ima_predictor_nibble_order_and_bounds(self):
        sound, _ = fixture()
        self.assertEqual(reference_pcm(sound), [0, 11, 17, 18, 17, 39, -7, 12, 30])
        for offset, value in ((0, 0), (1, 0), (2, 0), (10, 89), (11, 1)):
            bad = bytearray(sound)
            bad[offset] = value
            with self.assertRaises(ValueError):
                validate_sound(bytes(bad))
        for amount in (1, 2, 3):
            with self.assertRaises(ValueError):
                validate_sound(sound[:-amount])
        for count in (0, 1_000_001):
            bad = bytearray(sound)
            struct.pack_into("<I", bad, 4, count)
            with self.assertRaises(ValueError):
                validate_sound(bytes(bad))

    def test_native_ima_quality_and_bounded_duration(self):
        # A known smooth waveform exercises both signs and block boundaries.
        samples = [round(16000 * math.sin(i * 2 * math.pi / 80)) for i in range(1000)]
        encoded = encode_ima(samples, 11025)
        decoded = reference_pcm(encoded)
        self.assertEqual(struct.unpack_from("<HHI", encoded), (0x8003, 11025, 1000))
        self.assertLessEqual(max(abs(a - b) for a, b in zip(samples, decoded)), 256)
        rms = math.sqrt(sum((a - b) ** 2 for a, b in zip(samples, decoded)) / len(samples))
        self.assertLess(rms, 100)
        for count in (1, 2, 8, 9, 248, 249, 250, 257, 498, 499, 1000):
            for rate in (8000, 11025):
                encoded = encode_ima([0] * count, rate)
                decoded = reference_pcm(encoded)
                self.assertEqual(decoded, [0] * len(decoded))
                self.assertGreaterEqual(len(decoded), count)
                self.assertLessEqual(len(decoded) - count, 7)
                self.assertLess((len(decoded) - count) / rate, 0.0009)
                # Full blocks128 bytes; final block predictor4 + payload4n.
                for offset in range(8, len(encoded), 128):
                    self.assertEqual((len(encoded[offset:offset + 128]) - 4) % 4, 0)
        for samples, rate in (([], 8000), ([32768], 8000), ([-32769], 8000),
                              ([0], 0), ([0], 22050)):
            with self.assertRaises(ValueError):
                encode_ima(samples, rate)

    def test_native_dmx_header_and_iwad_directory_bounds(self):
        native, wad = wad_fixture(sample=160)
        encoded = encode_pcm_sound(native)
        self.assertEqual(struct.unpack_from("<I", encoded, 4)[0], 17)
        self.assertEqual(reference_pcm(encoded), [8192] * 17)
        self.assertEqual(extract_wad(bytes(wad), (b"DSPISTOL",)), [(b"DSPISTOL", encoded)])
        # Source byte16/effective count-32 matches the pinned engine trim.
        ramp = struct.pack("<HHI", 3, 11025, 80) + bytes(range(80))
        trimmed = [(x - 128) * 256 for x in range(8, 56)]
        self.assertEqual(encode_pcm_sound(ramp), encode_ima(trimmed, 11025))
        for offset, value in ((0, 4), (2, 0), (2, 22050), (4, 48), (4, 50), (4, 1_000_001)):
            bad = bytearray(native)
            struct.pack_into("<I" if offset == 4 else "<H", bad, offset, value)
            with self.assertRaises(ValueError):
                encode_pcm_sound(bytes(bad))
        for broken in (native[:7], native[:-1]):
            with self.assertRaises(ValueError):
                encode_pcm_sound(broken)
        directory = struct.unpack_from("<I", wad, 8)[0]
        for offset, value in ((4, 0), (4, 100_001), (8, 11), (8, len(wad)),
                              (directory, len(wad) + 1), (directory + 4, len(wad))):
            bad = bytearray(wad)
            struct.pack_into("<I", bad, offset, value)
            with self.assertRaises(ValueError):
                extract_wad(bytes(bad), (b"DSPISTOL",))
        for broken in (wad[:11], wad[:-1], b"PWAD" + wad[4:]):
            with self.assertRaises(ValueError):
                extract_wad(bytes(broken), (b"DSPISTOL",))
        with self.assertRaises(ValueError):
            extract_wad(bytes(wad), (b"DSOOF",))
        _, duplicate = wad_fixture((b"DSPISTOL", b"DSPISTOL"))
        with self.assertRaises(ValueError):
            extract_wad(bytes(duplicate), (b"DSPISTOL",))

    def test_native_iwad_cli_uses_only_local_temporary_output(self):
        _, wad = wad_fixture((b"DSPISTOL", b"DSOOF", b"DSITEMUP"))
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "doom1.wad"
            output = Path(temporary) / "sound-bank"
            source.write_bytes(wad)
            command = [sys.executable, str(Path(__file__).resolve().parents[1] / "tools/make_sound_bank.py"),
                       "--wad", str(source), "--output-dir", str(output)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual(manifest["source_format"], "IWAD_PCM8")
            self.assertEqual(manifest["payload_bytes"], 60)
            self.assertEqual(manifest["ima_padding_max_samples"], 7)
            generated = (output / "fm1_doom_sound_bank.c").read_bytes()
            self.assertEqual(hashlib.sha256(generated).hexdigest(), manifest["generated_source_sha256"])


if __name__ == "__main__":
    unittest.main()
