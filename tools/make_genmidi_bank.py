"""Extract byte-exact E1M1 DOS OPL instrument patches from a local IWAD.

Private generated data must stay in ignored build/. Instrument names and
unused songs/patches are omitted; no active operator/register byte is changed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from make_music_score import mus_events, wad_music


def wad_genmidi(data):
    if len(data) < 12 or data[:4] not in (b"IWAD", b"PWAD"):
        raise ValueError("not a WAD")
    count, directory = struct.unpack_from("<II", data, 4)
    if directory < 12 or directory > len(data) or count > (len(data) - directory) // 16:
        raise ValueError("truncated WAD directory")
    result = None
    for i in range(count):
        offset, size, name = struct.unpack_from("<II8s", data, directory + i * 16)
        if offset > len(data) or size > len(data) - offset:
            raise ValueError("WAD lump exceeds bounds")
        if name.rstrip(b"\0").upper() == b"GENMIDI":
            result = data[offset:offset + size]
    if result is None:
        raise ValueError("original GENMIDI is absent")
    return result


def make_bank(genmidi, events):
    if len(genmidi) < 8 + 175 * 36 or genmidi[:8] != b"#OPL_II#":
        raise ValueError("invalid GENMIDI instrument table")
    programs = [0] * 16
    used = set()
    for _, kind, channel, a, b in events:
        if kind == 4 and a == 0:
            programs[channel] = b
        if kind != 1 or not b:
            continue
        if channel == 15 and not 35 <= a <= 81:
            raise ValueError("unsupported original percussion note")
        index = 128 + a - 35 if channel == 15 else programs[channel]
        if not 0 <= index < 175:
            raise ValueError("unsupported original instrument")
        used.add(index)
    if not used:
        raise ValueError("score has no instruments")
    rows = sorted(used)
    bank = bytearray(struct.pack("<4sHH", b"FMGM", len(rows), 0))
    for index in rows:
        bank.append(index)
        bank.extend(genmidi[8 + index * 36:8 + (index + 1) * 36])
    return bytes(bank), rows


def expand_bank(bank):
    if len(bank) < 8:
        raise ValueError("truncated patch bank")
    magic, count, reserved = struct.unpack_from("<4sHH", bank)
    if magic != b"FMGM" or not 1 <= count <= 175 or reserved or len(bank) != 8 + count * 37:
        raise ValueError("invalid patch bank")
    result = {}
    for i in range(count):
        index = bank[8 + i * 37]
        if index >= 175 or index in result or (result and index <= max(result)):
            raise ValueError("unordered patch bank")
        result[index] = bank[9 + i * 37:45 + i * 37]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="local original IWAD containing GENMIDI and D_E1M1")
    parser.add_argument("output", type=Path, help="ignored build/opl-bank output directory")
    args = parser.parse_args()
    source = args.input.read_bytes()
    original = wad_genmidi(source)
    bank, patches = make_bank(original, mus_events(wad_music(source)))
    assert all(data == original[8 + index * 36:8 + (index + 1) * 36]
               for index, data in expand_bank(bank).items())
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "genmidi_bank.fmgm").write_bytes(bank)
    rows = [",".join(str(x) for x in bank[i:i + 24]) for i in range(0, len(bank), 24)]
    (args.output / "genmidi_bank.c").write_text(
        "/* Private original GENMIDI operator bytes; do not redistribute. */\n#include <stdint.h>\n"
        "const uint8_t fm1_doom_genmidi_bank[]={\n" + ",\n".join(rows) + "\n};\n"
        f"const uint32_t fm1_doom_genmidi_bank_len={len(bank)}u;\n", encoding="utf-8")
    manifest = {"source": str(args.input.resolve()), "source_sha256": hashlib.sha256(source).hexdigest(),
                "genmidi_sha256": hashlib.sha256(original).hexdigest(), "patch_indices": patches,
                "original_patch_bytes": len(patches) * 36, "bank_bytes": len(bank),
                "bank_sha256": hashlib.sha256(bank).hexdigest(),
                "payload_bytes": len(bank), "payload_sha256": hashlib.sha256(bank).hexdigest(),
                "generated_source_sha256": hashlib.sha256((args.output / "genmidi_bank.c").read_bytes()).hexdigest(),
                "roundtrip_verified": True}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
