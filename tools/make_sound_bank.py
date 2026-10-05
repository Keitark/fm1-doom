"""Create a small private shareware IMA sound bank from a local WHX or IWAD.

Generated samples are user-local game data and must remain outside Git.
WHX sounds are copied unchanged. Native IWAD PCM8 sounds are encoded locally.
Only original pistol, oof, pickup and optional menu-switch sounds are included.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SOUNDS = (b"DSPISTOL", b"DSOOF", b"DSITEMUP")
STEPS = (7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,
         50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,
         230,253,279,307,337,371,408,449,494,544,598,658,724,796,
         876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,
         2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,
         7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
         18500,20350,22385,24623,27086,29794,32767)


def reference_pcm(data: bytes) -> list[int]:
    """Offline reference decoder following WHX low/high nibble block order."""
    validate_sound(data)
    output = []
    for offset in range(8, len(data), 128):
        block = data[offset:offset + 128]
        predictor, index = struct.unpack_from("<hB", block)
        output.append(predictor)
        for value in block[4:]:
            for shift in (0, 4):
                code = value >> shift & 15
                step = STEPS[index]
                delta = (step >> 3) + ((step >> 2) if code & 1 else 0)
                delta += (step >> 1) if code & 2 else 0
                delta += step if code & 4 else 0
                predictor += -delta if code & 8 else delta
                predictor = max(-32768, min(32767, predictor))
                index = max(0, min(88, index + (-1,-1,-1,-1,2,4,6,8)[code & 7]))
                output.append(predictor)
    return output


def pcm_fnv(samples: list[int]) -> int:
    value = 2166136261
    for sample in samples:
        for byte in struct.pack("<h", sample):
            value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def emit_reference(sounds: list[tuple[bytes, bytes]]) -> str:
    rows = ['/* Host-only reference hashes; no PCM waveforms are stored here. */',
            'static const struct { uint32_t samples, fnv; } sound_reference[] = {']
    for _, raw in sounds:
        samples = reference_pcm(raw)
        rows.append(f'    {{{len(samples)}u, {pcm_fnv(samples)}u}},')
    return "\n".join(rows + ['};', ''])


def validate_sound(data: bytes) -> None:
    if len(data) < 12 or data[:2] != b"\x03\x80":
        raise ValueError("not bounded WHX IMA sound data")
    rate, samples = struct.unpack_from("<HI", data, 2)
    if rate not in (8000, 11025) or not 1 <= samples <= 1_000_000:
        raise ValueError("unsupported or unbounded sound rate/count")
    for offset in range(8, len(data), 128):
        block = data[offset:offset + 128]
        if (len(block) < 4 or (len(block) - 4) % 4
                or block[2] > 88 or block[3]):
            raise ValueError("truncated or invalid IMA sound block")


def extract_whx(data: bytes, wanted: tuple[bytes, ...]) -> list[tuple[bytes, bytes]]:
    if len(data) < 36:
        raise ValueError("truncated WHX header")
    magic, count, directory = struct.unpack_from("<4sII", data)
    size = struct.unpack_from("<I", data, 12)[0]
    named = struct.unpack_from("<H", data, 34)[0]
    names_at = 36 + (count + 1) * 4
    if (magic != b"IWHX" or directory != 36 or size != len(data)
            or not 1 <= count <= 100_000 or named > count
            or names_at > len(data) or named * 12 > len(data) - names_at):
        raise ValueError("invalid WHX header/directory")
    found = {}
    for i in range(named):
        raw_name, number = struct.unpack_from("<10sH", data, names_at + i * 12)
        name = raw_name.split(b"\0", 1)[0].upper()
        if name not in wanted:
            continue
        if number >= count or name in found:
            raise ValueError("invalid or duplicate WHX sound index")
        start, end = struct.unpack_from("<II", data, directory + number * 4)
        padding = start >> 30
        start &= 0xFFFFFF
        end &= 0xFFFFFF
        if end < start + padding or end > len(data):
            raise ValueError("sound exceeds WHX bounds")
        raw = data[start:end - padding]
        validate_sound(raw)
        found[name] = raw
    if any(name not in found for name in wanted):
        raise ValueError("requested shareware sound is absent")
    return [(name, found[name]) for name in wanted]


def encode_ima(samples: list[int], rate: int) -> bytes:
    """Encode signed PCM16 to bounded WHX-compatible mono IMA blocks.

    Each full block is 128 bytes / 249 samples. The final payload is rounded
    to four bytes using repeated final PCM samples. The runtime consumes every
    block byte rather than the declared count, so at most seven padded source
    samples play (less than 0.9 ms at either supported sample rate).
    """
    if (rate not in (8000, 11025) or not 1 <= len(samples) <= 1_000_000
            or any(not isinstance(x, int) or not -32768 <= x <= 32767 for x in samples)):
        raise ValueError("unsupported or unbounded PCM rate/samples")
    output = bytearray(struct.pack("<HHI", 0x8003, rate, len(samples)))
    differences = [abs(b - a) for a, b in zip(samples[:64], samples[1:64])]
    target = 2 * sum(differences) // max(1, len(differences))
    index = min(range(len(STEPS)), key=lambda i: abs(STEPS[i] - target))
    for offset in range(0, len(samples), 249):
        block = samples[offset:offset + 249]
        predictor = block[0]
        output.extend(struct.pack("<hBB", predictor, index, 0))
        tail = block[1:] + [block[-1]] * (-(len(block) - 1) % 8)
        codes = []
        for sample in tail:
            difference = sample - predictor
            code = 8 if difference < 0 else 0
            difference = abs(difference)
            step = STEPS[index]
            delta = step >> 3
            for bit, amount in ((4, step), (2, step >> 1), (1, step >> 2)):
                if difference >= amount:
                    code |= bit
                    difference -= amount
                    delta += amount
            predictor += -delta if code & 8 else delta
            predictor = max(-32768, min(32767, predictor))
            index = max(0, min(88, index + (-1,-1,-1,-1,2,4,6,8)[code & 7]))
            codes.append(code)
        output.extend(codes[i] | codes[i + 1] << 4 for i in range(0, len(codes), 2))
    result = bytes(output)
    validate_sound(result)
    return result


def encode_pcm_sound(data: bytes) -> bytes:
    """Read the original Doom DMX PCM8 header and apply the engine's trim.

    Match the pinned Doom/WHX converter: source starts at lump byte 16 and
    declared sample length is reduced by 32. Bytes outside that range are not
    encoded. The original header count must fit inside the complete lump.
    """
    if len(data) < 8 or data[:2] != b"\x03\x00":
        raise ValueError("not native DMX PCM8 sound data")
    rate, count = struct.unpack_from("<HI", data, 2)
    if rate not in (8000, 11025) or not 49 <= count <= 1_000_000 or count > len(data) - 8:
        raise ValueError("unsupported or truncated DMX sound rate/count")
    return encode_ima([(value - 128) * 256 for value in data[16:16 + count - 32]], rate)


def extract_wad(data: bytes, wanted: tuple[bytes, ...]) -> list[tuple[bytes, bytes]]:
    """Validate an IWAD directory and encode only the requested PCM8 sounds."""
    if len(data) < 12:
        raise ValueError("truncated IWAD header")
    magic, count, directory = struct.unpack_from("<4sII", data)
    if (magic != b"IWAD" or not 1 <= count <= 100_000 or directory < 12
            or directory > len(data) or count * 16 > len(data) - directory):
        raise ValueError("invalid IWAD header/directory")
    found = {}
    for i in range(count):
        start, size, raw_name = struct.unpack_from("<II8s", data, directory + i * 16)
        if start > len(data) or size > len(data) - start:
            raise ValueError("lump exceeds IWAD bounds")
        name = raw_name.split(b"\0", 1)[0].upper()
        if name not in wanted:
            continue
        if name in found:
            raise ValueError("duplicate IWAD sound name")
        found[name] = encode_pcm_sound(data[start:start + size])
    if any(name not in found for name in wanted):
        raise ValueError("requested shareware sound is absent")
    return [(name, found[name]) for name in wanted]


def emit_source(sounds: list[tuple[bytes, bytes]]) -> str:
    source = ['/* Private locally generated shareware sound data; do not commit. */',
              '#include "fm1_doom_sound.h"',
              'const uint8_t fm1_doom_sound_bank[] = {']
    payload = b"".join(raw for _, raw in sounds)
    for offset in range(0, len(payload), 16):
        source.append("    " + ",".join(f"0x{x:02x}" for x in payload[offset:offset + 16]) + ",")
    source.extend(['};', 'const fm1_doom_sound_entry fm1_doom_sound_entries[] = {'])
    offset = 0
    for name, raw in sounds:
        source.append(f'    {{"{name[2:].decode().lower()}", fm1_doom_sound_bank + {offset}u, {len(raw)}u}},')
        offset += len(raw)
    source.extend(['};', f'const uint32_t fm1_doom_sound_entry_count = {len(sounds)}u;',
                   f'const uint32_t fm1_doom_sound_bank_bytes = {len(payload)}u;', ''])
    return "\n".join(source)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--whx", type=Path, help="Local WHX with original IMA sounds")
    inputs.add_argument("--wad", type=Path, help="Local original doom1.wad IWAD with PCM8 sounds")
    parser.add_argument("--output-dir", type=Path, default=Path("build/sound-bank"))
    parser.add_argument("--menu", action="store_true", help="Add original menu switch sound")
    args = parser.parse_args()
    input_path = args.whx or args.wad
    data = input_path.read_bytes()
    extract = extract_whx if args.whx else extract_wad
    sounds = extract(data, SOUNDS + ((b"DSSWTCHN",) if args.menu else ()))
    source = emit_source(sounds)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "fm1_doom_sound_bank.c").write_bytes(source.encode("utf-8"))
    (args.output_dir / "fm1_doom_sound_bank_ref.h").write_text(emit_reference(sounds), encoding="utf-8")
    manifest = {
        "source": str(input_path.resolve()), "source_sha256": hashlib.sha256(data).hexdigest(),
        "payload_bytes": sum(len(raw) for _, raw in sounds),
        "generated_source_sha256": hashlib.sha256(source.encode()).hexdigest(),
        "sounds": [{"name": name.decode(), "bytes": len(raw),
                    "sha256": hashlib.sha256(raw).hexdigest(),
                    "rate": struct.unpack_from("<H", raw, 2)[0]}
                   for name, raw in sounds],
    }
    if args.wad:
        manifest.update(source_format="IWAD_PCM8", ima_padding_max_samples=7)
    (args.output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"{len(sounds)} original shareware SFX: {manifest['payload_bytes']} XIP bytes")


if __name__ == "__main__":
    main()
