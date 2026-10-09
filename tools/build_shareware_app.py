"""Fetch Doom shareware data, generate private FM-1 assets and build app.bin.

The Debian shareware package and every WAD-derived artifact stay under the
ignored build/ directory. This builds an unflashed WL82 UBOOT -app input; it
does not create a complete flash dump and never talks to a connected device.
"""
import argparse
from io import BytesIO
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[1]
BUILD_ROOT = ROOT / "build"
DEBIAN_PACKAGE = "doom-wad-shareware_1.9.fixed-5_all.deb"
DEBIAN_PACKAGE_URL = (
    "https://deb.debian.org/debian/pool/non-free/d/doom-wad-shareware/"
    + DEBIAN_PACKAGE
)
DEBIAN_PACKAGE_BYTES = 1_366_368
DEBIAN_PACKAGE_SHA256 = "5802f176c0303e228095b5312def53de602781cf4c53e79842257484a0d9e938"
DEBIAN_COPYRIGHT_URL = (
    "https://sources.debian.org/src/doom-wad-shareware/1.9.fixed-5/debian/copyright/"
)
MAX_PACKAGE_BYTES = 5_000_000
MAX_IWAD_BYTES = 8_000_000


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def stream_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def download_package(destination: Path) -> bytes:
    request = Request(DEBIAN_PACKAGE_URL, headers={"User-Agent": "fm1-doom-local-builder/1"})
    partial = destination.with_suffix(destination.suffix + ".part")
    total = 0
    digest = hashlib.sha256()
    try:
        with urlopen(request, timeout=60) as response, partial.open("wb") as output:
            length = response.headers.get("Content-Length")
            if length and int(length) != DEBIAN_PACKAGE_BYTES:
                raise ValueError(f"unexpected Debian package size: {length}")
            while True:
                block = response.read(64 * 1024)
                if not block:
                    break
                total += len(block)
                if total > MAX_PACKAGE_BYTES:
                    raise ValueError("Debian package exceeds the 5 MB download limit")
                output.write(block)
                digest.update(block)
        if total != DEBIAN_PACKAGE_BYTES:
            raise ValueError(f"Debian package size mismatch: expected {DEBIAN_PACKAGE_BYTES}, got {total}")
        if digest.hexdigest() != DEBIAN_PACKAGE_SHA256:
            raise ValueError("Debian package SHA-256 mismatch; refusing to extract it")
        partial.replace(destination)
    except Exception:
        partial.unlink(missing_ok=True)
        raise
    return destination.read_bytes()


def deb_data_tar(deb: bytes) -> bytes:
    if not deb.startswith(b"!<arch>\n"):
        raise ValueError("download is not a Debian ar package")
    offset = 8
    data_tar = None
    while offset < len(deb):
        if len(deb) - offset < 60:
            raise ValueError("truncated Debian package member header")
        header = deb[offset:offset + 60]
        if header[58:60] != b"`\n":
            raise ValueError("invalid Debian package member header")
        try:
            name = header[:16].decode("ascii").strip().rstrip("/")
            size = int(header[48:58].decode("ascii").strip())
        except (UnicodeDecodeError, ValueError) as error:
            raise ValueError("invalid Debian package member metadata") from error
        start = offset + 60
        end = start + size
        if size < 0 or end > len(deb):
            raise ValueError("Debian package member exceeds package bounds")
        if name == "data.tar.xz":
            if data_tar is not None:
                raise ValueError("duplicate data archive in Debian package")
            data_tar = deb[start:end]
        offset = end + (size & 1)
    if offset != len(deb) or data_tar is None:
        raise ValueError("Debian package has no valid data.tar.xz member")
    return data_tar


def package_files(data_tar: bytes) -> tuple[bytes, bytes]:
    wanted = {
        "usr/share/games/doom/doom1.wad": None,
        "usr/share/doc/doom-wad-shareware/copyright": None,
    }
    with tarfile.open(fileobj=BytesIO(data_tar), mode="r:xz") as archive:
        for member in archive.getmembers():
            name = member.name.removeprefix("./").lstrip("/")
            if name not in wanted:
                continue
            if not member.isfile() or member.size < 0 or member.size > MAX_IWAD_BYTES:
                raise ValueError(f"invalid packaged file: {member.name}")
            if wanted[name] is not None:
                raise ValueError(f"duplicate packaged file: {name}")
            source = archive.extractfile(member)
            if source is None:
                raise ValueError(f"cannot read packaged file: {name}")
            content = source.read(member.size + 1)
            if len(content) != member.size:
                raise ValueError(f"truncated packaged file: {name}")
            wanted[name] = content
    wad = wanted["usr/share/games/doom/doom1.wad"]
    copyright_text = wanted["usr/share/doc/doom-wad-shareware/copyright"]
    if wad is None or copyright_text is None:
        raise ValueError("Debian package is missing doom1.wad or its copyright file")
    if not 12 <= len(wad) <= MAX_IWAD_BYTES or wad[:4] != b"IWAD":
        raise ValueError("packaged Doom data is not a bounded IWAD")
    return wad, copyright_text


def run_tool(script: str, *arguments: str | Path) -> None:
    command = [sys.executable, str(ROOT / "tools" / script),
               *(str(argument) for argument in arguments)]
    print(f"\n== {script} ==", flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def run_git_submodules() -> None:
    print("\n== initialize pinned source submodules ==", flush=True)
    subprocess.run(["git", "submodule", "update", "--init"], cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fm1-root", type=Path, required=True,
                        help="FM-1 checkout containing the pinned SDK and build_boot.py")
    parser.add_argument("--wad", type=Path,
                        help="use a local shareware IWAD instead of downloading Debian's package")
    parser.add_argument("--output-dir", type=Path, default=Path("build/shareware-rom"),
                        help="fresh private output directory under build/ (default: build/shareware-rom)")
    args = parser.parse_args()

    fm1_root = args.fm1_root.expanduser().resolve()
    if not (fm1_root / "firmware/nes/build_boot.py").is_file():
        parser.error(f"FM-1 build support was not found under {fm1_root}")
    if not (fm1_root / "references/source/fw-AC79_AIoT_SDK").is_dir():
        parser.error(f"the pinned FM-1 SDK checkout is missing under {fm1_root}")

    output_arg = args.output_dir
    output = (output_arg if output_arg.is_absolute() else ROOT / output_arg).resolve()
    build_root = BUILD_ROOT.resolve()
    try:
        output.relative_to(build_root)
    except ValueError:
        parser.error(f"output must remain under ignored build/: {build_root}")
    if output == build_root:
        parser.error("choose a dedicated output subdirectory below build/")
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error(f"output directory is not empty; choose a fresh directory: {output}")

    output.mkdir(parents=True, exist_ok=True)
    source_dir = output / "source"
    source_dir.mkdir()
    if args.wad:
        wad_path = args.wad.expanduser().resolve()
        if not wad_path.is_file():
            parser.error(f"IWAD does not exist: {wad_path}")
        with wad_path.open("rb") as source:
            if source.read(4) != b"IWAD":
                parser.error("--wad must point to an uncompressed Doom IWAD")
        source_info = {
            "kind": "user_supplied_iwad",
            "name": wad_path.name,
            "bytes": wad_path.stat().st_size,
            "sha256": stream_sha256(wad_path),
        }
    else:
        package_path = source_dir / DEBIAN_PACKAGE
        print(f"Downloading pinned Debian shareware package: {DEBIAN_PACKAGE_URL}", flush=True)
        package_bytes = download_package(package_path)
        wad_bytes, copyright_text = package_files(deb_data_tar(package_bytes))
        wad_path = source_dir / "doom1.wad"
        wad_path.write_bytes(wad_bytes)
        (source_dir / "debian-copyright.txt").write_bytes(copyright_text)
        source_info = {
            "kind": "debian_shareware_package",
            "package": DEBIAN_PACKAGE,
            "package_url": DEBIAN_PACKAGE_URL,
            "package_sha256": DEBIAN_PACKAGE_SHA256,
            "copyright_reference": DEBIAN_COPYRIGHT_URL,
            "wad_name": wad_path.name,
            "wad_bytes": len(wad_bytes),
            "wad_sha256": sha256(wad_bytes),
        }

    run_git_submodules()
    assets = output / "assets"
    run_tool("convert_shareware_wad.py", wad_path, "--output-dir", assets)
    run_tool("make_lowres_engine.py")
    run_tool("compile_target_engine.py", "--fm1-root", fm1_root, "--lowres")
    run_tool("compile_target_port.py", "--fm1-root", fm1_root, "--lowres")

    target = output / "target"
    run_tool(
        "build_target_candidate.py",
        assets / "e1m1-4k.fmd",
        "--fm1-root", fm1_root,
        "--out-dir", target,
        "--sound-bank", assets / "sound-bank" / "fm1_doom_sound_bank.c",
        "--music-bank", assets / "music-original" / "music_score.c",
        "--genmidi-bank", assets / "opl-bank" / "genmidi_bank.c",
    )

    candidate_app = target / "app.bin"
    if not candidate_app.is_file():
        raise ValueError("target build finished without producing app.bin")
    app_path = output / "app.bin"
    shutil.copy2(candidate_app, app_path)
    candidate_manifest = json.loads((target / "build-manifest.json").read_text(encoding="utf-8"))
    report = {
        "status": "uboot_app_input_ready_unflashed",
        "source": source_info,
        "assets_manifest": "assets/conversion.json",
        "candidate_manifest": "target/build-manifest.json",
        "app_bin": {
            "path": "app.bin",
            "bytes": app_path.stat().st_size,
            "sha256": stream_sha256(app_path),
            "format": candidate_manifest.get("uboot_app_format"),
        },
        "hardware_boot_verified": False,
        "device_operations_performed": False,
    }
    (output / "shareware-app-manifest.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print("\nBuild complete.")
    print(f"UBOOT app input: {app_path}")
    print(f"Bytes: {report['app_bin']['bytes']}")
    print(f"SHA-256: {report['app_bin']['sha256']}")
    print("This is the plain app.bin for a writer's -app option, not a complete raw-flash dump. No device was flashed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError, tarfile.TarError) as error:
        raise SystemExit(f"Build failed: {error}") from error
