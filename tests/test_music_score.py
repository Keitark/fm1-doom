import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from make_music_score import compress_score, encode_events, expand_score, mus_events, wad_music, whx_music


class MusicScoreTests(unittest.TestCase):
    def test_native_wad_extraction_and_bounds(self):
        music = b"MUS\x1a" + bytes(12)
        wad = (struct.pack("<4sII", b"IWAD", 2, 12 + len(music)) + music +
               struct.pack("<II8s", 12, 0, b"PLAYPAL") +
               struct.pack("<II8s", 12, len(music), b"D_E1M1"))
        self.assertEqual(wad_music(wad), music)
        self.assertEqual(wad_music(b"PWAD" + wad[4:]), music)
        for invalid in (wad[:-1], b"IWAD" + struct.pack("<II", 1, 0),
                        wad[:-16] + struct.pack("<II8s", len(wad), 1, b"D_E1M1"),
                        wad[:-8] + b"D_E1M1xx"):
            with self.assertRaises(ValueError):
                wad_music(invalid)

    def test_original_mus_timing_velocity_and_controllers(self):
        stream = bytes([0x40, 0, 29, 0x90, 60 | 128, 100, 0x81, 0,
                        0x10, 64, 0x80, 60, 7, 0x60])
        mus = struct.pack("<4s6H", b"MUS\x1a", len(stream), 16, 1, 0, 0, 0) + stream
        self.assertEqual(mus_events(mus), [(0, 4, 0, 0, 29), (0, 1, 0, 60, 100),
                                          (128, 1, 0, 64, 100), (128, 0, 0, 60, 0),
                                          (135, 6, 0, 0, 0)])

    def test_score_compression_preserves_all_event_bytes(self):
        events = [(0, 1, 0, 60, 100), (128, 0, 0, 60, 0), (140, 6, 0, 0, 0)]
        raw = encode_events(events)
        self.assertEqual(raw, bytes([0, 16, 60, 100, 128, 1, 0, 60, 12, 96]))
        bank, depth = compress_score(raw * 100)
        self.assertEqual(expand_score(bank), raw * 100)
        self.assertLess(depth, 17)
        self.assertLess(len(bank), len(raw) * 100)

    def test_rejects_malformed_sources_and_grammar(self):
        with self.assertRaises(ValueError):
            mus_events(b"MUS\x1a" + b"\0" * 12)
        with self.assertRaises(ValueError):
            whx_music(b"IWHX" + b"\xff" * 40)
        bank, _ = compress_score(b"abcdefgh" * 100)
        changed = bytearray(bank)
        changed[17] = 0
        with self.assertRaises(ValueError):
            expand_score(changed)


if __name__ == "__main__":
    unittest.main()
