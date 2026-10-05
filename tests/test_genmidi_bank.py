import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from make_genmidi_bank import expand_bank, make_bank, wad_genmidi


class GenmidiBankTests(unittest.TestCase):
    def test_used_original_operator_bytes_and_double_voice_flags_survive(self):
        table = b"#OPL_II#" + b"".join(bytes([i]) * 36 for i in range(175))
        events = [(0, 4, 0, 0, 29), (0, 1, 0, 60, 100), (1, 1, 15, 40, 100),
                  (1, 4, 1, 0, 34), (2, 1, 1, 28, 127), (3, 1, 0, 40, 0)]
        bank, indices = make_bank(table, events)
        self.assertEqual(indices, [29, 34, 133])
        self.assertEqual(expand_bank(bank), {i: bytes([i]) * 36 for i in indices})
        self.assertEqual(len(bank), 119)

    def test_directory_duplicate_resolution_and_bounds(self):
        table = b"#OPL_II#" + bytes(175 * 36)
        wad = (struct.pack("<4sII", b"IWAD", 2, 12 + len(table)) + table +
               struct.pack("<II8s", 12, 0, b"GENMIDI") +
               struct.pack("<II8s", 12, len(table), b"GENMIDI"))
        self.assertEqual(wad_genmidi(wad), table)
        with self.assertRaises(ValueError):
            wad_genmidi(wad[:-1])
        with self.assertRaises(ValueError):
            wad_genmidi(wad[:-16] + struct.pack("<II8s", len(wad), 1, b"GENMIDI"))

    def test_rejects_invalid_patch_closures(self):
        table = b"#OPL_II#" + bytes(175 * 36)
        for events in ([], [(0, 1, 15, 34, 1)], [(0, 1, 15, 82, 1)]):
            with self.assertRaises(ValueError):
                make_bank(table, events)
        bank, _ = make_bank(table, [(0, 1, 0, 60, 127)])
        for invalid in (bank[:-1], bank[:6] + b"\1\0" + bank[8:], bank[:8] + b"\xff" + bank[9:]):
            with self.assertRaises(ValueError):
                expand_bank(invalid)


if __name__ == "__main__":
    unittest.main()
