import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from stage_wad import Lump, omit_direct_boot_ui


class DirectBootUiTests(unittest.TestCase):
    def test_omits_only_ui_outside_asset_namespaces(self):
        source = [
            Lump(b"M_DOOM", b"menu"), Lump(b"WIMAP0", b"intermission"),
            Lump(b"STBAR", b"status"), Lump(b"F_START", b""),
            Lump(b"STEP2", b"level flat"), Lump(b"F_END", b""),
            Lump(b"PLAYPAL", b"palette"),
        ]
        result = omit_direct_boot_ui(source)
        self.assertEqual([x.name for x in result],
                         [b"F_START", b"STEP2", b"F_END", b"PLAYPAL"])


if __name__ == "__main__":
    unittest.main()
