import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from stage_wad import Lump, MAP_LUMPS, omit_direct_boot_ui, pixelate_flat, pixelate_patch, prune_graphics


def patch():
    header = struct.pack("<HHhh", 4, 4, 1, 2)
    columns = [bytes([0, 4, 0, *range(x * 4, x * 4 + 4), 0, 255]) for x in range(4)]
    offsets = struct.pack("<4I", *(24 + 9 * x for x in range(4)))
    return header + offsets + b"".join(columns)


def complete_fixture():
    result = [Lump(b"M_DOOM", patch()), Lump(b"E1M1", b"")]
    things = b"".join(struct.pack("<hhhhh", 0, 0, 0, kind, 7)
                      for kind in (1, 3004, 9, 3001))
    geometry = {
        b"THINGS": things,
        b"SIDEDEFS": struct.pack("<hh8s8s8sH", 0, 0, b"SKY1", b"-", b"-", 0),
        b"SECTORS": struct.pack("<hh8s8shhh", 0, 128, b"FLAT1", b"FLAT1", 160, 0, 0),
    }
    result += [Lump(name, geometry.get(name, b"")) for name in MAP_LUMPS]
    texture = struct.pack("<8sIHHIH", b"SKY1", 0, 4, 4, 0, 1)
    texture += struct.pack("<hhHhh", 0, 0, 0, 0, 0)
    result += [Lump(b"TEXTURE1", struct.pack("<II", 1, 8) + texture),
               Lump(b"PNAMES", struct.pack("<I8s", 1, b"WALLPIC")),
               Lump(b"S_START", b"")]
    result += [Lump(name, patch()) for name in
               (b"PISGA0", b"PISFA0", b"PUNGA0", b"SHTGA0", b"POSSA1", b"SPOSA1", b"TROOA1")]
    result += [Lump(b"S_END", b""), Lump(b"P_START", b""),
               Lump(b"WALLPIC", patch()), Lump(b"P_END", b""),
               Lump(b"F_START", b""), Lump(b"FLAT1", bytes(range(256)) * 16),
               Lump(b"F_END", b"")]
    return result


class StageQualityTests(unittest.TestCase):
    def test_original_pistol_and_fist_keep_pixels_while_world_is_coarse(self):
        original = complete_fixture()
        result = prune_graphics(original, b"E1M1", True, 4, weapon_pixelate=1)
        original_by_name = {l.name: l.data for l in original}
        by_name = {l.name: l.data for l in result}
        self.assertEqual([l.name for l in result], [l.name for l in original])
        for name in (b"PISGA0", b"PISFA0", b"PUNGA0", b"M_DOOM"):
            self.assertEqual(by_name[name], original_by_name[name])
        for name in (b"SHTGA0", b"POSSA1", b"SPOSA1", b"TROOA1", b"WALLPIC"):
            self.assertEqual(by_name[name], pixelate_patch(original_by_name[name], 4))
            self.assertNotEqual(by_name[name], original_by_name[name])
        self.assertEqual(by_name[b"FLAT1"], pixelate_flat(original_by_name[b"FLAT1"], 4))
        self.assertEqual(by_name[b"TEXTURE1"], original_by_name[b"TEXTURE1"])
        self.assertEqual(by_name[b"PNAMES"], original_by_name[b"PNAMES"])
        self.assertIn(Lump(b"M_DOOM", original_by_name[b"M_DOOM"]),
                      omit_direct_boot_ui(result, menu_ui=True))

    def test_omitted_override_keeps_existing_profile(self):
        original = complete_fixture()
        result = prune_graphics(original, b"E1M1", True, 4)
        explicit = prune_graphics(original, b"E1M1", True, 4, weapon_pixelate=4)
        self.assertEqual(result, explicit)
        by_name = {l.name: l.data for l in result}
        self.assertEqual(by_name[b"PISGA0"], pixelate_patch(patch(), 4))

    def test_weapon_override_can_be_coarser_than_world(self):
        result = prune_graphics(complete_fixture(), b"E1M1", True, 1, weapon_pixelate=2)
        by_name = {l.name: l.data for l in result}
        self.assertEqual(by_name[b"PISGA0"], pixelate_patch(patch(), 2))
        self.assertEqual(by_name[b"TROOA1"], patch())
        self.assertEqual(by_name[b"M_DOOM"], patch())


if __name__ == "__main__":
    unittest.main()
