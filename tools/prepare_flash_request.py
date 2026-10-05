"""Offline, private application update request for the existing FM-1 broker.

No device I/O. Keeps the verified baseline boot/config/tail and never writes a key.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]


def require(ok: bool, message: str) -> None:
    if not ok:
        raise ValueError(message)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_session_baseline(session: Path) -> tuple[bytes, str]:
    """Accept only the protected session's complete, verified current image."""
    session = session.resolve()
    state = json.loads((session / "state.json").read_text(encoding="utf-8-sig"))
    digest = state.get("baseline_sha256")
    require(isinstance(digest, str) and re.fullmatch(r"[0-9a-f]{64}", digest) is not None,
            "protected session baseline SHA-256 is invalid")
    require(state.get("blocked") is False and not state.get("reset_pending")
            and not state.get("needs_observation"),
            "protected session is not an idle verified baseline")
    filename = state.get("baseline_file")
    require(isinstance(filename, str) and bool(filename),
            "protected session baseline file is missing")
    base_file = (session / filename).resolve()
    require(base_file.is_relative_to(session), "baseline escaped protected session")
    baseline = base_file.read_bytes()
    require(len(baseline) == 1_048_576 and sha(baseline) == digest,
            "full readback bytes differ from the protected session baseline")
    receipt_name = "baseline-deployment.json" if base_file.name == "baseline.bin" else "deployment.json"
    receipt_file = (base_file.parent / receipt_name).resolve()
    require(receipt_file.is_relative_to(session), "receipt escaped protected session")
    receipt = json.loads(receipt_file.read_text(encoding="utf-8-sig"))
    require(receipt.get("status") in ("written_and_readback_verified",
                                      "written_and_twice_readback_verified")
            and receipt.get("full_readback_sha256") == digest,
            "baseline lacks a matching full-readback receipt")
    return baseline, digest


def verify_stock_baseline(pack, baseline: bytes) -> None:
    """Keep the original V14 partitions; the entire app slot may hold prior code."""
    stock = pack.V14_BACKUP.read_bytes()
    require(sha(stock) == pack.V14_SHA, "original V14 reference identity changed")
    before, original_area, original_entries, _ = pack.stock_layout(stock)
    after, area, entries, _, address, _ = pack._decode_layout(baseline)
    require(address == pack.AREA and len(entries) == len(original_entries),
            "baseline stock directory shape changed")
    require(original_entries[0].size == 584_956
            and 0 < entries[0].size <= original_entries[0].size
            and entries[0].data_offset == pack.APP,
            "baseline application exceeds the reviewed stock slot")
    require(after[pack.AREA + 4:pack.AREA + 32]
                == before[pack.AREA + 4:pack.AREA + 32]
            and after[pack.AREA + 36:pack.AREA + 40]
                == before[pack.AREA + 36:pack.AREA + 40]
            and after[pack.AREA + 44:pack.AREA + 64]
                == before[pack.AREA + 44:pack.AREA + 64]
            and all(after[e.hdr_off:e.hdr_off + 32]
                    == before[o.hdr_off:o.hdr_off + 32]
                    for o, e in zip(original_entries[1:], entries[1:])),
            "baseline changed stock partition offsets or directory fields")
    require(baseline[:pack.AREA] == stock[:pack.AREA]
            and baseline[pack.AREA + 64:pack.APP] == stock[pack.AREA + 64:pack.APP]
            and baseline[pack.APP + original_entries[0].size:]
                == stock[pack.APP + original_entries[0].size:],
            "baseline changed stock boot, config, or reserved partitions")
    app, info, layout = pack.decode_application(baseline)
    require(len(app) == entries[0].size and info["data_crc_valid"]
            and layout["entry_point"] == "0x2000120"
            and area.size == original_area.size
            and pack.jl_crc16(after[pack.AREA + 32:pack.AREA + area.size]) == area.data_crc,
            "baseline application or area CRC is invalid")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fm1-root", required=True, type=Path)
    parser.add_argument("--session", required=True, type=Path,
                        help="protected session holding a verified full readback")
    parser.add_argument("--build", type=Path, default=ROOT / "build/target-candidate",
                        help="directory containing app.bin and its validated build manifest")
    args = parser.parse_args()
    fm1 = args.fm1_root.resolve()
    sys.path.insert(0, str(fm1 / "firmware/nes"))
    import build_stock_image as pack
    import plan_flash_update as planner

    session = args.session.resolve()
    baseline, baseline_sha = load_session_baseline(session)
    verify_stock_baseline(pack, baseline)

    build = args.build.resolve()
    built = json.loads((build / "build-manifest.json").read_text(encoding="utf-8"))
    require(built.get("status") == "uboot_app_input_ready_unflashed"
            and built.get("uboot_app_input_ready") is True
            and built.get("hardware_boot_verified") is False,
            "Doom app build is not a validated UBOOT input")
    for name, digest in built["source_sha256"].items():
        require(sha(Path(name).read_bytes()) == digest, "stale build source: " + name)
    app = (build / "app.bin").read_bytes()
    elf = (build / "fm1-doom-candidate.elf").read_bytes()
    require(sha(app) == built["application_sha256"]
            and sha(elf) == built["elf_sha256"]
            and app == (build / "fm1-doom-candidate.app.bin").read_bytes(),
            "Doom app/ELF differs from the frozen build")
    require(0 < len(app) <= built["reviewed_app_slot_limit_bytes"] == 584_956,
            "Doom app exceeds the reviewed application slot")

    plain, area, entries, _, address, key = pack._decode_layout(baseline)
    baseline_plain = bytes(plain)
    require(address == pack.AREA and area.size == 585_627
            and entries[0].data_offset == pack.APP
            and entries[1].data_offset == 0x92E1C,
            "baseline application directory changed")
    plain[pack.APP:pack.APP + len(app)] = app
    struct.pack_into("<H", plain, pack.AREA + 34, pack.jl_crc16(app))
    struct.pack_into("<I", plain, pack.AREA + 40, len(app))
    struct.pack_into("<H", plain, pack.AREA + 32,
                     pack.jl_crc16(plain[pack.AREA + 34:pack.AREA + 64]))
    struct.pack_into("<H", plain, pack.AREA + 2,
                     pack.jl_crc16(plain[pack.AREA + 32:pack.AREA + area.size]))
    struct.pack_into("<H", plain, pack.AREA,
                     pack.jl_crc16(plain[pack.AREA + 2:pack.AREA + 32]))
    candidate = bytearray(baseline)
    for start, end in ((pack.AREA, pack.AREA + 64),
                       (pack.APP, (pack.APP + len(app) + 31) & ~31)):
        pack.jl_sfc_cipher(plain, start, end - start, pack.AREA, key)
        candidate[start:end] = plain[start:end]
    candidate = bytes(candidate)
    decoded, info, _ = pack.decode_application(candidate)
    require(decoded == app and info["data_crc_valid"], "Doom application round trip failed")
    require(candidate[:pack.AREA] == baseline[:pack.AREA]
            and candidate[pack.AREA + 64:pack.APP] == baseline[pack.AREA + 64:pack.APP]
            and candidate[pack.APP + len(app):] == baseline[pack.APP + len(app):],
            "candidate changed boot, config, reserved, or unused app bytes")
    decoded_plain, new_area, new_entries, _, new_base, _ = pack._decode_layout(candidate)
    require(new_base == pack.AREA and new_area.size == area.size
            and new_entries[0].size == len(app)
            and new_entries[0].data_offset == pack.APP
            and len(entries) == len(new_entries)
            and all(decoded_plain[e.hdr_off:e.hdr_off + 32]
                    == baseline_plain[e.hdr_off:e.hdr_off + 32] for e in entries[1:])
            and pack.jl_crc16(decoded_plain[pack.AREA + 32:pack.AREA + new_area.size])
                == new_area.data_crc,
            "candidate directory changed outside the application record")
    sectors = planner.changed_sectors(baseline, candidate)
    require(sectors and sectors[-1] == pack.AREA
            and all(pack.AREA <= offset < 0x93000 for offset in sectors),
            "candidate sector scope changed")
    image = bytearray(baseline)
    for operation in planner.model_operations(baseline, candidate, sectors):
        planner.model_apply(image, operation)
    require(image == candidate, "offline sector write simulation failed")
    for operation in planner.model_operations(bytes(image), baseline, sectors):
        planner.model_apply(image, operation)
    require(image == baseline, "offline sector restore simulation failed")

    out = ROOT / "build/flash-request" / sha(candidate)
    out.mkdir(parents=True, exist_ok=True)
    image_file = out / "fm1-doom-private.bin"
    request_file = out / "flash-request.json"
    report_file = out / "manifest.json"
    if image_file.exists():
        require(image_file.read_bytes() == candidate, "image output collision")
    else:
        image_file.write_bytes(candidate)
    request = {"op": "plan", "baseline_sha256": baseline_sha,
               "sha256": sha(candidate), "image": base64.b64encode(candidate).decode("ascii")}
    request_file.write_text(json.dumps(request), encoding="utf-8")
    report = {"status": "offline_plan_ready", "baseline_sha256": baseline_sha,
              "candidate_sha256": sha(candidate), "application_sha256": sha(app),
              "application_bytes": len(app), "sector_count": len(sectors),
              "directory_sector_last": True, "update_and_restore_model_passed": True,
              "preserved_stock_boot_config_tail": True,
              "image": str(image_file), "request": str(request_file),
              "device_operations_performed": False, "hardware_boot_verified": False}
    report_file.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
