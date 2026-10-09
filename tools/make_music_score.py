"""Compile a local IWAD/MUS/MUSX/WHX E1M1 score to a heapless XIP event bank.

Generated game data stays in ignored build/. MUSX decoding follows the locally
available BSD-licensed rp2040-doom tiny_huff/musx format; no game data is bundled.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct


def wad_music(data):
    """Extract D_E1M1 without trusting directory offsets or lump sizes."""
    if len(data) < 12 or data[:4] not in (b"IWAD", b"PWAD"):
        raise ValueError("not a WAD")
    count, directory = struct.unpack_from("<II", data, 4)
    if directory < 12 or directory > len(data) or count > (len(data) - directory) // 16:
        raise ValueError("truncated WAD directory")
    result = None
    for index in range(count):
        offset, size, name = struct.unpack_from("<II8s", data, directory + index * 16)
        if offset > len(data) or size > len(data) - offset:
            raise ValueError("WAD lump exceeds source bounds")
        # Doom resolves duplicate names from the end of the directory.
        if name.rstrip(b"\0").upper() == b"D_E1M1":
            result = data[offset:offset + size]
    if result is None:
        raise ValueError("WAD has no D_E1M1 music")
    return result


class Bits:
    def __init__(self, data):
        self.data, self.position = data, 0

    def read(self, count=1):
        if count < 0 or self.position + count > len(self.data) * 8:
            raise ValueError("truncated MUSX bitstream")
        value = 0
        for i in range(count):
            value |= ((self.data[self.position // 8] >> (self.position % 8)) & 1) << i
            self.position += 1
        return value


class Huffman:
    def __init__(self, symbols):
        self.single = symbols[0][0] if len(symbols) == 1 else None
        self.codes = {}
        code = 0
        for length in range(1, 16):
            for symbol, width in symbols:
                if width == length:
                    self.codes[length, code] = symbol
                    code += 1
            code <<= 1

    def decode(self, bits):
        if self.single is not None:
            return self.single
        code = 0
        for length in range(1, 16):
            code = code * 2 + bits.read()
            if (length, code) in self.codes:
                return self.codes[length, code]
        raise ValueError("invalid MUSX Huffman symbol")


def simple_decoder(bits):
    if not bits.read():
        return Huffman([])
    grouped, minimum, maximum = bits.read(), bits.read(8), bits.read(8)
    if minimum > maximum:
        raise ValueError("invalid MUSX symbol range")
    if minimum == maximum:
        return Huffman([(minimum, 0)])
    low, high = bits.read(4), bits.read(4)
    if low > high:
        raise ValueError("invalid MUSX code lengths")
    width = (high - low).bit_length()
    symbols = []
    if low == high:
        for value in range(minimum, maximum + 1):
            if bits.read():
                symbols.append((value, low))
    elif grouped:
        for base in range(minimum, maximum + 1, 8):
            same = bits.read()
            present = bits.read() if same else None
            for value in range(base, min(base + 8, maximum + 1)):
                if present if same else bits.read():
                    symbols.append((value, low + bits.read(width)))
    else:
        for value in range(minimum, maximum + 1):
            if bits.read():
                symbols.append((value, low + bits.read(width)))
    return Huffman(symbols)


def musx_events(data):
    if len(data) < 8 or data[:4] != b"MUSX":
        raise ValueError("not MUSX")
    size = struct.unpack_from("<I", data, 4)[0]
    if size > len(data) - 8:
        raise ValueError("truncated MUSX payload")
    bits = Bits(data[8:8 + size])
    low, high = bits.read(4), bits.read(4)
    if low > high:
        raise ValueError("invalid channel Huffman lengths")
    symbols = []
    for channel in range(16):
        if bits.read():
            for kind in range(8):
                if bits.read():
                    symbols.append((channel * 16 + kind,
                                    low + bits.read((high - low).bit_length()) if low != high else low))
    channel_decoder = Huffman(symbols)
    volume, pitch, vibrato, note, drum, velocity, groups = [simple_decoder(bits) for _ in range(7)]
    maximum_notes = bits.read(4)
    release = {count: simple_decoder(bits) for count in range(2, maximum_notes + 1)}
    gaps = simple_decoder(bits)
    last_volume, press_volume = [100] * 16, [100] * 16
    wheels, vibratos, active = [128] * 16, [0] * 16, [[] for _ in range(16)]
    global_volume, time, remaining, events = 100, 0, 0, []
    zig = lambda value: -(value // 2 + 1) if value & 1 else value // 2
    for _ in range(100000):
        if not remaining:
            remaining = groups.decode(bits)
            if not remaining:
                raise ValueError("empty MUSX event group")
        descriptor = channel_decoder.decode(bits)
        source_channel, kind = descriptor >> 4, descriptor & 15
        channel = 15 if source_channel == 9 else source_channel
        a = b = 0
        if kind == 0:
            kind, a = 3, bits.read(3) + 10
        elif kind == 1:
            events.append((time, 6, channel, 0, 0))
            return events
        elif kind == 2:
            index = release[len(active[source_channel])].decode(bits) if len(active[source_channel]) > 1 else 0
            if index >= len(active[source_channel]):
                raise ValueError("invalid MUSX note release")
            kind, a = 0, active[source_channel].pop(index)
        elif kind == 3:
            a = (drum if source_channel == 9 else note).decode(bits)
            b = velocity.decode(bits)
            if b == 129:
                b = global_volume
                last_volume[source_channel] = press_volume[source_channel] = b
            elif b == 128:
                b = press_volume[source_channel]
            else:
                global_volume = last_volume[source_channel] = press_volume[source_channel] = b
            active[source_channel].append(a)
            kind = 1
        elif kind == 4:
            kind, a, b = 4, bits.read(4), bits.read(8)
        elif kind == 5:
            wheels[source_channel] = (wheels[source_channel] + zig(pitch.decode(bits))) & 255
            kind, a = 2, wheels[source_channel]
        elif kind == 6:
            last_volume[source_channel] = (last_volume[source_channel] + zig(volume.decode(bits))) & 255
            kind, a, b = 4, 3, last_volume[source_channel]
        elif kind == 7:
            vibratos[source_channel] = (vibratos[source_channel] + zig(vibrato.decode(bits))) & 255
            kind, a, b = 4, 2, vibratos[source_channel]
        else:
            raise ValueError("unknown MUSX event")
        events.append((time, kind, channel, a, b))
        remaining -= 1
        if not remaining:
            while True:
                gap = gaps.decode(bits)
                time += gap
                if gap != 255:
                    break
    raise ValueError("MUSX score lacks bounded end")


def mus_events(data):
    if len(data) < 16 or data[:4] != b"MUS\x1a":
        raise ValueError("not a bounded MUS file")
    size, start = struct.unpack_from("<HH", data, 4)
    if start < 16 or start + size > len(data):
        raise ValueError("MUS score exceeds file")
    position, end, time, volumes, events = start, start + size, 0, [127] * 16, []
    def read():
        nonlocal position
        if position >= end:
            raise ValueError("truncated MUS event")
        value = data[position]
        position += 1
        return value
    while position < end:
        descriptor = read()
        channel, kind = descriptor & 15, (descriptor >> 4) & 7
        a = b = 0
        if kind == 6:
            events.append((time, kind, channel, a, b))
            return events
        if kind in (0, 1, 2, 3):
            a = read()
            if kind == 1:
                if a & 128:
                    volumes[channel] = read()
                a &= 127
                b = volumes[channel]
        elif kind == 4:
            a, b = read(), read()
        else:
            raise ValueError("unknown MUS event")
        events.append((time, kind, channel, a, b))
        if descriptor & 128:
            delay = 0
            for _ in range(4):
                byte = read()
                delay = delay * 128 + (byte & 127)
                if not byte & 128:
                    break
            else:
                raise ValueError("MUS delay exceeds limit")
            time += delay
    raise ValueError("MUS score lacks end")


def whx_music(data):
    if len(data) < 36 or data[:4] != b"IWHX":
        raise ValueError("not WHX")
    count, directory = struct.unpack_from("<II", data, 4)
    named = struct.unpack_from("<H", data, 34)[0]
    names = 36 + (count + 1) * 4
    if count > 100000 or directory + (count + 1) * 4 > len(data) or names + named * 12 > len(data):
        raise ValueError("invalid WHX directory")
    for index in range(named):
        name, number = struct.unpack_from("<10sH", data, names + index * 12)
        if name.rstrip(b"\0").upper() == b"D_E1M1":
            if number >= count:
                break
            start, end = struct.unpack_from("<II", data, directory + number * 4)
            start &= 0xffffff
            end &= 0xffffff
            if start > end or end > len(data):
                break
            return data[start:end]
    raise ValueError("D_E1M1 absent or invalid")


def encode_events(events):
    output, previous = bytearray(), 0
    for time, kind, channel, a, b in events:
        delta = time - previous
        if delta < 0:
            raise ValueError("nonmonotonic score")
        while delta >= 128:
            output.append((delta & 127) | 128)
            delta >>= 7
        output.append(delta)
        output.append(kind * 16 + channel)
        if kind in (0, 1, 2, 3, 4):
            output.append(a)
        if kind in (1, 4):
            output.append(b)
        previous = time
    return bytes(output)


def compress_score(raw):
    alphabet = sorted(set(raw))
    mapping = {value: index for index, value in enumerate(alphabet)}
    nodes, depth = [(value, 255) for value in alphabet], [1] * len(alphabet)
    sequence = [mapping[value] for value in raw]
    while len(nodes) < 255:
        counts = Counter(zip(sequence, sequence[1:]))
        candidates = [(count, pair) for pair, count in counts.items()
                      if count > 3 and max(depth[pair[0]], depth[pair[1]]) < 16]
        if not candidates:
            break
        _, pair = max(candidates)
        replacement, result, position = len(nodes), [], 0
        while position < len(sequence):
            if tuple(sequence[position:position + 2]) == pair:
                result.append(replacement)
                position += 2
            else:
                result.append(sequence[position])
                position += 1
        if len(sequence) - len(result) <= 2:
            break
        nodes.append(pair)
        depth.append(max(depth[pair[0]], depth[pair[1]]) + 1)
        sequence = result
    return (struct.pack("<4sIHHI", b"FMSC", len(raw), len(nodes), 140, len(sequence))
            + bytes(value for pair in nodes for value in pair) + bytes(sequence)), max(depth)


def expand_score(bank):
    magic, size, count, rate, packed = struct.unpack_from("<4sIHHI", bank)
    if magic != b"FMSC" or count > 255 or rate != 140 or len(bank) != 16 + count * 2 + packed:
        raise ValueError("invalid score bank")
    nodes = [tuple(bank[16 + index * 2:18 + index * 2]) for index in range(count)]
    for index, (left, right) in enumerate(nodes):
        if right != 255 and (left >= index or right >= index):
            raise ValueError("cyclic score grammar")
    output = bytearray()
    for token in bank[16 + count * 2:]:
        stack = [token]
        while stack:
            token = stack.pop()
            if token >= count:
                raise ValueError("invalid score token")
            left, right = nodes[token]
            if right == 255:
                output.append(left)
            else:
                stack.extend((right, left))
            if len(stack) > 16 or len(output) > size:
                raise ValueError("score exceeds bounded decode")
    if len(output) != size:
        raise ValueError("score length mismatch")
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="local IWAD/PWAD, MUS, MUSX, or WHX containing D_E1M1")
    parser.add_argument("output", type=Path, help="ignored build/music-bank output directory")
    args = parser.parse_args()
    source = args.input.read_bytes()
    lump = (whx_music(source) if source.startswith(b"IWHX") else
            wad_music(source) if source[:4] in (b"IWAD", b"PWAD") else source)
    events = musx_events(lump) if lump.startswith(b"MUSX") else mus_events(lump)
    raw = encode_events(events)
    bank, depth = compress_score(raw)
    assert expand_score(bank) == raw
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "music_score.fmsc").write_bytes(bank)
    rows = [",".join(str(value) for value in bank[pos:pos + 24]) for pos in range(0, len(bank), 24)]
    (args.output / "music_score.c").write_text(
        "/* Generated locally from user-owned game data; do not redistribute. */\n#include <stdint.h>\n"
        "const uint8_t fm1_doom_music_score[]={\n" + ",\n".join(rows) + "\n};\n"
        f"const uint32_t fm1_doom_music_score_len={len(bank)}u;\n")
    active, peak = set(), 0
    for _, kind, channel, a, _ in events:
        if kind == 1:
            active.add((channel, a))
        elif kind == 0:
            active.discard((channel, a))
        peak = max(peak, len(active))
    manifest = {"source": str(args.input.resolve()), "source_sha256": hashlib.sha256(source).hexdigest(),
                "original_lump_bytes": len(lump), "events": len(events), "duration_ticks_140hz": events[-1][0],
                "duration_seconds": events[-1][0] / 140, "score_raw_bytes": len(raw), "score_bank_bytes": len(bank),
                "score_sha256": hashlib.sha256(bank).hexdigest(), "grammar_depth": depth,
                "peak_distinct_notes": peak, "event_types": dict(Counter(event[1] for event in events)),
                "controllers": sorted({event[3] for event in events if event[1] == 4}),
                "programs": sorted({event[4] for event in events if event[1] == 4 and event[3] == 0}),
                "roundtrip_verified": True}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
