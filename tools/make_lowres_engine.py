"""Create a disposable 160x100 Doomgeneric source variant under build/.

The pinned upstream submodule is never edited. This is a host/size experiment:
the legacy 320-pixel status bar is suppressed; the adapter draws a small HUD.
"""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "vendor/doomgeneric/doomgeneric"
TARGET = ROOT / "build/lowres-source"
PIN = "dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284"


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise ValueError(f"unexpected upstream source: {path.name}")
    path.write_text(text.replace(old, new), encoding="utf-8")


def main() -> None:
    head = subprocess.check_output(["git", "-C", str(SOURCE), "rev-parse", "HEAD"], text=True).strip()
    if head != PIN:
        raise ValueError(f"unreviewed Doomgeneric revision: {head}")
    if subprocess.check_output(["git", "-C", str(SOURCE), "status", "--porcelain"], text=True).strip():
        raise ValueError("Doomgeneric submodule has local changes")
    shutil.copytree(SOURCE, TARGET, dirs_exist_ok=True)
    replace_once(TARGET / "i_video.h", "#define SCREENWIDTH  320", "#define SCREENWIDTH  160")
    replace_once(TARGET / "i_video.h", "#define SCREENHEIGHT 200", "#define SCREENHEIGHT 100")
    replace_once(TARGET / "r_main.c", "    setblocks = blocks;",
                 "    setblocks = 11; /* low-resolution proof: force full view */")
    status = TARGET / "st_stuff.c"
    text = status.read_text(encoding="utf-8")
    start = text.index("void ST_Drawer (boolean fullscreen, boolean refresh)")
    end = text.index("typedef void (*load_callback_t)", start)
    if text.count("void ST_Drawer (boolean fullscreen, boolean refresh)") != 1:
        raise ValueError("unexpected ST_Drawer layout")
    status.write_text(text[:start] +
                      "void ST_Drawer (boolean fullscreen, boolean refresh)\n"
                      "{\n    (void)fullscreen; (void)refresh;\n"
                      "    ST_doPaletteStuff();\n}\n\n" + text[end:], encoding="utf-8")
    print(TARGET)
    print("Experimental 160x100 engine generated. The adapter supplies a compact HUD.")


if __name__ == "__main__":
    main()
