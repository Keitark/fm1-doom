# Direct E1M1 low-memory experiment

This is the original Doom engine, generated from the pinned clean Doomgeneric
submodule. The build keeps E1M1 gameplay and the selected 8×8 asset pixels,
renders internally at 160×100, and scales to the FM-1's 240×240 RGB565 LCD.
LCD transfer bytes per frame therefore remain 115,200.

`tools/make_lowres_engine.py` copies the engine into ignored
`build/lowres-source`. It forces a full view, replaces the 320-wide status bar
and text HUD with a small health/ammo HUD, and places immutable state/actor
tables in flash. The direct-boot profile omits menu and intermission graphics,
so slot 19/Escape is disabled. Fast/nightmare skill, networking, text messages,
the menu, level completion, save/load, and audio are outside this profile. The
generated copy also uses 16 local command backup tics and a 64-visplane limit;
the engine reports an error if that plane limit is reached. The upstream
submodule and all game data remain unmodified and uncommitted, respectively.

## Reproduce on Windows

```powershell
git submodule update --init
python tools/make_lowres_engine.py
cmake -S . -B build/lowres -G "Visual Studio 17 2022" -A x64 -DFM1_DOOM_ENGINE_DIR="$((Resolve-Path build/lowres-source).Path)" -DFM1_DOOM_EXPERIMENTAL_WIDTH=160 -DFM1_DOOM_EXPERIMENTAL_HEIGHT=100
cmake --build build/lowres --config Release
ctest --test-dir build/lowres -C Release --output-on-failure
python tools/stage_wad.py C:\path\to\doom1.wad build\stage-no-ui.wad --map E1M1 --silent --prune-graphics --prune-sprites --no-attract-art --no-ui --pixelate 8
python tools/pack_archive.py build\stage-no-ui.wad build\doom1.wad --block-size 4096
$env:PATH = "C:\Program Files\Git\mingw64\bin;$env:PATH" # zlib1.dll
$env:FM1_DOOM_ZONE_KIB = '448'
.\build\lowres\Release\fm1_doom_host.exe build\doom1.wad 120 build\lowres.ppm 2
Remove-Item Env:FM1_DOOM_ZONE_KIB
```

To measure 32-bit allocations without needing a 32-bit zlib DLL, build with
`-A Win32`, set `FM1_DOOM_ZONE_KIB=296`, and pass `build\stage-no-ui.wad`
directly. `FM1_DOOM_HOST_KEYS=147456` holds the mapped forward and left keys
for a moving host smoke. The host also passes `-nogui` to Doom so allocation
failures return through the console instead of opening a dialog.

For the tested Doom shareware 1.8 IWAD, the no-UI staged WAD is 361,801 B.
FMD1 is 79,785 B with 4 KiB blocks/cache or 76,014 B with 16 KiB blocks/cache.
The 4 KiB variant produced the same final 240×240 image hash as the prior
191,534 B archive after 120 host ticks. A 32-bit host completed 120 idle ticks
and 300 ticks with held forward/left keys at a 296 KiB Doom zone. A 288 KiB
zone failed allocation in an earlier untuned build. These are short host runs,
not a full-level traversal or hardware frame-rate evidence. A 64-bit
AddressSanitizer Debug run completed 120 ticks on the 4 KiB archive.

## Offline FM-1 size diagnostic

```powershell
python tools/compile_target_engine.py --fm1-root F:\dev\fm1 --lowres
python tools/compile_target_port.py --fm1-root F:\dev\fm1 --lowres
python tools/link_target_probe.py --fm1-root F:\dev\fm1 --lowres --doom-only
```

The pinned SDK linker grants 523,596 B of `ram0`. The latest Doom-only probe
includes an actual fixed 296 KiB target zone and 4 KiB archive cache. It links
with 28,336 B `.ram0_data` and 449,320 B `.ram0_bss`, totaling 477,656 B.
The linked `_HEAP_BEGIN` to `_HEAP_END` span is 45,900 B before runtime
allocations. Its app sections are 363,888 B before WAD. Adding the selected
79,785 B archive gives 443,673 B against the stock 602,112 B app allocation.
This is not a verified runtime memory budget: the probe's `app_main` does not
start Doom, it uses nonfunctional libc shims, and target task stacks, SDK heap,
decompressor scratch, flash mapping, and real board services are not accounted
for. The zone size was chosen from a 32-bit Windows host run, not measured on
the AC7911B8.

No FM-1 firmware image has been built or run. The remaining work includes a
real SDK task, bounded reads from a protected flash placement, LCD/key/audio
bindings, a full ELF and stack/heap audit, and physical 30 FPS telemetry.
