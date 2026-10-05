"""Create a small private shareware sound bank from a local WHX or IWAD.

Generated samples are user-local game data and must remain outside Git.
WHX IMA sounds are copied unchanged. Native IWAD PCM8 uses lossless bounded
Rice coding by default. Pistol, oof, pickup and optional menu sounds are kept.
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
    """Independent offline PCM reference for legacy IMA or exact PCM8/Rice."""
    validate_sound(data)
    if data[1] == 129:
        return [(sample - 128) * 256 for sample in decode_rice(data)]
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
    if len(data) < 8 or data[:2] not in (b"\x03\x80", b"\x03\x81"):
        raise ValueError("not bounded IMA or PCM8/Rice sound data")
    rate, samples = struct.unpack_from("<HI", data, 2)
    if rate not in (8000, 11025) or not 1 <= samples <= 1_000_000:
        raise ValueError("unsupported or unbounded sound rate/count")
    if data[1] == 129:
        decode_rice(data)
        return
    if len(data) < 12:
        raise ValueError("truncated IMA sound")
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


def encode_rice(pcm: bytes, rate: int) -> bytes:
    """Exact PCM8, 256-sample blocks, at most 15 input bits per decoded sample.

    A block stores first unsigned sample and Rice k, then signed modulo-256
    deltas, zigzag-coded, LSB first. Seven unary ones escape to eight raw bits;
    other values store q ones, a zero and k remainder bits. Final byte padding
    is zero. The declared count determines the exact final block and duration.
    """
    if rate not in (8000, 11025) or not 1 <= len(pcm) <= 1_000_000:
        raise ValueError("unsupported or unbounded PCM rate/samples")
    out = bytearray(struct.pack("<HHI", 0x8103, rate, len(pcm)))
    for offset in range(0, len(pcm), 256):
        block = pcm[offset:offset + 256]
        previous = block[0]
        deltas = []
        for sample in block[1:]:
            signed = ((sample - previous + 128) & 255) - 128
            previous = sample
            deltas.append((signed << 1) ^ (signed >> 7))
        sizes = [sum((value >> k) + 1 + k if (value >> k) < 7 else 15
                     for value in deltas) for k in range(8)]
        k = min(range(8), key=lambda index: sizes[index])
        out.extend((block[0], k))
        pending = count = 0

        def bit(value: int) -> None:
            nonlocal pending, count
            pending |= value << count
            count += 1
            if count == 8:
                out.append(pending)
                pending = count = 0

        def bits(value: int, amount: int) -> None:
            for index in range(amount):
                bit((value >> index) & 1)

        for value in deltas:
            quotient = value >> k
            if quotient >= 7:
                bits(127, 7)
                bits(value, 8)
            else:
                bits((1 << quotient) - 1, quotient)
                bit(0)
                bits(value & ((1 << k) - 1), k)
        if count:
            out.append(pending)
    encoded = bytes(out)
    if decode_rice(encoded) != pcm:
        raise ValueError("lossless PCM round trip failed")
    return encoded


def decode_rice(data: bytes) -> bytes:
    """Bounds-checked independent offline decoder; no target code is reused."""
    if len(data) < 8:
        raise ValueError("truncated Rice header")
    marker, rate, total = struct.unpack_from("<HHI", data)
    if marker != 0x8103 or rate not in (8000, 11025) or not 1 <= total <= 1_000_000:
        raise ValueError("unsupported Rice header")
    output = bytearray()
    offset = 8
    while len(output) < total:
        if len(data) - offset < 2 or data[offset + 1] > 7:
            raise ValueError("truncated or invalid Rice block")
        previous, k = data[offset:offset + 2]
        offset += 2
        samples = min(256, total - len(output))
        output.append(previous)
        position = 0

        def bit() -> int:
            nonlocal position
            index = offset + position // 8
            if index >= len(data):
                raise ValueError("truncated Rice sample")
            value = (data[index] >> (position & 7)) & 1
            position += 1
            return value

        def bits(amount: int) -> int:
            value = 0
            for index in range(amount):
                value |= bit() << index
            return value

        for _ in range(1, samples):
            quotient = 0
            while quotient < 7 and bit():
                quotient += 1
            value = bits(8) if quotient == 7 else (quotient << k) | bits(k)
            if value > 255:
                raise ValueError("Rice delta exceeds unsigned PCM8")
            signed = (value >> 1) ^ -(value & 1)
            previous = (previous + signed) & 255
            output.append(previous)
        if position & 7 and data[offset + position // 8] >> (position & 7):
            raise ValueError("nonzero Rice alignment padding")
        offset += (position + 7) // 8
    if offset != len(data):
        raise ValueError("trailing Rice sound data")
    return bytes(output)


def native_pcm(data: bytes) -> tuple[int, bytes]:
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
    return rate, data[16:16 + count - 32]


def encode_pcm_sound(data: bytes, codec: str = "rice") -> bytes:
    rate, pcm = native_pcm(data)
    if codec == "rice":
        return encode_rice(pcm, rate)
    if codec == "ima":
        return encode_ima([(value - 128) * 256 for value in pcm], rate)
    raise ValueError("unsupported native sound codec")


def extract_wad(data: bytes, wanted: tuple[bytes, ...], codec: str = "rice",
                optional: tuple[bytes, ...] = ()) -> list[tuple[bytes, bytes]]:
    """Validate an IWAD directory and encode the requested native PCM8 sounds."""
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
        if name not in wanted + optional:
            continue
        if name in found:
            raise ValueError("duplicate IWAD sound name")
        found[name] = encode_pcm_sound(data[start:start + size], codec)
    if any(name not in found for name in wanted):
        raise ValueError("requested shareware sound is absent")
    return [(name, found[name]) for name in wanted + optional if name in found]


def emit_source(sounds: list[tuple[bytes, bytes]]) -> str:
    source = ['/* Private locally generated shareware sound data; do not commit. */',
              '#include "fm1_doom_sound.h"',
              'const uint8_t fm1_doom_sound_bank[] = {']
    # Identical original sounds such as DSOOF/DSNOWAY share exact bank bytes.
    offsets = {}
    payload = bytearray()
    for _, raw in sounds:
        if raw not in offsets:
            offsets[raw] = len(payload)
            payload.extend(raw)
    for offset in range(0, len(payload), 16):
        source.append("    " + ",".join(f"0x{x:02x}" for x in payload[offset:offset + 16]) + ",")
    source.extend(['};', 'const fm1_doom_sound_entry fm1_doom_sound_entries[] = {'])
    for name, raw in sounds:
        offset = offsets[raw]
        source.append(f'    {{"{name[2:].decode().lower()}", fm1_doom_sound_bank + {offset}u, {len(raw)}u}},')
    source.extend(['};', f'const uint32_t fm1_doom_sound_entry_count = {len(sounds)}u;',
                   f'const uint32_t fm1_doom_sound_bank_bytes = {len(payload)}u;', ''])
    return "\n".join(source)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--whx", type=Path, help="Local WHX with original IMA sounds")
    inputs.add_argument("--wad", type=Path, help="Local original doom1.wad IWAD with PCM8 sounds")
    parser.add_argument("--output-dir", type=Path, default=Path("build/sound-bank"))
    parser.add_argument("--menu", action="store_true", help="Add original switch sounds (native IWAD keeps both variants)")
    parser.add_argument("--codec", choices=("rice", "ima"), default="rice",
                        help="Native IWAD codec: exact PCM8/Rice (default) or legacy lossy IMA")
    args = parser.parse_args()
    input_path = args.whx or args.wad
    data = input_path.read_bytes()
    if args.whx:
        sounds = extract_whx(data, SOUNDS + ((b"DSSWTCHN",) if args.menu else ()))
    else:
        sounds = extract_wad(data, SOUNDS + ((b"DSSWTCHN", b"DSSWTCHX") if args.menu else ()),
                             args.codec, optional=(b"DSNOWAY",))
    source = emit_source(sounds)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "fm1_doom_sound_bank.c").write_bytes(source.encode("utf-8"))
    (args.output_dir / "fm1_doom_sound_bank_ref.h").write_text(emit_reference(sounds), encoding="utf-8")
    manifest = {
        "source": str(input_path.resolve()), "source_sha256": hashlib.sha256(data).hexdigest(),
        "payload_bytes": sum(len(raw) for raw in set(raw for _, raw in sounds)),
        "entry_bytes_before_deduplication": sum(len(raw) for _, raw in sounds),
        "generated_source_sha256": hashlib.sha256(source.encode()).hexdigest(),
        "sounds": [{"name": name.decode(), "bytes": len(raw),
                    "sha256": hashlib.sha256(raw).hexdigest(),
                    "rate": struct.unpack_from("<H", raw, 2)[0],
                    "decoded_samples": len(reference_pcm(raw)),
                    "decoded_pcm16_fnv": pcm_fnv(reference_pcm(raw))}
                   for name, raw in sounds],
    }
    if args.wad:
        manifest.update(source_format="IWAD_PCM8", codec="PCM8_RICE" if args.codec == "rice" else "IMA",
                        lossless=args.codec == "rice", decoded_matches_original_pcm8=args.codec == "rice",
                        rice_max_bits_per_sample=15 if args.codec == "rice" else None,
                        ima_padding_max_samples=0 if args.codec == "rice" else 7)
    (args.output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"{len(sounds)} original shareware SFX: {manifest['payload_bytes']} XIP bytes")


if __name__ == "__main__":
    main()
