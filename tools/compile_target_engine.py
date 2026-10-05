"""Diagnostic compile of pinned Doomgeneric core for pi32v2; no link or flash."""
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DOOM = ROOT / "vendor/doomgeneric/doomgeneric"
SKIP = {"doomgeneric.c", "doomgeneric_win.c", "i_video.c", "w_file.c"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fm1-root", type=Path, required=True)
    args = parser.parse_args()
    fm1 = args.fm1_root.resolve()
    sys.path.insert(0, str(fm1 / "firmware/nes"))
    import build_boot as board
    sdk = board.SDK.resolve()
    if sdk != (fm1 / "references/source/fw-AC79_AIoT_SDK").resolve():
        parser.error("SDK path differs from selected FM-1 root")
    pin = subprocess.check_output(["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True).strip()
    if pin != board.SDK_PIN:
        parser.error("SDK revision differs from the reviewed pin")
    if subprocess.check_output(["git", "-C", str(sdk), "status", "--porcelain"], text=True).strip():
        parser.error("SDK checkout is dirty")
    make = board.MAKE.read_text(encoding="utf-8")
    flags = board.make_list(make, "CFLAGS")
    includes = ["-I" + str(ROOT / "include"), "-I" + str(DOOM), "-I" + str(sdk / "apps/common")]
    includes.extend("-I" + str(board.sdk_path(i[2:])) for i in board.make_list(make, "INCLUDES"))
    names = re.findall(r'<ClCompile Include="([^\"]+\.c)"', (DOOM / "doomgeneric.vcxproj").read_text())
    out = ROOT / "build/target-engine"
    out.mkdir(parents=True, exist_ok=True)
    failures = []
    for name in names:
        if name in SKIP:
            continue
        result = subprocess.run([str(board.TC / "clang.exe"), *flags, *includes,
                                 "-c", str(DOOM / name), "-o", str(out / (name + ".o"))],
                                capture_output=True, text=True)
        if result.returncode:
            failures.append(name)
            print(f"FAIL {name}:\n{result.stderr[:1500]}", flush=True)
        else:
            print(f"PASS {name}", flush=True)
    print(f"Compiled {len(names) - len(SKIP) - len(failures)} core files; failed {len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
