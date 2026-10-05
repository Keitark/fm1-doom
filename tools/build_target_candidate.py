"""Link an offline FM-1 Doom app from a lawful local FMD1 archive.

This emits an ELF and raw app section image for inspection. It does not make
an update package, invoke a downloader, or contact the device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from pack_archive import unpack

ROOT = Path(__file__).resolve().parents[1]
APP_LIMIT = 602_112  # stock V15 application allocation, not whole flash
RAM0_LIMIT = 523_596  # pinned linker RAM0 window


def run(command: list[str], *, cwd: Path | None = None) -> str:
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{Path(command[0]).name} failed:\n{(result.stdout + result.stderr)[-5000:]}")
    return result.stdout


def parse_sections(text: str) -> dict[str, tuple[int, int]]:
    return {name: (int(size, 16), int(vma, 16))
            for name, size, vma in re.findall(
                r"^\s*\d+\s+(\.[A-Za-z0-9_]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)",
                text, re.M)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="local 4 KiB-block FMD1 menu archive")
    parser.add_argument("--fm1-root", type=Path, required=True)
    args = parser.parse_args()
    fm1 = args.fm1_root.resolve()
    sys.path.insert(0, str(fm1 / "firmware/nes"))
    import build_boot as board

    sdk = board.SDK.resolve()
    if sdk != (fm1 / "references/source/fw-AC79_AIoT_SDK").resolve():
        parser.error("FM-1 SDK path is outside the expected checkout")
    if run(["git", "-C", str(sdk), "rev-parse", "HEAD"]).strip() != board.SDK_PIN:
        parser.error("FM-1 SDK revision differs from the reviewed pin")
    if run(["git", "-C", str(sdk), "status", "--porcelain"]).strip():
        parser.error("FM-1 SDK checkout is dirty")
    source = args.archive.read_bytes()
    if len(source) > 131_072 or source[4:8] != (4096).to_bytes(4, "little"):
        parser.error("archive must use 4 KiB FMD1 blocks and fit the 128 KiB app-data cap")
    if not unpack(source).startswith(b"IWAD"):
        parser.error("FMD1 payload is not an IWAD")

    base = fm1 / "firmware/nes/build/boot-stock-power-smb1"
    if not (base / "sdk.ld").is_file() or not (base / "sdk.used").is_file():
        parser.error("reviewed SDK boot baseline is missing")
    baseline = json.loads((base / "build-manifest.json").read_text(encoding="utf-8"))
    if baseline.get("sdk_commit") != board.SDK_PIN or not baseline.get("static_audit", {}).get("static_audit") == "passed":
        parser.error("reviewed SDK boot baseline audit or pin is missing")
    for name in ("boot_compat.c", "boot_trace.c", "board_power.c", "app_config.h"):
        path = fm1 / "firmware/nes/boot" / name
        expected = baseline.get("source_sha256", {}).get(str(path))
        if not expected or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            parser.error(f"board boot source differs from the reviewed baseline: {name}")
    board_objects = [p for p in sorted(base.glob("[0-9][0-9]-*.o"))
                     if p.name not in ("31-board.c.o", "32-app_main.c.o",
                                       "36-wl82_services.c.o")]
    engine_objects = sorted((ROOT / "build/target-engine-lowres").glob("*.c.o"))
    port_objects = sorted((ROOT / "build/target-port-lowres").glob("*.o"))
    if len(board_objects) != 34 or len(engine_objects) != 79 or len(port_objects) != 7:
        parser.error("run the lowres target engine and port compile checks first")
    generated = ROOT / "build/lowres-source"
    if (generated / "d_main.c").stat().st_mtime_ns < (ROOT / "tools/make_lowres_engine.py").stat().st_mtime_ns:
        parser.error("regenerate the low-resolution engine after changing its generator")
    for obj in engine_objects:
        source_path = generated / obj.name.removesuffix(".o")
        if not source_path.is_file() or obj.stat().st_mtime_ns < source_path.stat().st_mtime_ns:
            parser.error(f"target engine object is stale: {obj.name}")
    for obj in port_objects:
        source_path = ROOT / "src" / (obj.stem + ".c")
        if not source_path.is_file() or obj.stat().st_mtime_ns < source_path.stat().st_mtime_ns:
            parser.error(f"target port object is stale: {obj.name}")

    out = ROOT / "build/target-candidate"
    out.mkdir(parents=True, exist_ok=True)
    manifest = out / "build-manifest.json"
    manifest.write_text(json.dumps({"status": "build_in_progress_or_failed",
                                    "flashable": False}) + "\n", encoding="utf-8")
    blob = out / "embedded_archive.c"
    with blob.open("w", encoding="ascii", newline="\n") as file:
        file.write("#include <stdint.h>\n"
                   "const uint8_t fm1_doom_embedded_archive[] __attribute__((aligned(4),used)) = {\n")
        for pos in range(0, len(source), 16):
            file.write("    " + ",".join(f"0x{value:02x}" for value in source[pos:pos + 16]) + ",\n")
        file.write("};\nconst uint32_t fm1_doom_embedded_archive_len = "
                   + str(len(source)) + ";\n")

    make = board.MAKE.read_text(encoding="utf-8")
    flags = board.make_list(make, "CFLAGS")
    defines = board.make_list(make, "DEFINES")
    defines += ["-DFM1_DOOM_SOURCE_WIDTH=160", "-DFM1_DOOM_SOURCE_HEIGHT=100",
                "-DFM1_NES_PLAYER=1", "-DFM1_TARGET_PI32V2=1",
                "-DFM1_LCD_STOCK_FILL=1", "-DFM1_LCD_STOCK_DMA=1",
                "-DFM1_LCD_STOCK_SEQUENCE=1"]
    includes = ["-I" + str(path) for path in
                (ROOT / "include", ROOT / "build/lowres-source",
                 fm1 / "firmware/nes/boot", fm1 / "firmware/nes/include",
                 sdk / "apps/common")]
    includes += ["-I" + str(board.sdk_path(item[2:]))
                 for item in board.make_list(make, "INCLUDES")]
    sources = [fm1 / "firmware/nes/boot/board.c",
               ROOT / "src/fm1_doom_target.c",
               ROOT / "src/fm1_doom_target_io.c",
               ROOT / "src/fm1_doom_target_libc.c",
               fm1 / "firmware/nes/boot/display_test.c",
               fm1 / "firmware/nes/src/fm1_wl82_keyscan.c",
               fm1 / "firmware/nes/src/fm1_stock_keys.c", blob]
    extras = []
    for index, path in enumerate(sources):
        obj = out / f"extra-{index}.o"
        run([str(board.TC / "clang.exe"), *flags, *defines, *includes,
             "-c", str(path), "-o", str(obj)])
        extras.append(obj)

    libs = [sdk / "include_lib/newlib/pi32v2-lib" / name
            for name in ("libm.a", "libc.a", "libcompiler_rt.a")]
    libs += [sdk / "cpu/wl82/liba" / name for name in
             ("cpu.a", "event.a", "system.a", "cfg_tool.a", "fs.a",
              "common_lib.a", "update.a", "zliblite.a")]
    elf = out / "fm1-doom-candidate.elf"
    link = [str(board.TC / "pi32v2-lto-wrapper.exe"), "-o", str(elf),
            *map(str, board_objects + engine_objects + port_objects + extras),
            "--start-group", *map(str, libs), "--end-group",
            "-T" + str(base / "sdk.ld"), "-M=" + str(out / "fm1-doom-candidate.map"),
            "--wrap=boot_info_init", "--wrap=memory_init",
            "--undefined=memory_init", "--undefined=app_main",
            "--undefined=fm1_doom_embedded_archive",
            "--plugin-opt=mcpu=r3", "--plugin-opt=-mattr=+fprev1",
            "--plugin-opt=-pi32v2-large-program=true",
            "--plugin-opt=-used-symbol-file=" + str(base / "sdk.used")]
    run(link, cwd=out)
    sections = parse_sections(run([str(board.TC / "llvm-objdump.exe"), "-h", str(elf)]))
    nm = run([str(board.TC / "llvm-nm.exe"), "-n", str(elf)])
    image_symbol = re.search(r"^([0-9a-fA-F]+)\s+[A-Za-z]\s+fm1_doom_embedded_archive$", nm, re.M)
    if not image_symbol or ".text" not in sections:
        raise ValueError("embedded archive symbol or flash section is missing")
    text_size, text_vma = sections[".text"]
    image_vma = int(image_symbol.group(1), 16)
    if not text_vma <= image_vma < text_vma + text_size:
        raise ValueError("embedded archive was not linked into XIP text")
    parts = []
    for name in (".text", ".data", ".dynamic_data", ".ram0_data", ".cache_ram_data"):
        part = out / (name[1:] + ".bin")
        run([str(board.TC / "llvm-objcopy.exe"), "-O", "binary", "-j", name,
             str(elf), str(part)])
        parts.append(part.read_bytes())
    image_offset = image_vma - text_vma
    if parts[0][image_offset:image_offset + len(source)] != source:
        raise ValueError("embedded flash bytes differ from the validated FMD1 archive")
    application = out / "fm1-doom-candidate.app.bin"
    application.write_bytes(b"".join(parts))
    heap = {name: int(addr, 16) for addr, name in re.findall(
        r"^([0-9a-fA-F]+)\s+[A-Za-z]\s+(_HEAP_BEGIN|_HEAP_END)$", nm, re.M)}
    ram = sum(sections.get(name, (0, 0))[0]
              for name in (".ram0_data", ".ram0_bss"))
    heap_bytes = heap["_HEAP_END"] - heap["_HEAP_BEGIN"]
    if application.stat().st_size > APP_LIMIT or ram > RAM0_LIMIT or heap_bytes < 32_768:
        raise ValueError("candidate exceeds flash/RAM or leaves under 32 KiB linker heap")
    report = {
        "status": "linked_unflashed_candidate", "flashable": False,
        "sdk_commit": board.SDK_PIN, "archive_sha256": hashlib.sha256(source).hexdigest(),
        "archive_bytes": len(source), "archive_flash_vma": image_vma,
        "application_bytes": application.stat().st_size,
        "application_sha256": hashlib.sha256(application.read_bytes()).hexdigest(),
        "source_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in sources},
        "ram0_data_bytes": sections.get(".ram0_data", (0, 0))[0],
        "ram0_bss_bytes": sections.get(".ram0_bss", (0, 0))[0],
        "linked_heap_bytes_before_runtime": heap_bytes,
        "missing_acceptance": ["physical boot", "runtime heap/stack", "LCD/key behavior",
                               "audio", "30 FPS timing", "rollback and update packaging"],
        "device_operations_performed": False,
    }
    manifest.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
