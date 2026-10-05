"""Offline Doom+known FM-1 SDK ELF link probe; never packages or flashes.

This deliberately retains the existing NES boot app as a known SDK baseline
and forces Doom into the link. Its size is a conservative diagnostic, not a
bootable Doom application or verified flash budget.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fm1-root", type=Path, required=True)
    parser.add_argument("--lowres", action="store_true",
                        help="Link generated 160x100 engine probe objects")
    args = parser.parse_args()
    fm1 = args.fm1_root.resolve()
    sys.path.insert(0, str(fm1 / "firmware/nes"))
    import build_boot as board
    sdk = board.SDK.resolve()
    pin = subprocess.check_output(["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True).strip()
    if sdk != (fm1 / "references/source/fw-AC79_AIoT_SDK").resolve() or pin != board.SDK_PIN:
        parser.error("unexpected SDK path or revision")
    if subprocess.check_output(["git", "-C", str(sdk), "status", "--porcelain"], text=True).strip():
        parser.error("SDK checkout is dirty")
    base = fm1 / "firmware/nes/build/boot-stock-power-smb1"
    if not (base / "sdk.ld").is_file() or not (base / "sdk.used").is_file():
        parser.error("known offline boot baseline is missing")
    board_objects = sorted(base.glob("[0-9][0-9]-*.o"))
    engine_dir = ROOT / ("build/target-engine-lowres" if args.lowres else "build/target-engine")
    port_dir = ROOT / ("build/target-port-lowres" if args.lowres else "build/target-port")
    engine_objects = sorted(engine_dir.glob("*.c.o"))
    port_objects = sorted(port_dir.glob("*.o"))
    if len(engine_objects) != 79 or len(port_objects) != 6:
        parser.error("compile_target_engine.py and compile_target_port.py must pass first")
    libs = [sdk / "include_lib/newlib/pi32v2-lib" / n
            for n in ("libm.a", "libc.a", "libcompiler_rt.a")]
    libs += [sdk / "cpu/wl82/liba" / n for n in
             ("cpu.a", "event.a", "system.a", "cfg_tool.a", "fs.a", "common_lib.a", "update.a", "zliblite.a")]
    nes = fm1 / "firmware/nes/build/pi32v2-smb1/libfm1_nes.a"
    out = ROOT / ("build/target-link-lowres" if args.lowres else "build/target-link")
    out.mkdir(parents=True, exist_ok=True)
    make = board.MAKE.read_text(encoding="utf-8")
    flags = board.make_list(make, "CFLAGS")
    includes = ["-I" + str(board.sdk_path(i[2:])) for i in board.make_list(make, "INCLUDES")]
    shim = out / "probe_libc_shims.o"
    subprocess.run([str(board.TC / "clang.exe"), *flags, *includes, "-c",
                    str(ROOT / "tools/probe_libc_shims.c"), "-o", str(shim)], check=True)
    elf = out / "fm1-doom-size-probe.elf"
    command = [str(board.TC / "pi32v2-lto-wrapper.exe"), "-o", str(elf),
               *map(str, board_objects + engine_objects + port_objects + [shim]),
               "--start-group", str(nes), *map(str, libs), "--end-group",
               "-T" + str(base / "sdk.ld"), "-M=" + str(out / "fm1-doom-size-probe.map"),
               "--wrap=boot_info_init", "--wrap=memory_init", "--undefined=memory_init",
               "--undefined=doomgeneric_Create", "--undefined=doomgeneric_Tick",
               "--undefined=fm1_doom_bind_io",
               "--undefined=fm1_fmd_zliblite_inflate", "--undefined=fm1_doom_set_wad_archive",
               "--plugin-opt=mcpu=r3", "--plugin-opt=-mattr=+fprev1",
               "--plugin-opt=-pi32v2-large-program=true",
               "--plugin-opt=-used-symbol-file=" + str(base / "sdk.used")]
    result = subprocess.run(command, cwd=out, capture_output=True, text=True)
    (out / "link.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    print(f"board={len(board_objects)} engine={len(engine_objects)} port={len(port_objects)}")
    print((result.stdout + result.stderr)[-8000:])
    if result.returncode:
        return result.returncode
    sections = subprocess.run([str(board.TC / "llvm-objdump.exe"), "-h", str(elf)],
                              capture_output=True, text=True)
    if sections.returncode:
        print(sections.stderr)
        return sections.returncode
    sizes = {name: int(value, 16) for name, value in
             re.findall(r"^\s*\d+\s+(\.[A-Za-z0-9_]+)\s+([0-9a-fA-F]+)",
                        sections.stdout, re.M)}
    for name in (".text", ".ram0_data", ".ram0_bss", ".cache_ram_data"):
        print(f"{name}: {sizes.get(name, 0):,} bytes")
    print(f"app sections (no WAD): {sum(sizes.get(n, 0) for n in ('.text', '.data', '.dynamic_data', '.ram0_data', '.cache_ram_data')):,} bytes")
    print("Probe only: retains NES app, uses nonfunctional libc shims, and does not run Doom on FM-1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
