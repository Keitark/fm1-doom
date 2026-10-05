# 160×100 engine experiment

The original Doom engine still draws E1M1, but this experiment halves each
dimension of its indexed framebuffer. The 8×8 asset pixelation and FMD1
archive are separate: they reduce WAD storage; the 160×100 engine variant
reduces render memory and work. The FM-1 LCD output remains 240×240 RGB565,
so its full-frame transfer cost is unchanged.

`tools/make_lowres_engine.py` checks the pinned, clean Doomgeneric submodule
and copies it to ignored `build/lowres-source`. In the copy it changes the
screen constants to 160×100, forces an 11-block full view to keep the renderer's
view tables in bounds, and suppresses the original 320-wide status bar. The
FM-1 video adapter draws a small health/ammo HUD along the bottom. No vendor
source or game data is committed.

```powershell
git submodule update --init
python tools/make_lowres_engine.py
cmake -S . -B build/lowres -G "Visual Studio 17 2022" -A x64 -DFM1_DOOM_ENGINE_DIR="$((Resolve-Path build/lowres-source).Path)" -DFM1_DOOM_EXPERIMENTAL_WIDTH=160 -DFM1_DOOM_EXPERIMENTAL_HEIGHT=100
cmake --build build/lowres --config Release
ctest --test-dir build/lowres -C Release --output-on-failure
python tools/stage_wad.py C:\path\to\doom1.wad build\stage.wad --map E1M1 --silent --prune-graphics --prune-sprites --no-attract-art --pixelate 8
python tools/pack_archive.py build\stage.wad build\doom1.wad --block-size 16384
$env:PATH = "C:\Program Files\Git\mingw64\bin;$env:PATH" # zlib1.dll
$env:FM1_DOOM_ZONE_KIB = '448'
.\build\lowres\Release\fm1_doom_host.exe build\doom1.wad 120 build\lowres.ppm 2
Remove-Item Env:FM1_DOOM_ZONE_KIB
```

For the tested Doom shareware 1.8 E1M1 archive, 120 host ticks completed at
448 KiB of Doom zone. A 384 KiB zone failed allocation. This bounds only a
short, idle, soundless host run; it does not cover a full level or FM-1 SDK
memory. An AddressSanitizer Debug run at 768 KiB completed 120 ticks. Both
the default and 160×100 host test suites pass. The displayed HUD was inspected
in the local PPM output.

The offline pi32v2 probe still retains the NES application and nonfunctional
libc shims. It is a size diagnostic, not firmware. Its last measured 160×100
build used 433,296 bytes of application sections before WAD data and
343,088 bytes of static RAM (`.ram0_data` plus `.ram0_bss`). With the selected
191,534-byte archive, application sections plus data reach 624,830 bytes,
22,718 bytes above the stock 602,112-byte application region. The remaining
RAM is well below a 448 KiB zone. These figures need refreshing after any
adapter change.

```powershell
python tools/compile_target_port.py --fm1-root F:\dev\fm1 --lowres
python tools/compile_target_engine.py --fm1-root F:\dev\fm1 --lowres
python tools/link_target_probe.py --fm1-root F:\dev\fm1 --lowres
```

No FM-1 image has been built or run. The 30 FPS goal requires a further memory
redesign, a safe flash layout, a real board binding, and device measurements.
