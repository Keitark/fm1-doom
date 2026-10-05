# FM-1 Doom UBOOT `app.bin` input

The current low-memory E1M1 build has a real FM-1 SDK `app_main`. It creates a
separate Doom and USB tasks so SDK event dispatch and diagnostics continue. The task validates a local
FMD1 archive linked in XIP flash, initializes the recovered stock LCD sequence,
starts the SPI2 DMA2 scanner with a paced interrupt, and runs Doom. LCD rows use
the working NES row origin zero and the 240×240 RGB565 strip interface. The
converter shares the LCD driver's persistent DMA strip. Scanner bits have a
10 ms stability filter. The scanner uses the SDK's hardware-interpolated
500 µs clock; the coarse 10 ms clock could falsely hit its 10 ms watchdog.
Input failures release keys and retry with 50–1000 ms backoff while rendering
continues, resetting the streak after one second of healthy sweeps. Engine errors, including LCD
transfer failures, record a bounded RAM message and stop the LCD/scanner;
USB status and serial UBOOT remain available. These paths need physical testing.

The first-stage game contains E1M1 only. Its original image menu offers New
Game and graphic detail; completing E1M1 starts E1M1 again rather than entering
an absent intermission/E1M2. Music and sound effects, save/load, and persistent
configuration are disabled. `fm1_doom_frames`, `fm1_doom_last_frame_ms`,
`fm1_doom_max_frame_interval_ms`, `fm1_doom_slow_frames`, `fm1_doom_stage`, and
`fm1_doom_fault`, and `fm1_doom_error_message` are RAM diagnostics. They are
not a completed 30 FPS test.

## Reproduce without touching the device

Use a lawful local Doom shareware IWAD. The commands create only ignored local
artifacts; no game data is committed.

```powershell
git submodule update --init
python tools/make_lowres_engine.py
python tools/stage_wad.py C:\path\to\doom1.wad build\stage-menu-ui.wad --map E1M1 --silent --prune-graphics --prune-sprites --no-attract-art --no-ui --menu-ui --pixelate 4 --weapon-pixelate 1
python tools/pack_archive.py build\stage-menu-ui.wad build\menu-ui-4k.wad --block-size 4096
python tools/compile_target_engine.py --fm1-root F:\dev\fm1 --lowres
python tools/compile_target_port.py --fm1-root F:\dev\fm1 --lowres
python tools/build_target_candidate.py build\menu-ui-4k.wad --fm1-root F:\dev\fm1
```

The builder requires the pinned SDK and a reviewed local FM-1 boot baseline.
It verifies every FMD1 block and CRC, checks that the exact archive bytes land
in the ELF's read-only `.text` and extracted app image, then enforces the stock
584,956 B V14/v32 application slot and 523,596 B RAM0 limit. It compiles the current
board root and LCD/key sources; unchanged baseline boot sources must still
match their recorded hashes. Outputs are
`build/target-candidate/fm1-doom-candidate.elf`,
`build/target-candidate/fm1-doom-candidate.app.bin`,
`build/target-candidate/app.bin`, and
`build/target-candidate/build-manifest.json`. **Use `app.bin` as the plain
application input for a WL82 UBOOT flow that accepts the SDK's `-app app.bin`
format.** It is the exact concatenation of `.text`, `.data`, `.dynamic_data`,
`.ram0_data`, and `.cache_ram_data`, with XIP entry `0x02000120`. It is not a
complete 1 MiB flash dump or an encrypted raw application region; the UBOOT
tool must handle its normal encoding, directory CRCs, and placement. Never
write these plain bytes directly at physical flash address `0x4120`.

The builder sets `flashable: true` for this UBOOT **input format** and
`hardware_boot_verified: false`. The generated file includes derived game
data and is ignored by Git; keep it private. Check its size and SHA-256
against the manifest before loading it in your UBOOT program.

For the locally tested shareware 1.8 archive (181,995 B), the candidate links
at 569,840 B app bytes. `.ram0_data` is 26,704 B and `.ram0_bss` is 447,784 B.
The linker heap span is 49,068 B. The reviewed task/queue/idle allocation is
42,584 B, plus an 800 B initialization allowance and 1,236 B of measured USB
allocations, leaving 4,448 B before remaining allocations and allocator padding.
The Doom and USB tasks each request 2048 SDK words (8 KiB).
The inflater uses a 7 KiB static arena and explicit allocation callbacks;
the lump directory uses the fixed Doom zone. Both are included in static RAM.
The builder requires at least 4096 B of reserve after the reviewed startup
budget. The archive is included in those app bytes,
not added afterward. The ELF/app bytes and archive hashes are in the local
manifest. This proves the UBOOT input's offline layout and static placement,
not boot or runtime memory safety.

## Existing protected writer

The SDK downloader accepts `-app app.bin`. The local protected writer already
accepts compiled builds through its preparation path, and writes only changed
4 KiB sectors. Its internal request contains a complete encoded image for
comparison and readback; it does not rewrite all flash sectors.

The Doom adapter prepares that existing request from `app.bin`, its manifest,
and the protected session's verified current readback. It checks source/header
hashes, app/ELF hashes, directory and payload CRCs, preserved boot/config/tail
bytes, and simulated write/restore. It performs no device I/O and stores no key:

```powershell
python tools/prepare_flash_request.py --fm1-root F:\dev\fm1 --build build\target-candidate --session "C:\Program Files\FM1FlashSession-<verified-current-session>"
```

Use the resulting private request with the existing `flash-session-client.ps1`
`plan`, `enter_uboot`, and `flash` operations. After one reset, `observe` verifies
the Doom CDC identity, running engine and advancing LCD transfer counters.
Physical LCD/input observation is also required. `DOOM STATUS` reports stage,
fault, frames, LCD/key errors, clocks and the bounded engine error message.
`DOOM STOP` shuts down peripherals cooperatively; `UBOOT` and the existing
confirmation handshake enter download mode only after Doom has stopped.

## Deployment and remaining gates

The first 493,104 B application was flashed on 2026-10-05 and its complete
1 MiB readback matched the prepared image. Decoding that readback reproduced
the exact app bytes at offset `0x4120` and valid payload/directory CRCs.
The user observed a blank LCD after reset and a normal power cycle.
That attempt did not establish working Doom boot. The revised 504,592 B build
adds the diagnostic channel and the NES startup corrections described above.
Its flash/readback/reset and CDC boot passed. The engine reported
`Unknown configuration variable: 'joystick_physical_button'` before frame zero:
the SDK formatter omits `%i`. Generated printf formats now use equivalent `%d`,
while scanf `%i` remains unchanged. Serial UBOOT recovery worked after the fault.
The final correction passed six-sector/full-readback verification and reset.
CDC now reports a running engine and advancing frames with zero faults; the
user confirms visible gameplay. Controls and performance remain under test.
See [DEPLOYMENT.md](DEPLOYMENT.md).

- The 32-bit Windows host completes 300 moving ticks and 80-tick menu/restart/
  exit scripts with a 296 KiB Doom zone, including the zone-backed lump
  directory. Transition wipes are disabled. A virtual-IWAD host run starts from an FMD1 archive even
  when no `doom1.wad` file exists on the filesystem.
- Host tests check LCD commands, row-zero placement, eight-row transfer, failure
  propagation, key debounce, and every optimized output pixel against the
  original mapper. SDK-header inflater tests use a real desktop zlib DLL and
  decode all 114 local archive blocks; target compilation checks the SDK binding.
- The actual FM-1 must still verify startup, power handoff, XIP reads,
  decoder heap use, task stack, LCD/key behavior, and the full ELF/update
  layout. Sound and 30 completed distinct gameplay frames/s remain
  unimplemented or unmeasured. USB recovery is implemented and needs device evidence.
- Preserve the known-good installed v32 image and a reviewed restore path before
  any physical write. No device operation is performed by the builder.
