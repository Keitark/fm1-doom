"""Create a local, single-map IWAD for size experiments; never redistribute it.

The output is an ordinary uncompressed WAD, so it can be tested against the
unmodified Doom lump reader. The reported zlib number is a measurement only.
"""
import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import struct
import zlib

MAP = re.compile(rb"E[1-4]M[1-9]$|MAP[0-9][0-9]$")
MAP_LUMPS = (b"THINGS", b"LINEDEFS", b"SIDEDEFS", b"VERTEXES", b"SEGS",
             b"SSECTORS", b"NODES", b"SECTORS", b"REJECT", b"BLOCKMAP")
WEAPON_VIEW_PREFIXES = {b"PISG", b"PISF", b"PUNG"}


@dataclass(frozen=True)
class Lump:
    name: bytes
    data: bytes


def read_wad(path: Path) -> list[Lump]:
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError("WAD header is truncated")
    magic, count, directory = struct.unpack_from("<4sII", data)
    if magic != b"IWAD" or count > 100000 or directory > len(data) - count * 16:
        raise ValueError("not a bounded IWAD")
    lumps = []
    for i in range(count):
        pos, size, raw_name = struct.unpack_from("<II8s", data, directory + i * 16)
        if pos > len(data) or size > len(data) - pos:
            raise ValueError(f"lump {i} exceeds the file")
        lumps.append(Lump(raw_name.split(b"\0", 1)[0], data[pos:pos + size]))
    return lumps


def keep_first_map(lumps: list[Lump], map_name: bytes, silent: bool,
                   no_attract_art: bool) -> list[Lump]:
    selected = []
    maps_seen = set()
    i = 0
    while i < len(lumps):
        lump = lumps[i]
        if MAP.fullmatch(lump.name):
            maps_seen.add(lump.name)
            if i + 10 >= len(lumps) or tuple(x.name for x in lumps[i + 1:i + 11]) != MAP_LUMPS:
                raise ValueError(f"unsupported map block at {lump.name!r}")
            if lump.name == map_name:
                selected.extend(lumps[i:i + 11])
            i += 11
            continue
        if lump.name.startswith(b"DEMO"):
            i += 1
            continue
        if no_attract_art and lump.name in (b"TITLEPIC", b"HELP1", b"HELP2", b"CREDIT"):
            i += 1
            continue
        if silent and lump.name == b"D_E1M1":
            # The engine still looks up the name before its disabled music
            # backend is called. Keep a zero-length marker, not the song.
            selected.append(Lump(lump.name, b""))
            i += 1
            continue
        if silent and (lump.name.startswith((b"D_", b"DS", b"DP"))
                       or lump.name in (b"GENMIDI", b"DMXGUS")):
            i += 1
            continue
        selected.append(lump)
        i += 1
    if map_name not in maps_seen:
        raise ValueError(f"map {map_name.decode()} is absent")
    return selected


def omit_direct_boot_ui(lumps: list[Lump], menu_ui: bool = False) -> list[Lump]:
    """Omit unused lowres UI art, optionally retaining original menu patches."""
    output = []
    namespace = b""
    for lump in lumps:
        if lump.name in (b"S_START", b"P_START", b"F_START"):
            namespace = lump.name[:1]
        elif lump.name in (b"S_END", b"P_END", b"F_END"):
            namespace = b""
        if not namespace and lump.name.startswith((b"WI", b"ST")):
            continue
        if not namespace and not menu_ui and lump.name.startswith(b"M_"):
            continue
        output.append(lump)
    return output


def sprite_prefixes(map_things: bytes) -> set[bytes]:
    """Conservative sprite closure for map actors and their potential effects."""
    if len(map_things) % 10:
        raise ValueError("map THINGS has an invalid record length")
    type_ids = {struct.unpack_from("<H", map_things, i + 6)[0]
                for i in range(0, len(map_things), 10)}
    info = (Path(__file__).resolve().parents[1]
            / "vendor/doomgeneric/doomgeneric/info.c").read_text(encoding="utf-8")
    state_to_sprite = {state.encode(): sprite.encode()
                       for sprite, state in re.findall(r"\{SPR_(\w+),[^\n]*//\s*(S_\w+)", info)}
    needed = {b"PLAY", b"PUNG", b"PISG", b"PISF", b"SHTG", b"SHTF",
              b"PUFF", b"BLUD", b"TFOG", b"IFOG", b"BAL1", b"BAL2",
              b"MISL", b"BEXP", b"BAR1"}
    recognized = set()
    blocks = re.findall(r"\{\s*// MT_(\w+)\s*\n\s*(-?\d+),\s*// doomednum(.*?)\n\s*\},",
                        info.split("mobjinfo_t mobjinfo", 1)[1], re.S)
    for _, number, body in blocks:
        if int(number) not in type_ids:
            continue
        recognized.add(int(number))
        needed.update(state_to_sprite[state.encode()]
                      for state in re.findall(r"\bS_\w+\b", body)
                      if state.encode() in state_to_sprite)
    # Player starts (1..4) are map start points, not mobj doomednum values.
    unknown = type_ids - recognized - {1, 2, 3, 4, 11}
    if unknown:
        raise ValueError(f"unknown map thing types: {sorted(unknown)}")
    return needed


def pixelate_patch(data: bytes, factor: int) -> bytes:
    """Coarsen pixels but preserve Doom patch dimensions and transparency."""
    if len(data) < 8:
        raise ValueError("truncated Doom patch")
    width, height = struct.unpack_from("<HH", data)
    if not 0 < width <= 1024 or not 0 < height <= 1024 or len(data) < 8 + width * 4:
        raise ValueError("invalid Doom patch header")
    old_offsets = struct.unpack_from("<" + "I" * width, data, 8)
    out = bytearray(data[:8] + bytes(width * 4))
    new_offsets = []
    columns = {}
    for x in range(width):
        source = old_offsets[(x // factor) * factor]
        if source in columns:
            new_offsets.append(columns[source])
            continue
        if source >= len(data):
            raise ValueError("Doom patch column exceeds lump")
        column_start = len(out)
        columns[source] = column_start
        new_offsets.append(column_start)
        pos = source
        while True:
            if pos >= len(data):
                raise ValueError("Doom patch column lacks terminator")
            top = data[pos]
            if top == 255:
                out.append(255)
                break
            if pos + 4 > len(data):
                raise ValueError("truncated Doom patch post")
            count = data[pos + 1]
            if pos + 4 + count > len(data):
                raise ValueError("Doom patch post exceeds lump")
            pixels = data[pos + 3:pos + 3 + count]
            reduced = bytes(pixels[(j // factor) * factor] for j in range(count))
            out.extend(data[pos:pos + 3])
            out.extend(reduced)
            out.append(data[pos + 3 + count])
            pos += 4 + count
    struct.pack_into("<" + "I" * width, out, 8, *new_offsets)
    return bytes(out)


def pixelate_flat(data: bytes, factor: int) -> bytes:
    if len(data) != 64 * 64:
        raise ValueError("non-64x64 flat in selected namespace")
    return bytes(data[(y // factor) * factor * 64 + (x // factor) * factor]
                 for y in range(64) for x in range(64))


def prune_graphics(lumps: list[Lump], map_name: bytes, sprites: bool,
                   pixelate: int, weapon_pixelate: int | None = None,
                   fist_pixelate: int | None = None) -> list[Lump]:
    """Keep the selected map's walls/flats plus episode-one switches/sky.

    Non-level UI graphics remain intact. Optional pixelation preserves logical
    dimensions; weapon_pixelate overrides retained pistol/fist view art, and
    fist_pixelate can separately reduce the fist to leave room for audio.
    """
    by_name = {lump.name: lump for lump in lumps}
    index = next(i for i, lump in enumerate(lumps) if lump.name == map_name)
    sides = lumps[index + 3].data
    sectors = lumps[index + 8].data
    needed_sprites = sprite_prefixes(lumps[index + 1].data) if sprites else set()
    if len(sides) % 30 or len(sectors) % 26:
        raise ValueError("map geometry has an invalid record length")
    wall_names = {sides[i + j:i + j + 8].split(b"\0", 1)[0]
                  for i in range(0, len(sides), 30) for j in (4, 12, 20)}
    flat_names = {sectors[i + j:i + j + 8].split(b"\0", 1)[0]
                  for i in range(0, len(sectors), 26) for j in (4, 12)}
    wall_names.add(b"SKY1")

    texture_data = by_name[b"TEXTURE1"].data
    texture_count = struct.unpack_from("<I", texture_data)[0]
    if texture_count > 10000 or len(texture_data) < 4 + 4 * texture_count:
        raise ValueError("invalid TEXTURE1 directory")
    texture_records = []
    for offset in struct.unpack_from("<" + "I" * texture_count, texture_data, 4):
        if offset + 22 > len(texture_data):
            raise ValueError("texture definition exceeds TEXTURE1")
        count = struct.unpack_from("<H", texture_data, offset + 20)[0]
        end = offset + 22 + 10 * count
        if end > len(texture_data):
            raise ValueError("texture patches exceed TEXTURE1")
        name = texture_data[offset:offset + 8].split(b"\0", 1)[0]
        texture_records.append((name, texture_data[offset:end]))

    # Episode-one switch pairs are eagerly resolved by P_InitSwitchList.
    wall_names.update(name for name, _ in texture_records if name.startswith((b"SW1", b"SW2")))
    # Retain complete animation sequences if their first frame is present.
    for prefix in (b"BLODGR", b"SLADRIP", b"BLODRIP", b"FIREWAL",
                   b"GSTFONT", b"FIRELAV", b"FIREMAG", b"FIREBLU", b"ROCKRED"):
        if any(name.startswith(prefix) for name in wall_names):
            wall_names.update(name for name, _ in texture_records if name.startswith(prefix))
    kept_textures = [record for name, record in texture_records if name in wall_names]
    missing_walls = wall_names - {name for name, _ in texture_records} - {b"-"}
    if missing_walls:
        raise ValueError(f"missing wall texture definitions: {sorted(missing_walls)}")
    packed_textures = bytearray(struct.pack("<I", len(kept_textures)))
    offset = 4 + len(kept_textures) * 4
    for record in kept_textures:
        packed_textures += struct.pack("<I", offset)
        offset += len(record)
    for record in kept_textures:
        packed_textures += record

    pnames = by_name[b"PNAMES"].data
    patch_count = struct.unpack_from("<I", pnames)[0]
    if patch_count > 10000 or len(pnames) < 4 + patch_count * 8:
        raise ValueError("invalid PNAMES")
    patch_names = [pnames[4 + i * 8:12 + i * 8].split(b"\0", 1)[0]
                   for i in range(patch_count)]
    needed_patches = set()
    for record in kept_textures:
        count = struct.unpack_from("<H", record, 20)[0]
        for i in range(count):
            patch_id = struct.unpack_from("<H", record, 22 + i * 10 + 4)[0]
            if patch_id >= patch_count:
                raise ValueError("texture references an invalid PNAMES index")
            needed_patches.add(patch_names[patch_id].upper())
    # NUKAGE animation is used by E1M1; keep all frames in their original order.
    if any(name.startswith(b"NUKAGE") for name in flat_names):
        flat_names.update({b"NUKAGE1", b"NUKAGE2", b"NUKAGE3"})

    output = []
    namespace = b""
    for lump in lumps:
        if lump.name == b"TEXTURE1":
            output.append(Lump(lump.name, bytes(packed_textures)))
            continue
        if lump.name in (b"S_START", b"P_START", b"F_START"):
            namespace = lump.name[:1]
        elif lump.name in (b"S_END", b"P_END", b"F_END"):
            namespace = b""
        if namespace == b"P" and lump.data and lump.name.upper() not in needed_patches:
            continue
        if namespace == b"F" and lump.data and lump.name not in flat_names:
            continue
        if sprites and namespace == b"S" and lump.data and lump.name[:4].upper() not in needed_sprites:
            continue
        lump_pixelate = pixelate
        if (namespace == b"S" and weapon_pixelate is not None
                and lump.name[:4].upper() in WEAPON_VIEW_PREFIXES):
            lump_pixelate = weapon_pixelate
        if (namespace == b"S" and fist_pixelate is not None
                and lump.name[:4].upper() == b"PUNG"):
            lump_pixelate = fist_pixelate
        if lump_pixelate > 1 and lump.data and namespace in (b"S", b"P"):
            lump = Lump(lump.name, pixelate_patch(lump.data, lump_pixelate))
        elif pixelate > 1 and lump.data and namespace == b"F":
            lump = Lump(lump.name, pixelate_flat(lump.data, pixelate))
        output.append(lump)
    return output


def write_wad(path: Path, lumps: list[Lump]) -> int:
    with path.open("wb") as out:
        out.write(b"\0" * 12)
        records = []
        for lump in lumps:
            pos = out.tell()
            out.write(lump.data)
            records.append((pos, len(lump.data), lump.name))
        directory = out.tell()
        for pos, size, name in records:
            out.write(struct.pack("<II8s", pos, size, name))
        length = out.tell()
        out.seek(0)
        out.write(struct.pack("<4sII", b"IWAD", len(records), directory))
    return length


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iwad", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--map", default="E1M1")
    parser.add_argument("--silent", action="store_true",
                        help="Remove music and sound lumps; run engine with -nosfx -nomusic")
    parser.add_argument("--prune-graphics", action="store_true",
                        help="Keep only map walls/flats and their referenced texture patches")
    parser.add_argument("--prune-sprites", action="store_true",
                        help="Also keep only map actor, weapon, and common effect sprites")
    parser.add_argument("--no-attract-art", action="store_true",
                        help="Drop title/help/credits images for direct E1M1 boot")
    parser.add_argument("--no-ui", action="store_true",
                        help="Drop menu/intermission/status/text art for the generated 160x100 direct-E1M1 build")
    parser.add_argument("--menu-ui", action="store_true",
                        help="With --no-ui, retain original M_ menu patches for the half-size menu")
    parser.add_argument("--pixelate", type=int, choices=(1, 2, 4, 8, 16), default=1,
                        help="Coarsen patch/sprite/flat pixels while keeping logical dimensions")
    parser.add_argument("--weapon-pixelate", type=int, choices=(1, 2, 4, 8, 16),
                        help="Override pistol/fist view patch pixelation; defaults to --pixelate")
    parser.add_argument("--fist-pixelate", type=int, choices=(1, 2, 4, 8, 16),
                        help="Override fist art separately; retains the selected pistol detail")
    args = parser.parse_args()
    map_name = args.map.upper().encode("ascii")
    if not MAP.fullmatch(map_name):
        parser.error("--map must be E#M# or MAP##")
    if args.iwad.resolve() == args.output.resolve():
        parser.error("input and output must differ")
    source = read_wad(args.iwad)
    stage = keep_first_map(source, map_name, args.silent, args.no_attract_art)
    if args.prune_sprites and not args.prune_graphics:
        parser.error("--prune-sprites requires --prune-graphics")
    if args.weapon_pixelate is not None and not args.prune_graphics:
        parser.error("--weapon-pixelate requires --prune-graphics")
    if args.fist_pixelate is not None and not args.prune_graphics:
        parser.error("--fist-pixelate requires --prune-graphics")
    if args.prune_graphics:
        stage = prune_graphics(stage, map_name, args.prune_sprites, args.pixelate,
                               args.weapon_pixelate, args.fist_pixelate)
    if args.no_ui:
        if map_name != b"E1M1" or not args.no_attract_art:
            parser.error("--no-ui requires --map E1M1 and --no-attract-art")
        stage = omit_direct_boot_ui(stage, args.menu_ui)
    elif args.menu_ui:
        parser.error("--menu-ui requires --no-ui")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    length = write_wad(args.output, stage)
    packed = len(zlib.compress(args.output.read_bytes(), 9))
    print(f"source={args.iwad.stat().st_size} bytes {len(source)} lumps")
    print(f"stage={length} bytes {len(stage)} lumps; zlib-9 estimate={packed} bytes")
    print("Compressed estimate is not directly readable by Doom and is not a firmware image.")


if __name__ == "__main__":
    main()
