# FM-1 Doom

The **original Doom engine** on the M-VAVE FM-1, based on
[Doomgeneric](https://github.com/ozkl/doomgeneric) pinned at
`dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284` under GPL-2.0.

## Current checkpoint

The live sound editor from source `626d2612` is installed: **565,744 B**, app
SHA-256 `70553874...`. All 139 application sectors and the complete 1 MiB image were
readback-verified. One reset reached `DOOM-FM1/1`, stage 4/fault 0, advancing
gameplay and audio, and zero LCD/key errors. RAM and emitted USB stack gates
pass; 19,212 B remain in the reviewed app slot. Earlier revisions established
visible gameplay, menus, movement, firing, music and physical master volume.
Physical acceptance of the new controls, speaker output and timing remains
pending. See [deployment evidence](DEPLOYMENT.md#live-editor-deployment-2026-10-07).

The current scope is **E1M1** with its complete original geometry. Gameplay uses
all **160×93** rendered samples, scaled to **240×224**, with a **16-row HUD**.
The engine buffer is 160×100. World/enemy assets retain 4×4 pixel blocks, the
pistol retains original detail, and the fist uses 2×2 asset blocks. These asset
settings differ from screen resolution. The former 8×8 presentation mode and
D♯4 view toggle have been removed from the source.

Music boots in original DOS OPL mode, confirmed by read-only device telemetry.
A switchable eight-voice synth now has
live VCO, VCF, VCA and room-reverb controls. Original OPL output remains exact
against the host reference; new control and delay-buffer regressions pass.
Physical acceptance of the new controls remains pending. See [audio](AUDIO.md).

The reported missing cold-boot USB was resolved by replacing the programmer
cable with a normal USB data cable. Windows then exposed healthy COM5 and a
UAC capture endpoint; read-only observation showed advancing gameplay with
zero LCD/key errors. A firmware cold-boot defect was not established.

The hardware target remains 30 completed gameplay frames/s. Earlier observed
builds did not meet that target; the new revision has no completed distinct-frame
rate measurement.
See [performance](PERFORMANCE_TARGET.md), [bench evidence](PORT_STATUS.md),
[deployment history](DEPLOYMENT.md) and [issue #1](https://github.com/Keitark/fm1-doom/issues/1).

## Controls

| Physical note | Stock slot | Doom action |
| --- | ---: | --- |
| F3 / G♯3 / G3 / A3 | 14 / 17 / 16 / 18 | Left / forward / backward / right |
| G5 / F5 (far-right white keys) | 40 / 38 | Fire / use |
| F♯3 / B3 | 15 / 20 | Run / strafe modifier |
| A♯3 / C♯4 | 19 / 22 | Menu/back / enter; fire also selects in the menu |
| C4 / D4 | 21 / 23 | Weapons 1 / 2 |
| E4 | 25 | Toggle original OPL / synth music |

These fixed physical-note names come from the recovered stock scanner table;
MIDI octave/transposition settings do not change the Doom controls.

### Live sound editing

Turn PRESETS to Warm, Acid or Room to enable synth editing. Original selects
the DOS OPL path. E4 toggles OPL/synth while retaining the last synth recipe and
knob settings. Turning knobs alone preserves the E4 mode choice.

| Physical control | Assignment | Default / range |
| --- | --- | --- |
| PRESETS | Original, Warm, Acid, Room | Original / 0–3 |
| ALGORITHM | Classic, Triangle, Pulse, Mixed band-pass | Classic / 0–3 |
| KNOB1 | VCO detune | 16 / 0–127 |
| KNOB2 | VCF cutoff, with melodic envelope sweep | 72 / 0–127 |
| KNOB3 | VCA contour; raising it lengthens attack/release | 32 / 0–127 |
| KNOB4 | Small-room reverb wet amount | 0 / 0–127 |
| Physical volume knob | Master music/effects gain through PB6/ADC4 | Follows the knob |

Warm keeps the existing melodic synth character; Acid raises the filter sweep;
Room softens the filter and lengthens the envelope. Reverb affects the synth
melody; original OPL drums and PCM effects retain their existing paths. Changes
smooth without restarting notes. Read-only `DOOM EDIT` reports preset,
algorithm, `vco`, `vcf`, `vca`, `reverb` and actual `synth_mode` over CDC.

### Leaving the spawn area

Walking straight from spawn reaches a normal wall; the opening is to the right.
Turn right with A3, move with G♯3, then turn left with F3 into the corridor.
Turn right farther along toward the first door and press F5 once nearby.
Release F5 before using another door. Holding USE continuously does not activate
later doors, and repeated presses can reverse an opening door.

The original texture-zero entry is retained so the first door renders properly.
The new sound revision partitions the fixed memory into a **294 KiB engine
zone plus a 2 KiB room delay**. A 1,200-tick 32-bit host route at this budget
crossed the door and reached active enemies without allocation failure. Exiting
currently reloads E1M1. Whole-level physical acceptance remains open.

## Build and test on Windows

```powershell
git submodule update --init
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\fm1_doom_host.exe C:\path\to\your\IWAD.WAD 120 build\frame.ppm 2
```

Host arguments are IWAD path, engine ticks, output PPM path and optional Doom
zone size in MiB (default 6). The runner starts E1M1 without input and disables
sound. It produces the video adapter's output locally. Private audio banks and
reference fixtures enable the additional music/effects contracts; see
[AUDIO.md](AUDIO.md).

## Prepare the current E1M1 assets

Supply a lawful original Doom shareware IWAD locally:

```powershell
python tools/stage_wad.py C:\path\to\doom1.wad build\stage-menu-ui.wad --map E1M1 --silent --prune-graphics --prune-sprites --no-attract-art --no-ui --menu-ui --pixelate 4 --weapon-pixelate 1 --fist-pixelate 2 --compact-assets
python tools/pack_archive.py build\stage-menu-ui.wad build\menu-ui-4k.fmd --block-size 4096
python tools/make_sound_bank.py --wad C:\path\to\doom1.wad --menu --output-dir build\sound-bank
python tools/make_music_score.py C:\path\to\doom1.wad build\music-original
python tools/make_genmidi_bank.py C:\path\to\doom1.wad build\opl-bank
```

The staged WAD keeps original image menus and map geometry. Its zero-length
music marker saves WAD-cache space; playback uses the separate immutable score.
Lossless patch-column/payload sharing and patch-index compaction preserve the
decoded graphics. Regenerate from the original IWAD: an older pruned archive
may already have lost `AASTINKY` and `WALL00_3`, needed to retain texture index
zero and keep `BIGDOOR2` nonzero. The tested profile is 553,048 B as a WAD and
170,636 B as a 4 KiB-cache FMD1 archive. Sizes depend on the input.

FMD1 is a compressed virtual WAD format. For host IWAD discovery, copy the
archive to a local `doom1.wad`; the reader recognizes its format. The Windows
runner needs `zlib1.dll` on PATH, such as Git for Windows supplies.

```powershell
Copy-Item build\menu-ui-4k.fmd build\doom1.wad
.\build\Release\fm1_doom_host.exe build\doom1.wad 120 build\e1m1.ppm 2
python tools/pack_archive.py build\menu-ui-4k.fmd build\restored.wad --unpack
```

See [low-memory build instructions](LOW_MEMORY_EXPERIMENT.md) for the generated
160×100 engine and constrained host runs.

## Target candidate

Use the pinned SDK/toolchain through the existing
[FM-1 board project](https://github.com/Keitark/fm1-tracker):
use its `codex/4-doom-boot-support` branch (companion
[PR #5](https://github.com/Keitark/fm1-tracker/pull/5)) for the shared scanner,
encoder, LCD, ADC and audio sources. Target builds also require the reviewed
local boot baseline described in [TARGET_CANDIDATE.md](TARGET_CANDIDATE.md).

```powershell
python tools/compile_target_engine.py --fm1-root F:\dev\fm1 --lowres
python tools/compile_target_port.py --fm1-root F:\dev\fm1 --lowres
python tools/build_target_candidate.py build\menu-ui-4k.fmd --fm1-root F:\dev\fm1 --music-bank build\music-original\music_score.c
```

These commands compile/link/package locally. The private `app.bin` is the plain
WL82 UBOOT input for a writer supporting SDK `-app app.bin`. The reviewed
V14/v32 application allocation is **584,956 B**. The installed 565,744 B image
leaves 19,212 B. Preserve the 4,096 B reviewed startup reserve and emitted USB
stack gate. SDK aggregate `heap_free` is not the largest free block.
See [candidate format and gates](TARGET_CANDIDATE.md) and
[port audit](AUDIT.md).

## Source and asset boundaries

This public repository distributes source and build tools. Supply lawful game
assets separately, such as the Doom shareware WAD or Freedoom; the selected
small target profile is measured against original shareware E1M1. Proprietary
WADs, generated original music/effect banks, stock firmware dumps, device keys,
private bench receipts and flashable images remain excluded. Generated game
data and binaries belong in ignored `build/`.

The port converts the indexed framebuffer into eight-row RGB565 LCD strips,
feeds stock-scanner key edges to Doomgeneric, and exposes a checked FMD1 virtual
WAD reader backed by a bounded decode cache. Shared board support owns LCD,
scanner, ADC and IIS integration. USB provides CDC and capture-only UAC1 at
44.1 kHz stereo PCM16, tapped before physical master gain. Diagnostics include
`DOOM AUDIO`, `DOOM VOLUME`, `DOOM USB_AUDIO`, `DOOM GAME` and frame capture;
see [audio diagnostics](AUDIO.md#usb-diagnostics).
