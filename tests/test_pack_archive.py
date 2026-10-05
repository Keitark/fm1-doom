import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pack_archive import pack, unpack


class ArchiveTests(unittest.TestCase):
    def test_roundtrip_and_corruption(self):
        raw = b"IWAD" + bytes(range(256)) * 33
        archive = pack(raw, 1024)
        self.assertEqual(unpack(archive), raw)
        for broken in (archive[:-1], archive + b"x", archive[:16] + b"\0" * 12 + archive[28:]):
            with self.assertRaises(ValueError):
                unpack(broken)
        altered = bytearray(archive)
        altered[-1] ^= 1
        with self.assertRaises((ValueError, __import__("zlib").error)):
            unpack(bytes(altered))

    def test_requires_wad_and_valid_block_size(self):
        with self.assertRaises(ValueError):
            pack(b"not a WAD", 4096)
        with self.assertRaises(ValueError):
            pack(b"IWAD" + bytes(20), 1000)


if __name__ == "__main__":
    unittest.main()
