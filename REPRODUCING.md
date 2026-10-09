# Reproducing the FM-1 Doom app

This guide reproduces the **software build** from a clean checkout through a
plain WL82 UBOOT `app.bin`. It does not claim that the resulting app boots or
works on an FM-1. The latest locally produced candidate is still unflashed;
device verification is a separate hardware acceptance step.

## Pinned inputs for the 2026-10-09 build

| Input | Pin | Where it comes from |
| --- | --- | --- |
| Doom port | `15510abb4015aca652cf1673000e784e88be3121` (`codex/1-doom-port`) | This repository; PR #2 is still a draft |
| Doomgeneric | `dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284` | Public Git submodule, initialized below |
| FM-1 board support | `20800a32d1c17389560f64005c007bc2c6f9444c` (`codex/4-doom-boot-support`) | [`fm1-tracker` PR #5](https://github.com/Keitark/fm1-tracker/pull/5), still a draft |
| Jieli WL82 SDK | `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d` | Separate SDK checkout at `references/source/fw-AC79_AIoT_SDK` inside the FM-1 checkout |
| Doom shareware package | `doom-wad-shareware_1.9.fixed-5_all.deb`, SHA-256 `5802f176c0303e228095b5312def53de602781cf4c53e79842257484a0d9e938` | Downloaded and verified by the build script |

The FM-1 board project and Jieli SDK are not vendored in this repository. The
SDK must be provisioned separately from an authorized source, and its checkout
must be clean and at the pin above. The target scripts also expect the Jieli
PI32 toolchain under `C:\JL\pi32\bin` (including `clang.exe`, `llvm-ar.exe`,
`llvm-nm.exe` and `llvm-objcopy.exe`). These dependencies are not installed by
the Doom build script. Python 3.10 or newer, Git, PowerShell, and network access
to GitHub and Debian are required. The asset converter itself uses Python's
standard library and keeps the fetched IWAD and generated assets in ignored
`build/` output.

## Clean-checkout build

Run these commands in PowerShell. Use separate checkout directories as shown,
or substitute your own absolute paths.

```powershell
git clone https://github.com/Keitark/fm1-doom.git F:\dev\fm1-doom
Set-Location F:\dev\fm1-doom
git checkout 15510abb4015aca652cf1673000e784e88be3121
git submodule update --init

git clone https://github.com/Keitark/fm1-tracker.git F:\dev\fm1-tracker
git -C F:\dev\fm1-tracker fetch origin codex/4-doom-boot-support
git -C F:\dev\fm1-tracker checkout --detach 20800a32d1c17389560f64005c007bc2c6f9444c
```

Provision the SDK at
`F:\dev\fm1-tracker\references\source\fw-AC79_AIoT_SDK`, then verify its
revision and clean state:

```powershell
git -C F:\dev\fm1-tracker\references\source\fw-AC79_AIoT_SDK rev-parse HEAD
git -C F:\dev\fm1-tracker\references\source\fw-AC79_AIoT_SDK status --short
Test-Path C:\JL\pi32\bin\clang.exe
```

The first command must print
`e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`; the second must print nothing, and
the toolchain check must return `True`.

Before building Doom, create the reviewed peripheral SMB1 boot baseline
expected by the target linker. Run this from the FM-1 checkout; it performs an
offline build and static audit and does not flash a device:

```powershell
Set-Location F:\dev\fm1-tracker
python .\firmware\nes\build_boot.py --profile peripherals --rom smb1 --out .\firmware\nes\build\boot-stock-power-smb1
```

Return to the Doom checkout and run the one-command asset and target build:

```powershell
Set-Location F:\dev\fm1-doom
python .\tools\build_shareware_app.py --fm1-root F:\dev\fm1-tracker --output-dir build\shareware-rom
```

The builder downloads the pinned Debian package, checks its byte count and
SHA-256, extracts the local IWAD and copyright notice, generates the E1M1
archive and audio banks, compiles the target, and writes the app and manifests
under ignored `build/shareware-rom/`. To use an existing uncompressed shareware
IWAD instead, add `--wad C:\path\to\doom1.wad`.

## Verify the result

For the pins and toolchain used on 2026-10-09, the build produced:

| Field | Expected value |
| --- | --- |
| App size | `567376` bytes |
| App SHA-256 | `ca1da356db77b36496fde06e6da4e7ccb32b38525241a1a88ef4626bbc7eca39` |
| Status | `uboot_app_input_ready_unflashed` |
| `hardware_boot_verified` | `false` |
| `device_operations_performed` | `false` |

Compare the local receipt and binary:

```powershell
Get-Content .\build\shareware-rom\shareware-app-manifest.json
Get-FileHash .\build\shareware-rom\app.bin -Algorithm SHA256
```

Different compiler/toolchain inputs can change the output hash. The target
manifest records the SDK and source-closure hashes for the build actually
produced; preserve it with the app when comparing a non-identical build.

## Hardware boundary

`app.bin` is the plain SDK application input for a WL82 UBOOT writer that
supports `-app app.bin`. It is **not** a full flash image or raw bytes to write
at a guessed flash offset. The build script does not open a serial port, invoke
a writer, or flash the FM-1.

To claim the firmware is running, the exact candidate still needs a separate
device deployment with the supported writer, successful sector and full-image
readback, then a normal boot check covering serial/game counters, LCD, keys,
music and effects. Earlier Doom revisions passed some of those hardware checks;
they do not verify the candidate listed in this guide. Current physical boot,
runtime memory, audio stability and 30-FPS acceptance remain unverified for
this app hash.

Use a normal USB data cable for post-boot CDC/UAC observation. During the
original bench checks, the programmer cable did not expose the normal-boot USB
interfaces; switching to a regular data cable restored them.

The downloaded Doom shareware package is classified as non-free by Debian.
The IWAD, copyright notice, generated assets and app stay in ignored local
build output; they are not included in this source repository.
