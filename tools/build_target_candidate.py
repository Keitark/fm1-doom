"""Link an offline FM-1 Doom app from a lawful local FMD1 archive.

This emits an ELF and raw app section image for inspection. It does not make
an update package, invoke a downloader, or contact the device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

from pack_archive import unpack
from pi32_stack import inspect as inspect_stack, usb_diagnostic_budget

ROOT = Path(__file__).resolve().parents[1]
APP_LIMIT = 602_112  # stock V15 application allocation, not whole flash
UBOOT_APP_SLOT_LIMIT = 584_956  # reviewed V14/v32 application slot
RAM0_LIMIT = 523_596  # pinned linker RAM0 window


def task_heap_budget(source: str, usb_dynamic_heap_bytes: int = 0,
                     audio_dynamic_heap_bytes: int = 0) -> dict:
    table = re.search(r"const struct task_info task_info_table\[\]\s*=\s*\{(.*?)\n\};", source, re.S)
    if not table:
        raise ValueError("target task table is missing")
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", table.group(1), flags=re.S)
    entry = re.compile(r'\{\s*"([^\"]+)"\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}\s*,?')
    values = entry.findall(body)
    remaining = entry.sub("", body)
    if not re.fullmatch(r"\s*\{\s*0\s*,\s*0\s*,\s*0\s*,\s*0\s*,\s*0\s*\}\s*,?\s*", remaining):
        raise ValueError("target task budget requires literal stack and queue sizes")
    names = [name for name, _, _, _ in values]
    reviewed = ["app_core", "sys_event", "systimer", "sys_timer", "fm1_doom"]
    reviewed_usb = reviewed[:4] + ["doom_usb"] + reviewed[4:]
    if names not in (reviewed, reviewed_usb):
        raise ValueError("target task table differs from the reviewed Doom/CDC budget")
    if usb_dynamic_heap_bytes and names != reviewed_usb:
        raise ValueError("CDC heap budget requires the USB task entry")
    tasks = [{"name": name, "priority": int(priority),
              "stack_words": int(stack), "stack_bytes": int(stack) * 4,
              "queue_size_bytes": int(queue),
              "queue_allocation_bytes": int(queue) + 92 if int(queue) else 0,
              "task_control_block_bytes": 164}
             for name, priority, stack, queue in values]
    if any(task["stack_words"] == 0 for task in tasks):
        raise ValueError("target task has no stack allocation")
    minimum = sum(task["stack_bytes"] + task["queue_allocation_bytes"] + 164
                  for task in tasks) + 2 * (256 * 4 + 164)
    return {"stack_word_bytes": 4, "queue_size_unit_bytes": 1, "tasks": tasks,
            "idle_task_count": 2, "idle_stack_words_each": 256,
            "idle_task_control_block_bytes_each": 164,
            "minimum_task_heap_bytes": minimum,
            "reviewed_init_allowance_bytes": 800,
            "usb_dynamic_heap_bytes": usb_dynamic_heap_bytes,
            "audio_dynamic_heap_bytes": audio_dynamic_heap_bytes,
            "required_runtime_reserve_bytes": 4096,
            "required_linker_heap_bytes": minimum + 800 + usb_dynamic_heap_bytes + audio_dynamic_heap_bytes + 4096,
            "assumptions": "Pinned SDK task stacks use 32-bit words; allocated queues use qsize bytes plus 92; task control blocks use 164 bytes. USB allocations are measured from the compiled CDC/configuration code; IIS DMA allocations are derived from the pinned driver IR and reviewed channel/point configuration. Endpoint DMA and inflater workspace are static, and lumpinfo is in the Doom zone."}


def usb_heap_allocations(cdc_ir: str, configuration_ir: str) -> dict:
    def allocations(text: str, function: str) -> list[int]:
        body = re.search(r"^define\b[^\n]*@" + re.escape(function) +
                         r"\([^\n]*\).*?^\}", text, re.M | re.S)
        if not body:
            raise ValueError(f"USB allocation measurement function is missing: {function}")
        return [int(size) for size in re.findall(
            r"\bcall\b[^\n]*@(?:zalloc|malloc)\(i32 (\d+)\)", body.group(0))]
    cdc = allocations(cdc_ir, "cdc_register")
    configuration = allocations(configuration_ir, "usb_config")
    if cdc != [200, 64] or configuration != [972]:
        raise ValueError(f"USB allocation sizes differ from the reviewed SDK: CDC {cdc}, config {configuration}")
    return {"cdc_gadget_bytes": cdc[0], "cdc_receive_buffer_bytes": cdc[1],
            "usb_configuration_bytes": configuration[0],
            "total_requested_bytes": sum(cdc + configuration),
            "mutex_storage": "os_mutex_create uses embedded static queue storage",
            "endpoint_dma_storage": "four static 256-byte buffers",
            "allocator_padding": "covered by the required 4096-byte runtime reserve"}


def iis_heap_allocations(iis_ir: str) -> dict:
    body = re.search(r"^define\b[^\n]*@iis_open\([^\n]*\).*?^\}", iis_ir, re.M | re.S)
    if not body:
        raise ValueError("IIS allocation measurement function is missing")
    definitions = dict(re.findall(r"^\s*(%[A-Za-z0-9_.]+) = ([^\n]+)", body.group(0), re.M))
    calls = re.findall(r"\bcall\b[^\n]*@malloc\(i32 (%[A-Za-z0-9_.]+)\)", body.group(0))
    if len(calls) != 1:
        raise ValueError("IIS driver must have exactly one measured DMA allocation")
    product = re.match(r"mul(?: \w+)* i32 (%[A-Za-z0-9_.]+), (%[A-Za-z0-9_.]+)",
                       definitions.get(calls[0], ""))
    if not product:
        raise ValueError("IIS DMA allocation is not the reviewed channels/points product")
    shift = points = None
    for operand in product.groups():
        definition = definitions.get(operand, "")
        factor = re.match(r"shl(?: \w+)* i32 (%[A-Za-z0-9_.]+), 3\b", definition)
        count = re.match(r"zext i16 (%[A-Za-z0-9_.]+) to i32\b", definition)
        if factor:
            channel = definitions.get(factor.group(1), "")
            if re.match(r"zext i8 %ch_num[A-Za-z0-9_.]* to i32\b", channel):
                shift = 8
        if count:
            if re.match(r"load i16, i16\* %sr_points[A-Za-z0-9_.]*\b", definitions.get(count.group(1), "")):
                points = 128
    if shift != 8 or points != 128:
        raise ValueError("IIS allocation operands differ from the reviewed driver")
    return {"enabled_channels": 1, "sr_points": points, "bytes_per_sr_point": shift,
            "total_requested_bytes": points * shift,
            "configuration": "Persistent IIS_PORTC platform data; channel_out=data_width=8; sr_points=128",
            "allocation_formula": "one channel * 128 sr_points * 8 bytes, from pinned iis_open IR"}


def generated_xip_bytes(path: Path, symbol: str) -> bytes:
    source = path.read_text(encoding="utf-8")
    initializer = re.search(r"const\s+uint8_t\s+" + re.escape(symbol) +
                            r"\[\][^{]*\{(.*?)\};", source, re.S)
    if not initializer:
        raise ValueError(f"generated private bank has no byte array: {symbol}")
    tokens = [token.strip() for token in initializer.group(1).split(",")]
    if tokens and not tokens[-1]:
        tokens.pop()
    if any(not re.fullmatch(r"0x[0-9a-fA-F]{1,2}|[0-9]{1,3}", token) for token in tokens):
        raise ValueError(f"generated private bank has an unsupported initializer: {symbol}")
    values = [int(token, 16) if token.startswith("0x") else int(token) for token in tokens]
    if any(value > 255 for value in values):
        raise ValueError(f"generated private bank has a non-byte initializer: {symbol}")
    payload = bytes(values)
    if not payload:
        raise ValueError(f"generated private bank is empty: {symbol}")
    return payload


def source_dependencies(source: Path, include_dirs: list[Path]) -> set[Path]:
    dependencies = set()
    pending = [source.resolve()]
    while pending:
        path = pending.pop()
        if path in dependencies:
            continue
        dependencies.add(path)
        for name in re.findall(r'^\s*#\s*include\s*["<]([^">]+)[">]',
                               path.read_text(encoding="utf-8", errors="replace"), re.M):
            for directory in [path.parent, *include_dirs]:
                header = directory / name
                if header.is_file():
                    pending.append(header.resolve())
                    break
    return dependencies


def check_object_freshness(obj: Path, source: Path, include_dirs: list[Path],
                           configuration: list[Path]) -> set[Path]:
    if not source.is_file():
        raise ValueError(f"target object source is missing: {obj.name}")
    dependencies = source_dependencies(source, include_dirs) | set(configuration)
    if any(obj.stat().st_mtime_ns < path.stat().st_mtime_ns for path in dependencies):
        raise ValueError(f"target object is stale after a source, header or compile configuration change: {obj.name}")
    return dependencies


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


def verify_sdfilesystem(nm: str) -> dict:
    """Keep the three stock configuration drivers and omit unused FAT volumes."""
    symbols = {name: int(address, 16) for address, name in re.findall(
        r"^([0-9a-fA-F]+)\s+[A-Za-z]\s+(\w+)$", nm, re.M)}
    drivers = ("sdfile_vfs_ops", "nor_sdfile_vfs_ops", "sdfile_ext_vfs_ops")
    if any(name not in symbols for name in (*drivers, "_vfs_ops_begin", "_vfs_ops_end")):
        raise ValueError("stock SDFILE configuration drivers are missing")
    begin, end = symbols["_vfs_ops_begin"], symbols["_vfs_ops_end"]
    if end - begin != 3 * 120 or sorted(symbols[name] for name in drivers) != [begin, begin + 120, begin + 240]:
        raise ValueError("VFS registration is not the reviewed three-driver SDFILE closure")
    if any(name in symbols for name in ("fat_sdfile_fat_ops", "jl_fat_vfs_ops", "fat_vfs_ops")):
        raise ValueError("unused FAT drivers remain linked")
    return {"drivers": list(drivers), "registration_bytes": end - begin,
            "fat_directory_extension": "unsupported", "fat_volume_support": False}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="local 4 KiB-block FMD1 menu archive")
    parser.add_argument("--fm1-root", type=Path, required=True)
    parser.add_argument("--sound-bank", type=Path,
                        default=ROOT / "build/sound-bank/fm1_doom_sound_bank.c",
                        help="private generated XIP shareware SFX bank source")
    parser.add_argument("--music-bank", type=Path,
                        default=ROOT / "build/music-bank/music_score.c",
                        help="private generated XIP E1M1 music bank source")
    parser.add_argument("--genmidi-bank", type=Path,
                        default=ROOT / "build/opl-bank/genmidi_bank.c",
                        help="private original GENMIDI patch closure for E1M1")
    args = parser.parse_args()
    out = ROOT / "build/target-candidate"
    out.mkdir(parents=True, exist_ok=True)
    manifest = out / "build-manifest.json"
    manifest.write_text(json.dumps({"status": "build_in_progress_or_failed",
                                    "flashable": False}) + "\n", encoding="utf-8")
    fm1 = args.fm1_root.resolve()
    sys.path.insert(0, str(fm1 / "firmware/nes"))
    usb = fm1 / "firmware/usb-diag"
    sys.path.insert(0, str(usb))
    import build_boot as board
    import vendor_overlay

    sdk = board.SDK.resolve()
    if sdk != (fm1 / "references/source/fw-AC79_AIoT_SDK").resolve():
        parser.error("FM-1 SDK path is outside the expected checkout")
    if run(["git", "-C", str(sdk), "rev-parse", "HEAD"]).strip() != board.SDK_PIN:
        parser.error("FM-1 SDK revision differs from the reviewed pin")
    if run(["git", "-C", str(sdk), "status", "--porcelain"]).strip():
        parser.error("FM-1 SDK checkout is dirty")
    source = args.archive.read_bytes()
    if len(source) > 196_608 or source[4:8] != (4096).to_bytes(4, "little"):
        parser.error("archive must use 4 KiB FMD1 blocks and fit the 192 KiB app-data cap")
    if not unpack(source).startswith(b"IWAD"):
        parser.error("FMD1 payload is not an IWAD")
    private_banks = {
        "sound": (args.sound_bank.resolve(), "fm1_doom_sound_bank"),
        "music": (args.music_bank.resolve(), "fm1_doom_music_score"),
        "genmidi": (args.genmidi_bank.resolve(), "fm1_doom_genmidi_bank"),
    }
    bank_payloads = {}
    for name, (path, symbol) in private_banks.items():
        if not path.is_file():
            parser.error(f"generate the private {name} bank before linking: {path}")
        bank_payloads[name] = generated_xip_bytes(path, symbol)

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
    make = board.MAKE.read_text(encoding="utf-8")
    dependency_includes = [ROOT / "include", generated, sdk / "apps/common"]
    dependency_includes += [board.sdk_path(item[2:])
                            for item in board.make_list(make, "INCLUDES")]
    common_configuration = [ROOT / "CMakeLists.txt", ROOT / "tools/make_lowres_engine.py",
                            generated / "doomgeneric.vcxproj", board.MAKE]
    source_closure = set(common_configuration + [ROOT / "tools/build_target_candidate.py",
                         ROOT / "tools/pi32_stack.py",
                         ROOT / "tools/compile_target_engine.py", ROOT / "tools/compile_target_port.py",
                         usb / "vendor_overlay.py"])
    source_closure.update(ROOT / "tools" / name
                          for name in ("make_sound_bank.py", "make_music_score.py", "make_genmidi_bank.py"))
    for name, (path, _) in private_banks.items():
        bank_manifest = path.parent / "manifest.json"
        if bank_manifest.is_file():
            metadata = json.loads(bank_manifest.read_text(encoding="utf-8"))
            expected_source = metadata.get("generated_source_sha256")
            if expected_source and expected_source not in (
                    hashlib.sha256(path.read_bytes()).hexdigest(),
                    hashlib.sha256(path.read_text(encoding="utf-8").encode()).hexdigest()):
                raise ValueError(f"private generated bank differs from its manifest: {path}")
            if name == "music" and metadata.get("score_sha256") != hashlib.sha256(bank_payloads[name]).hexdigest():
                raise ValueError("private music score bytes differ from the roundtrip manifest")
            if name == "music" and metadata.get("roundtrip_verified") is not True:
                raise ValueError("private music score has no verified event roundtrip")
            if name == "sound" and metadata.get("payload_bytes") != len(bank_payloads[name]):
                raise ValueError("private sound bank size differs from its manifest")
            if name == "genmidi" and (
                    metadata.get("payload_sha256") != hashlib.sha256(bank_payloads[name]).hexdigest()
                    or metadata.get("payload_bytes") != len(bank_payloads[name])
                    or metadata.get("roundtrip_verified") is not True):
                raise ValueError("private GENMIDI bank differs from its original-patch proof")
            source_closure.add(bank_manifest)
        elif name == "genmidi":
            raise ValueError("private GENMIDI bank has no verified patch manifest")
    for obj in engine_objects:
        source_path = generated / obj.name.removesuffix(".o")
        source_closure.update(check_object_freshness(obj, source_path, dependency_includes,
                              common_configuration + [ROOT / "tools/compile_target_engine.py"]))
    for obj in port_objects:
        source_path = ROOT / "src" / (obj.stem + ".c")
        source_closure.update(check_object_freshness(obj, source_path, dependency_includes,
                              common_configuration + [ROOT / "tools/compile_target_port.py"]))
    blob = out / "embedded_archive.c"
    with blob.open("w", encoding="ascii", newline="\n") as file:
        file.write("#include <stdint.h>\n"
                   "const uint8_t fm1_doom_embedded_archive[] __attribute__((aligned(4),used)) = {\n")
        for pos in range(0, len(source), 16):
            file.write("    " + ",".join(f"0x{value:02x}" for value in source[pos:pos + 16]) + ",\n")
        file.write("};\nconst uint32_t fm1_doom_embedded_archive_len = "
                   + str(len(source)) + ";\n")

    flags = board.make_list(make, "CFLAGS")
    defines = board.make_list(make, "DEFINES")
    defines += ["-DFM1_DOOM_SOURCE_WIDTH=160", "-DFM1_DOOM_SOURCE_HEIGHT=100",
                "-DFM1_NES_PLAYER=1", "-DFM1_TARGET_PI32V2=1",
                "-DFM1_USB_CONTROLLER=0", "-DFM1_KEYSCAN_DMA2=1",
                "-DFM1_KEYSCAN_IRQ=1", "-DFM1_KEYSCAN_PACED=1",
                "-DFM1_LCD_STOCK_FILL=1", "-DFM1_LCD_STOCK_DMA=1",
                "-DFM1_LCD_STOCK_SEQUENCE=1"]
    candidate_includes = [ROOT / "include", ROOT / "build/lowres-source", usb,
                          fm1 / "firmware/nes/boot", fm1 / "firmware/nes/include",
                          sdk / "apps/common", sdk / "apps/common/usb",
                          sdk / "apps/common/usb/device"]
    candidate_includes += [board.sdk_path(item[2:])
                           for item in board.make_list(make, "INCLUDES")]
    candidate_includes.append(ROOT / "vendor/emu8950")
    includes = ["-I" + str(path) for path in candidate_includes]
    sources = [fm1 / "firmware/nes/boot/board.c",
               ROOT / "src/fm1_doom_target.c",
               ROOT / "src/fm1_doom_target_io.c",
               ROOT / "src/fm1_doom_target_libc.c",
               ROOT / "src/fm1_doom_printf.c",
               fm1 / "firmware/nes/boot/display_test.c",
               fm1 / "firmware/nes/src/fm1_wl82_keyscan.c",
               fm1 / "firmware/nes/src/fm1_stock_keys.c", blob]
    sources += [ROOT / "src/fm1_doom_usb.c", ROOT / "src/fm1_doom_usb_protocol.c"]
    audio_source = ROOT / "src/fm1_doom_sound.c"
    audio_text = audio_source.read_text(encoding="utf-8")
    for statement in ("memset(&audio_pd, 0, sizeof(audio_pd));",
                      "audio_pd.port_sel = IIS_PORTC;",
                      "audio_pd.channel_out = audio_pd.data_width = 8;",
                      "audio_pd.sr_points = 128;",
                      "rc = iis_open(&audio_pd, 0);"):
        if audio_text.count(statement) != 1:
            raise ValueError("audio platform configuration differs from the measured IIS allocation")
    sources += [audio_source, ROOT / "src/fm1_doom_music.c", ROOT / "src/fm1_doom_opl.c",
                ROOT / "vendor/emu8950/emu8950.c",
                fm1 / "firmware/nes/src/fm1_audio_queue.c",
                fm1 / "firmware/nes/src/fm1_volume.c"]
    sources += [path for path, _ in private_banks.values()]
    sources += [usb / name for name in ("descriptors.c", "usb_policy.c", "dma.c",
                                       "rx_channel.c", "boot_entry.c")]
    sources.append(sdk / "apps/common/usb/usb_config.c")
    overlays = {}
    for name, transform in (("cdc.c", vendor_overlay.cdc),
                            ("usb_device.c", vendor_overlay.device),
                            ("msd_upgrade.c", vendor_overlay.boot_entry)):
        original = sdk / "apps/common/usb/device" / name
        target = out / ("fm1-" + name)
        target.write_text(transform(original.read_text(encoding="utf-8")), encoding="utf-8")
        sources.append(target)
        source_closure.add(original)
        overlays[str(original)] = hashlib.sha256(original.read_bytes()).hexdigest()
    for path, name in ((sdk / "cpu/wl82/sdk_ld.c", "sdk.ld"),
                       (sdk / "cpu/wl82/sdk_used_list.c", "sdk.used")):
        source_closure.update(source_dependencies(path, candidate_includes))
        run([str(board.TC / "clang.exe"), *flags, *defines, *includes,
             "-D__LD__", "-E", "-P", str(path), "-o", str(out / name)])
    used = out / "sdk.used"
    retained = ["memory_init", "app_main", "fm1_doom_usb_task", "cdc_read_data",
                "cdc_write_data", "fm1_cdc_ready", "fm1_usb_device_descriptor",
                "fm1_usb_config_descriptor", "fm1_usb_rx_irq", "go_mask_usb_updata",
                "nvram_set_boot_state", "fm1_wl82_keyscan_async_raw", "jiffies_half_msec"]
    retained += ["fm1_sound_module", "fm1_music_module", "fm1_doom_sound_init",
                 "fm1_doom_sound_shutdown", "iis_open", "iis_close", "iis_irq_handler",
                 "iis_set_dec_data_handler", "iis_set_sample_rate", "iis_channel_on",
                 "iis_channel_off", "fm1_doom_sound_bank", "fm1_doom_sound_entries",
                 "fm1_doom_sound_entry_count", "fm1_doom_sound_bank_bytes",
                 "fm1_doom_music_score", "fm1_doom_music_score_len",
                 "fm1_doom_genmidi_bank", "fm1_doom_genmidi_bank_len"]
    retained += ["sprintf", "snprintf", "vsprintf", "vsnprintf", "print", "printf", "vprintf", "perror"]
    used.write_text(used.read_text() + "\n" + "\n".join(retained) + "\n")
    ld = (out / "sdk.ld").read_text()
    for old, new in (("*(.data)", "*(.data .data.*)"), ("*(.bss)", "*(.bss .bss.*)")):
        ld = vendor_overlay.once(ld, old, new)
    for section in (".syscfg.2.ops", ".syscfg.1.ops"):
        ld = vendor_overlay.once(ld, "*(" + section + ")", "/* no persistent cfg repair */")
    ld += "\nSECTIONS { /DISCARD/ : { *(.syscfg.2.ops) *(.syscfg.1.ops) } }\n"
    (out / "sdk.ld").write_text(ld)
    source_closure.update((out / "sdk.ld", used))
    extras = []
    usb_ir = {}
    for index, path in enumerate(sources):
        source_closure.update(source_dependencies(path, candidate_includes))
        obj = out / f"extra-{index}.o"
        run([str(board.TC / "clang.exe"), *flags, *defines, *includes,
             "-c", str(path), "-o", str(obj)])
        extras.append(obj)
        if path.name in ("fm1-cdc.c", "usb_config.c"):
            ir = out / (path.stem + "-allocations.ll")
            run([str(board.TC / "clang.exe"), "-x", "ir", "-target", "pi32v2",
                 "-S", "-emit-llvm", str(obj), "-o", str(ir)])
            usb_ir[path.name] = ir.read_text()
    usb_allocations = usb_heap_allocations(usb_ir["fm1-cdc.c"], usb_ir["usb_config.c"])
    cpu_library = sdk / "cpu/wl82/liba/cpu.a"
    iis_object = out / "iis-driver-audit.o"
    extracted = subprocess.run([str(board.TC / "llvm-ar.exe"), "p", str(cpu_library), "iis.c.o"],
                               capture_output=True)
    if extracted.returncode or not extracted.stdout.startswith(b"BC\xc0\xde"):
        raise ValueError("cannot extract the pinned IIS driver bitcode for allocation measurement")
    iis_object.write_bytes(extracted.stdout)
    iis_ir = out / "iis-driver-allocations.ll"
    run([str(board.TC / "clang.exe"), "-x", "ir", "-target", "pi32v2", "-S", "-emit-llvm",
         str(iis_object), "-o", str(iis_ir)])
    audio_allocations = iis_heap_allocations(iis_ir.read_text(encoding="utf-8"))
    source_closure.update((cpu_library, iis_object, iis_ir))
    task_budget = task_heap_budget((ROOT / "src/fm1_doom_target.c").read_text(encoding="utf-8"),
                                   usb_allocations["total_requested_bytes"],
                                   audio_allocations["total_requested_bytes"])
    task_budget["usb_allocations"] = usb_allocations
    task_budget["audio_allocations"] = audio_allocations

    libs = [sdk / "include_lib/newlib/pi32v2-lib" / name
            for name in ("libm.a", "libc.a", "libcompiler_rt.a")]
    libs += [sdk / "cpu/wl82/liba" / name for name in
             ("cpu.a", "event.a", "system.a", "cfg_tool.a", "fs.a",
              "common_lib.a", "update.a", "zliblite.a")]
    elf = out / "fm1-doom-candidate.elf"
    link = [str(board.TC / "pi32v2-lto-wrapper.exe"), "-o", str(elf),
            *map(str, board_objects + engine_objects + port_objects + extras),
            "--start-group", *map(str, libs), "--end-group",
            "-T" + str(out / "sdk.ld"), "-M=" + str(out / "fm1-doom-candidate.map"),
            "--wrap=boot_info_init", "--wrap=memory_init",
            "--undefined=memory_init", "--undefined=app_main",
            "--undefined=fm1_doom_embedded_archive",
            "--plugin-opt=mcpu=r3", "--plugin-opt=-mattr=+fprev1",
            "--plugin-opt=-pi32v2-large-program=true",
            "--plugin-opt=-used-symbol-file=" + str(used)]
    run(link, cwd=out)
    elf_bytes = elf.read_bytes()
    if elf_bytes[:7] != b"\x7fELF\x01\x01\x01" or len(elf_bytes) < 52:
        raise ValueError("target is not a little-endian ELF32 executable")
    elf_type, elf_machine, elf_version, elf_entry = struct.unpack_from("<HHII", elf_bytes, 16)
    if (elf_type, elf_machine, elf_version, elf_entry) != (2, 0xF1, 1, 0x02000120):
        raise ValueError("ELF machine or FM-1 UBOOT XIP entry differs")
    sections = parse_sections(run([str(board.TC / "llvm-objdump.exe"), "-h", str(elf)]))
    if sections.get(".text", (0, 0))[1] != 0x02000120:
        raise ValueError("XIP text does not start at the UBOOT application entry")
    for name in (".data", ".bss", ".dynamic_data", ".dynamic_bss"):
        if sections.get(name, (0, 0))[0]:
            raise ValueError(f"unsupported external RAM section is nonempty: {name}")
    nm = run([str(board.TC / "llvm-nm.exe"), "-n", str(elf)])
    filesystem = verify_sdfilesystem(nm)
    usb_stack_report = inspect_stack(elf, board.TC)
    usb_stack_bytes = next(task["stack_bytes"] for task in task_budget["tasks"]
                           if task["name"] == "doom_usb")
    usb_stack_budget = usb_diagnostic_budget(usb_stack_report, usb_stack_bytes)
    (out / "usb-stack-audit.json").write_text(json.dumps(usb_stack_report, indent=2) + "\n", encoding="utf-8")
    for name in retained:
        if not re.search(r"^[0-9a-fA-F]+\s+[A-Za-z]\s+" + re.escape(name) + r"$", nm, re.M):
            raise ValueError(f"required Doom/CDC link symbol is missing: {name}")
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
    bank_link_info = {}
    for name, (path, symbol) in private_banks.items():
        match = re.search(r"^([0-9a-fA-F]+)\s+[A-Za-z]\s+" + re.escape(symbol) + r"$", nm, re.M)
        if not match:
            raise ValueError(f"private bank symbol is missing: {symbol}")
        address = int(match.group(1), 16)
        offset = address - text_vma
        payload = bank_payloads[name]
        if not 0 <= offset <= text_size - len(payload):
            raise ValueError(f"private bank is outside XIP text: {symbol}")
        if parts[0][offset:offset + len(payload)] != payload:
            raise ValueError(f"linked private bank bytes differ from their generated source: {symbol}")
        bank_link_info[name] = {"source": str(path), "bytes": len(payload),
                                "sha256": hashlib.sha256(payload).hexdigest(), "flash_vma": address}
    for symbol in ("fm1_doom_sound_entries", "fm1_doom_sound_entry_count", "fm1_doom_sound_bank_bytes",
                   "fm1_doom_music_score_len", "fm1_doom_genmidi_bank_len"):
        match = re.search(r"^([0-9a-fA-F]+)\s+[A-Za-z]\s+" + symbol + r"$", nm, re.M)
        if not match or not text_vma <= int(match.group(1), 16) < text_vma + text_size:
            raise ValueError(f"private sound metadata is outside XIP text: {symbol}")
    application = out / "fm1-doom-candidate.app.bin"
    application.write_bytes(b"".join(parts))
    heap = {name: int(addr, 16) for addr, name in re.findall(
        r"^([0-9a-fA-F]+)\s+[A-Za-z]\s+(_HEAP_BEGIN|_HEAP_END)$", nm, re.M)}
    ram = sum(sections.get(name, (0, 0))[0]
              for name in (".ram0_data", ".ram0_bss"))
    heap_bytes = heap["_HEAP_END"] - heap["_HEAP_BEGIN"]
    if application.stat().st_size > min(APP_LIMIT, UBOOT_APP_SLOT_LIMIT) or ram > RAM0_LIMIT:
        raise ValueError("candidate exceeds flash or static RAM")
    if heap_bytes < task_budget["required_linker_heap_bytes"]:
        raise ValueError(f"candidate linker heap {heap_bytes} is below the reviewed startup requirement {task_budget['required_linker_heap_bytes']}")
    task_budget["runtime_reserve_after_reviewed_startup_bytes"] = heap_bytes - task_budget["minimum_task_heap_bytes"] - task_budget["reviewed_init_allowance_bytes"] - task_budget["usb_dynamic_heap_bytes"] - task_budget["audio_dynamic_heap_bytes"]
    uboot_app = out / "app.bin"
    uboot_app.write_bytes(application.read_bytes())
    if uboot_app.read_bytes() != b"".join(parts):
        raise ValueError("UBOOT app.bin differs from the five SDK section binaries")
    report = {
        "status": "uboot_app_input_ready_unflashed", "flashable": True,
        "hardware_boot_verified": False,
        "sdk_commit": board.SDK_PIN, "archive_sha256": hashlib.sha256(source).hexdigest(),
        "archive_bytes": len(source), "archive_flash_vma": image_vma,
        "application_bytes": application.stat().st_size,
        "application_sha256": hashlib.sha256(application.read_bytes()).hexdigest(),
        "elf_sha256": hashlib.sha256(elf_bytes).hexdigest(),
        "uboot_app_file": str(uboot_app),
        "uboot_app_input_ready": True,
        "uboot_app_format": "plain WL82 SDK app.bin for isd_download -app; not an encoded raw-flash image",
        "uboot_xip_entry": hex(elf_entry),
        "reviewed_app_slot_limit_bytes": UBOOT_APP_SLOT_LIMIT,
        "source_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in sorted(source_closure)},
        "ram0_data_bytes": sections.get(".ram0_data", (0, 0))[0],
        "ram0_bss_bytes": sections.get(".ram0_bss", (0, 0))[0],
        "linked_heap_bytes_before_runtime": heap_bytes,
        "startup_heap_budget": task_budget,
        "filesystem": filesystem,
        "usb_diagnostic_stack": usb_stack_budget,
        "audio": {"output": "IIS_PORTC ALINK0 channel 3 signed 24-bit stereo at 44100 Hz",
                  "sfx_voices": 2, "private_xip_banks": bank_link_info,
                  "dynamic_dma_bytes": audio_allocations["total_requested_bytes"]},
        "usb_controller": 0,
        "usb_recovery": "CDC status, cooperative stop and guarded IRQ-context UBOOT entry",
        "keyscan_mode": "DMA2 IRQ with 1 ms pacing",
        "keyscan_clock": "SDK hardware-interpolated half-millisecond clock",
        "vendor_overlay_sources": overlays,
        "missing_acceptance": ["physical boot", "runtime heap/stack", "LCD/key behavior",
                               "audio", "30 FPS timing", "physical rollback and write validation"],
        "device_operations_performed": False,
    }
    manifest.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
