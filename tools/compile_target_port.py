"""Compile-only FM-1 adapter check; never links, packages, or contacts a device."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fm1-root", type=Path, required=True)
    parser.add_argument("--lowres", action="store_true",
                        help="Compile against generated 160x100 engine source")
    args = parser.parse_args()
    fm1 = args.fm1_root.resolve()
    sys.path.insert(0, str(fm1 / "firmware" / "nes"))
    import build_boot as board

    sdk = board.SDK.resolve()
    if sdk != (fm1 / "references/source/fw-AC79_AIoT_SDK").resolve():
        parser.error("FM-1 SDK path did not resolve inside --fm1-root")
    pin = subprocess.check_output(["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True).strip()
    if pin != board.SDK_PIN:
        parser.error(f"FM-1 SDK commit differs from the reviewed pin: {pin}")
    if subprocess.check_output(["git", "-C", str(sdk), "status", "--porcelain"], text=True).strip():
        parser.error("FM-1 SDK checkout is dirty")
    make_text = board.MAKE.read_text(encoding="utf-8")
    flags = board.make_list(make_text, "CFLAGS")
    if args.lowres:
        flags += ["-DFM1_DOOM_SOURCE_WIDTH=160", "-DFM1_DOOM_SOURCE_HEIGHT=100"]
    engine = ROOT / ("build/lowres-source" if args.lowres else "vendor/doomgeneric/doomgeneric")
    if not (engine / "i_video.h").is_file():
        parser.error("generate the low-resolution source first")
    includes = ["-I" + str(ROOT / "include"), "-I" + str(engine)]
    includes += ["-I" + str(sdk / "apps/common")]
    includes += ["-I" + str(board.sdk_path(item[2:])) for item in board.make_list(make_text, "INCLUDES")]
    out = ROOT / "build" / ("target-port-lowres" if args.lowres else "target-port")
    out.mkdir(parents=True, exist_ok=True)
    for name in ("fm1_doom_port", "doomgeneric_fm1", "i_video_fm1", "fm1_doom_archive", "fm1_fmd_zliblite", "w_file_fm1"):
        source = ROOT / "src" / (name + ".c")
        object_file = out / (name + ".o")
        subprocess.run([str(board.TC / "clang.exe"), *flags, *includes,
                        "-c", str(source), "-o", str(object_file)], check=True)
        print(f"compiled {name}: {object_file.stat().st_size} object bytes")
    print("Compile-only check passed. No target link or device acceptance was performed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
