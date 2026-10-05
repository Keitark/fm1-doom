# Direct E1M1 low-memory menu build

This is the original Doom engine, generated from the pinned clean Doomgeneric
submodule. The build keeps E1M1 gameplay and the selected 8×8 asset pixels.
It renders internally at 160×100. Gameplay samples form exact 8×8 blocks on
the FM-1's 240×240 RGB565 LCD (30 columns by 28 rows); the bottom 16 LCD rows
keep a finer health/ammo HUD. The original Doom menu patches are drawn at half
size into the engine buffer and use finer LCD scaling so their artwork is
readable. LCD transfer bytes per frame remain 115,200.

`tools/make_lowres_engine.py` copies the engine into ignored
`build/lowres-source`. It forces a full view, replaces the 320-wide status bar
and text HUD with a small health/ammo HUD, shrinks the original menu images,
and places immutable state/actor tables in flash. The menu offers New Game
(E1M1, four skills) and Options (graphic detail). Slot 19 opens the menu, goes
back from a submenu, then closes it; up/down navigate, and slot 22 or fire
selects. Use returns from a submenu. The menu draws no custom text. Fast and
nightmare skill, networking, text messages, level completion, save/load, and
audio are outside this profile. The
generated copy also uses 16 local command backup tics and a 64-visplane limit;
the engine reports an error if that plane limit is reached. The upstream
submodule and all game data remain unmodified and uncommitted, respectively.

The startup audit sizes drawing lookups to the fixed screen, moves the lump
directory into the Doom zone, and disables transition wipes (48,000 B peak).
The optimized converter preserves every output pixel while sampling each
8×8 gameplay block once. A bounded 7 KiB inflater arena avoids SDK heap
allocation. See [AUDIT.md](AUDIT.md) for tests and the current memory budget.

## Reproduce on Windows

```powershell
git submodule update --init
python tools/make_lowres_engine.py
cmake -S . -B build/lowres -G "Visual Studio 17 2022" -A x64 -DFM1_DOOM_ENGINE_DIR="$((Resolve-Path build/lowres-source).Path)" -DFM1_DOOM_EXPERIMENTAL_WIDTH=160 -DFM1_DOOM_EXPERIMENTAL_HEIGHT=100
cmake --build build/lowres --config Release
ctest --test-dir build/lowres -C Release --output-on-failure
python tools/stage_wad.py C:\path\to\doom1.wad build\stage-menu-ui.wad --map E1M1 --silent --prune-graphics --prune-sprites --no-attract-art --no-ui --menu-ui --pixelate 8
python tools/pack_archive.py build\stage-menu-ui.wad build\doom1.wad --block-size 4096
$env:PATH = "C:\Program Files\Git\mingw64\bin;$env:PATH" # zlib1.dll
$env:FM1_DOOM_ZONE_KIB = '448'
.\build\lowres\Release\fm1_doom_host.exe build\doom1.wad 120 build\lowres.ppm 2
$env:FM1_DOOM_HOST_MENU_SCRIPT = 'open'
.\build\lowres\Release\fm1_doom_host.exe build\doom1.wad 80 build\menu.ppm 2
Remove-Item Env:FM1_DOOM_HOST_MENU_SCRIPT
Remove-Item Env:FM1_DOOM_ZONE_KIB
```

To measure 32-bit allocations without needing a 32-bit zlib DLL, build with
`-A Win32`, set `FM1_DOOM_ZONE_KIB=296`, and pass `build\stage-menu-ui.wad`
directly. `FM1_DOOM_HOST_KEYS=147456` holds the mapped forward and left keys
for a moving host smoke. The host also passes `-nogui` to Doom so allocation
failures return through the console instead of opening a dialog. The host menu
script also accepts `options`, `detail` (selects graphic detail with fire),
`resume`, and `restart`.

For the tested Doom shareware 1.8 IWAD, the menu profile staged WAD is
465,581 B and its FMD1 archive is 117,590 B with 4 KiB blocks/cache. A 32-bit
host at a 296 KiB Doom zone completed 80 ticks each with the menu open,
options selected, resume, and New Game restart. The prior no-menu profile
completed 120 idle and 300 moving ticks at the same zone size. These are short
host runs, not a full-level traversal or hardware frame-rate evidence.

## Offline FM-1 size diagnostic

```powershell
python tools/compile_target_engine.py --fm1-root F:\dev\fm1 --lowres
python tools/compile_target_port.py --fm1-root F:\dev\fm1 --lowres
python tools/link_target_probe.py --fm1-root F:\dev\fm1 --lowres --doom-only
```

The pinned SDK linker grants 523,596 B of `ram0`. The Doom-only size probe
includes an actual fixed 296 KiB target zone and 4 KiB archive cache. It links
with 28,112 B `.ram0_data` and 442,616 B `.ram0_bss`, totaling 470,728 B.
The linked heap span is 52,812 B before runtime allocations. Its app sections
are 361,072 B before WAD. Adding the selected 117,590 B archive gives 478,662 B
against the stock 602,112 B app allocation. This remains a size-only probe.

The [real SDK application candidate](TARGET_CANDIDATE.md) now links the local
archive into XIP and connects the Doom task, LCD, and key scanner. It is an
offline raw app image, not an update package or a verified runtime memory
budget. Audio, physical bring-up, and 30 FPS telemetry remain open.
