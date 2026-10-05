import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from stage_wad import (Lump, MAP_LUMPS, compact_assets, omit_direct_boot_ui,
                       patch_columns, pixelate_flat, pixelate_patch,
                       prune_graphics, read_wad, share_patch_columns,
                       validate_compact_assets, write_wad)


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

    def test_fist_space_saving_preserves_pistol_enemies_menu_and_geometry(self):
        original = complete_fixture()
        baseline = prune_graphics(original, b"E1M1", True, 4, weapon_pixelate=1)
        result = prune_graphics(original, b"E1M1", True, 4,
                                weapon_pixelate=1, fist_pixelate=2)
        self.assertEqual([l.name for l in result], [l.name for l in baseline])
        for before, after in zip(baseline, result):
            if before.name == b"PUNGA0":
                self.assertEqual(after.data, pixelate_patch(patch(), 2))
                self.assertNotEqual(after.data, before.data)
                self.assertEqual(after.data[:8], before.data[:8])
            else:
                self.assertEqual(after, before)

    def test_weapon_override_can_be_coarser_than_world(self):
        result = prune_graphics(complete_fixture(), b"E1M1", True, 1, weapon_pixelate=2)
        by_name = {l.name: l.data for l in result}
        self.assertEqual(by_name[b"PISGA0"], pixelate_patch(patch(), 2))
        self.assertEqual(by_name[b"TROOA1"], patch())
        self.assertEqual(by_name[b"M_DOOM"], patch())


def multi_post_patch():
    # Two separated posts retain top offsets, lengths, pixels and both padding
    # bytes. Columns 0/1 are byte-identical but originally stored separately.
    first = bytes((0, 2, 7, 10, 11, 8, 5, 2, 9, 12, 13, 6, 255))
    last = bytes((1, 3, 4, 20, 21, 22, 5, 255))
    return struct.pack("<hhhhIII", 3, 8, -2, 4, 20, 33, 46) + first + first + last


class CompactAssetTests(unittest.TestCase):
    def test_column_sharing_preserves_all_posts_and_patch_geometry(self):
        original = multi_post_patch()
        result = share_patch_columns(original)
        self.assertEqual(patch_columns(result), patch_columns(original))
        self.assertEqual(result[:8], original[:8])
        offsets = struct.unpack_from("<III", result, 8)
        self.assertEqual(offsets[0], offsets[1])
        self.assertNotEqual(offsets[1], offsets[2])
        self.assertEqual(len(original) - len(result), 13)

    def test_compaction_remaps_pnames_and_keeps_all_names_and_menu_pixels(self):
        original = complete_fixture()
        table = next(l.data for l in original if l.name == b"TEXTURE1")
        table = bytearray(table)
        struct.pack_into("<H", table, 8 + 26, 1)
        original = [Lump(l.name, bytes(table)) if l.name == b"TEXTURE1"
                    else Lump(l.name, struct.pack("<I8s8s8s", 3, b"UNUSED0", b"WALLPIC", b"UNUSED2"))
                    if l.name == b"PNAMES"
                    else Lump(l.name, multi_post_patch()) if l.name == b"M_DOOM"
                    else l for l in original]
        # An unused patch remains addressable by its existing logical name.
        end = next(i for i,l in enumerate(original) if l.name == b"P_END")
        original.insert(end, Lump(b"UNUSED0", multi_post_patch()))
        result = compact_assets(original)
        validate_compact_assets(original, result)
        self.assertEqual([l.name for l in result], [l.name for l in original])
        by_name = {l.name:l.data for l in result}
        self.assertEqual(by_name[b"PNAMES"], struct.pack("<I8s", 1, b"WALLPIC"))
        self.assertEqual(struct.unpack_from("<H", by_name[b"TEXTURE1"], 8 + 26)[0], 0)
        self.assertEqual(patch_columns(by_name[b"M_DOOM"]), patch_columns(multi_post_patch()))
        self.assertIn(b"UNUSED0", by_name)
        for before, after in zip(original, result):
            if before.name in MAP_LUMPS or before.name in (b"FLAT1", b"E1M1"):
                self.assertEqual(before, after)

    def test_pnames_closure_includes_texture2(self):
        original = complete_fixture()
        table = bytearray(next(l.data for l in original if l.name == b"TEXTURE1"))
        struct.pack_into("<H", table, 8 + 26, 1)
        original = [Lump(l.name, struct.pack("<I8s8s", 2, b"WALLPIC", b"SECOND"))
                    if l.name == b"PNAMES" else l for l in original]
        original.append(Lump(b"TEXTURE2", bytes(table)))
        result = compact_assets(original)
        validate_compact_assets(original, result)
        self.assertEqual(next(l.data for l in result if l.name == b"PNAMES"),
                         struct.pack("<I8s8s", 2, b"WALLPIC", b"SECOND"))

    def test_shared_payload_offsets_preserve_logical_reader_results(self):
        original = complete_fixture()
        compacted = compact_assets(original)
        with tempfile.TemporaryDirectory() as directory:
            plain = Path(directory)/"plain.wad"
            shared = Path(directory)/"shared.wad"
            write_wad(plain, compacted)
            write_wad(shared, compacted, share_payloads=True)
            self.assertEqual(read_wad(plain), compacted)
            self.assertEqual(read_wad(shared), compacted)
            self.assertLess(shared.stat().st_size, plain.stat().st_size)
            data = shared.read_bytes()
            _,count,start = struct.unpack_from("<4sII", data)
            records = {name.rstrip(b"\0"):pos for pos,size,name in
                       (struct.unpack_from("<II8s",data,start+16*i) for i in range(count))}
            self.assertEqual(records[b"PISGA0"], records[b"TROOA1"])

    def test_malformed_column_bounds_and_missing_terminator_are_rejected(self):
        source = multi_post_patch()
        bad_offset = bytearray(source)
        struct.pack_into("<I", bad_offset, 8, len(source))
        bad_header = bytearray(source)
        struct.pack_into("<I", bad_header, 8, 0)
        bad_count = bytearray(source)
        bad_count[21] = 255
        for data in (b"", source[:19], bytes(bad_offset), bytes(bad_header),
                     bytes(bad_count), source[:-1]):
            with self.subTest(data=data):
                with self.assertRaises(ValueError):
                    share_patch_columns(data)

    def test_invalid_texture_and_pnames_tables_are_rejected(self):
        original = complete_fixture()
        bad_index = bytearray(next(l.data for l in original if l.name == b"TEXTURE1"))
        struct.pack_into("<H", bad_index, 8 + 26, 1)
        for name,data in ((b"PNAMES",b""),
                          (b"PNAMES",struct.pack("<I",2)+b"WALLPIC\0"),
                          (b"TEXTURE1",struct.pack("<II",1,0)),
                          (b"TEXTURE1",bytes(bad_index))):
            with self.subTest(name=name,data=data):
                fixture=[Lump(l.name,data) if l.name == name else l for l in original]
                with self.assertRaises(ValueError):
                    compact_assets(fixture)

    def test_validator_rejects_changed_pixels_geometry_and_map_bytes(self):
        original = complete_fixture()
        result = compact_assets(original)
        for name,index in ((b"M_DOOM",27),(b"WALLPIC",27),
                           (b"M_DOOM",4),(b"THINGS",0)):
            altered=[]
            for lump in result:
                if lump.name == name:
                    data=bytearray(lump.data)
                    data[index] ^= 1
                    lump=Lump(lump.name,bytes(data))
                altered.append(lump)
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    validate_compact_assets(original,altered)


if __name__ == "__main__":
    unittest.main()
