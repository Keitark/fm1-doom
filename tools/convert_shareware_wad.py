"""Convert a local Doom shareware IWAD into FM-1's private E1M1 asset set.

All generated game data is written below the ignored build/ directory and must
not be committed. Supply your own lawful original Doom shareware IWAD.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
BUILD_ROOT = ROOT / "build"


def run(script: str, *arguments: str | Path) -> None:
    command = [sys.executable, str(ROOT / "tools" / script),
               *(str(argument) for argument in arguments)]
    print("\n$ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iwad", type=Path,
                        help="local Doom shareware IWAD, for example C:\\games\\doom1.wad")
    parser.add_argument("--output-dir", type=Path,
                        default=Path("build/shareware-e1m1"),
                        help="fresh ignored output directory under build/ (default: build/shareware-e1m1)")
    args = parser.parse_args()

    iwad = args.iwad.expanduser().resolve()
    if not iwad.is_file():
        parser.error(f"IWAD does not exist: {iwad}")
    try:
        with iwad.open("rb") as source:
            if source.read(4) != b"IWAD":
                parser.error("input must be an uncompressed Doom IWAD")
    except OSError as error:
        parser.error(f"cannot read IWAD: {error}")

    output_arg = args.output_dir
    output = (output_arg if output_arg.is_absolute() else ROOT / output_arg).resolve()
    build_root = BUILD_ROOT.resolve()
    try:
        output.relative_to(build_root)
    except ValueError:
        parser.error(f"output must remain under the ignored build/ directory: {build_root}")
    if output == build_root:
        parser.error("choose a dedicated output subdirectory below build/")
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error(f"output directory is not empty; choose a fresh directory: {output}")

    output.mkdir(parents=True, exist_ok=True)
    staged = output / "stage-menu-ui.wad"
    archive = output / "e1m1-4k.fmd"

    run("stage_wad.py", iwad, staged, "--map", "E1M1", "--silent",
        "--prune-graphics", "--prune-sprites", "--no-attract-art", "--no-ui",
        "--menu-ui", "--pixelate", "4", "--weapon-pixelate", "1",
        "--fist-pixelate", "2", "--compact-assets")
    run("pack_archive.py", staged, archive, "--block-size", "4096")
    run("make_sound_bank.py", "--wad", iwad, "--menu", "--output-dir",
        output / "sound-bank")
    run("make_music_score.py", iwad, output / "music-original")
    run("make_genmidi_bank.py", iwad, output / "opl-bank")

    artifacts = []
    for path in sorted(output.rglob("*")):
        if path.is_file():
            artifacts.append({
                "path": path.relative_to(output).as_posix(),
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            })
    manifest = {
        "profile": "E1M1; 4x4 world/enemy; original pistol; 2x2 fist; 4 KiB FMD1",
        "source_name": iwad.name,
        "source_bytes": iwad.stat().st_size,
        "source_sha256": sha256(iwad),
        "artifacts": artifacts,
        "repository_policy": "keep_generated_game_data_local",
    }
    (output / "conversion.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"\nCreated {archive}")
    print(f"Generated local assets and conversion manifest: {output}")
    print("Keep this generated game data out of Git; only the converter source belongs in the public repository.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
