"""Pack a user-owned staged WAD into independently compressed, seekable blocks.

FMD1 uses little-endian fields: magic, block size, virtual WAD size, block
count, then (file offset, compressed size, CRC32 of raw block) per block.
Each block is a complete zlib stream, suitable for SDK zliblite uncompress.
The archive is deliberately not committed: its input may be copyrighted.
"""
import argparse
from pathlib import Path
import struct
import zlib

HEADER = struct.Struct("<4sIII")
ENTRY = struct.Struct("<III")
MAGIC = b"FMD1"


def pack(raw: bytes, block_size: int) -> bytes:
    if not raw.startswith(b"IWAD"):
        raise ValueError("input must be a staged IWAD")
    if block_size < 1024 or block_size > 32768 or block_size & (block_size - 1):
        raise ValueError("block size must be a power of two from 1024 to 32768")
    count = (len(raw) + block_size - 1) // block_size
    offset = HEADER.size + count * ENTRY.size
    entries = []
    payload = []
    for pos in range(0, len(raw), block_size):
        block = raw[pos:pos + block_size]
        coded = zlib.compress(block, 9)
        entries.append(ENTRY.pack(offset, len(coded), zlib.crc32(block)))
        payload.append(coded)
        offset += len(coded)
    return HEADER.pack(MAGIC, block_size, len(raw), count) + b"".join(entries + payload)


def unpack(archive: bytes) -> bytes:
    if len(archive) < HEADER.size:
        raise ValueError("truncated FMD1 header")
    magic, block_size, raw_size, count = HEADER.unpack_from(archive)
    if (magic != MAGIC or block_size < 1024 or block_size > 32768
            or block_size & (block_size - 1) or raw_size < 12
            or count != (raw_size + block_size - 1) // block_size
            or count > (len(archive) - HEADER.size) // ENTRY.size):
        raise ValueError("invalid FMD1 header")
    cursor = HEADER.size + count * ENTRY.size
    blocks = []
    for i in range(count):
        offset, length, crc = ENTRY.unpack_from(archive, HEADER.size + i * ENTRY.size)
        expected = min(block_size, raw_size - i * block_size)
        if offset != cursor or length < 1 or length > len(archive) - offset:
            raise ValueError(f"invalid FMD1 block {i} location")
        stream = zlib.decompressobj()
        raw = stream.decompress(archive[offset:offset + length], expected + 1)
        if (len(raw) != expected or not stream.eof or stream.unused_data
                or stream.unconsumed_tail or zlib.crc32(raw) != crc):
            raise ValueError(f"invalid FMD1 block {i} contents")
        blocks.append(raw)
        cursor += length
    if cursor != len(archive):
        raise ValueError("FMD1 trailing bytes")
    return b"".join(blocks)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--block-size", type=int, default=4096)
    parser.add_argument("--unpack", action="store_true")
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error("input and output must differ")
    source = args.input.read_bytes()
    result = unpack(source) if args.unpack else pack(source, args.block_size)
    args.output.write_bytes(result)
    print(f"{len(source)} -> {len(result)} bytes")


if __name__ == "__main__":
    main()
