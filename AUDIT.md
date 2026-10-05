# FM-1 Doom audit, 2026-10-05–06

The audit inspected the current sources, pinned SDK headers and library
bitcode, and the linked PI32V2 instructions. These corrections are implemented:

| Finding | Correction | Evidence |
| --- | --- | --- |
| Doom requested 8192 words, or 32 KiB, exceeding startup heap | Request 2048 words, or 8 KiB; enforce complete startup/USB budget | Six-task floor including USB and 4 KiB reserve is 48,716 B |
| SDK `uncompress` passes null callbacks that its inflater rejects | Explicit callbacks, 7 KiB static arena, one complete `Z_FINISH` call | SDK state allocation is 7,120 B; desktop binding tests decode 114 archive blocks and reject corruption/truncation |
| 493-entry lump directory consumes 13,804 B SDK heap | Allocate zeroed directory inside the fixed Doom zone | 32-bit 296 KiB host passes moving/menu/restart/exit scripts |
| Desktop error handler fails to report or shut down target I/O | Bounded RAM message, fault/stage update, peripheral stop and watchdog loop | Linked `I_Error` instructions call formatting, stop both peripherals and loop safely |
| Large drawing lookups retain 1120×832 capacities | Size lookup arrays for the fixed 160×100 screen | 6,768 B static RAM saved |
| Transition wipe peaks at three 16,000 B screen allocations | Remove captures, transpose and animation waits | 48,000 B transition peak removed |
| Eight identical LCD rows repeat palette sampling | Expand one sampled row and copy seven rows | Gameplay lookups fall from 53,760 to 840 per frame; all output pixels match the reference |
| CRC loops eight times per byte | Use two nibble table steps | Existing CRC tests and complete archive reads pass |
| Disabled console formats output that is discarded | Discard routine console output directly; retain fatal RAM message | SDK `printf` has no output transport |
| Build freshness ignores included headers | Check recursive headers/configuration and freeze their hashes | 86 engine/port objects checked; final closure includes 395 source/config/header files |
| Converter and LCD driver each hold a 3,840 B strip | Share the driver's persistent synchronous DMA strip and skip alias copies | Full pixel comparison and external-buffer contract pass |
| Switch/animation definitions occupy mutable RAM | Make both generated definition tables constant in XIP | 1,464 B tables; 1,472 B recovered in the actual link including alignment |
| Synchronous key-scanner startup can hide the first frame on failure | Reuse NES DMA2/paced IRQ scanning with neutral input and bounded retries | Working NES documented `KEY_START rc=-3 frame=0`; scanner BUSY is handled separately |
| LCD row origin differs from working NES | Use rows 0..239 | Strip tests cover first and last row bounds |
| Fatal errors have no transport | Keep CDC live with bounded status/error reporting and guarded serial UBOOT | Protocol tests and target CDC/UBOOT link-symbol checks pass |
| SDK formatter omits signed `%i` values | Normalize generated printf conversions to equivalent `%d`; retain scanf `%i` | Actual CDC reports unknown `joystick_physical_button`; SDK formatter IR lacks case 105; five normalization tests pass |
| Scanner's coarse 10 ms clock equals its 10 ms timeout | Use SDK hardware-interpolated half-millisecond clock | Actual frame counters continue while key error is -3; compiled SDK IR confirms clock granularity |
| Three lifetime input retries permanently disable controls | Continue bounded 50–1000 ms recovery, reset streak after one second healthy | Input remains neutral while recovery is pending; TRACE exposes progress and failures |
| Composite textures need fragmented 16/32 KiB zone blocks | Compose one column into a 135-byte static buffer | Stock C comparisons cover 2,711 columns per profile; formerly failing fire/movement stress passes 600 ticks |
| Weapon art uses 320-wide coordinates but new scale divides by 160 | Keep original 320-wide reference in weapon scale/inverse | Reciprocal scale/projection tests; visible firing pistol in host output |
| One coarse presentation cannot show fine sprites | Slot 24 toggles Smooth/Detailed; original pistol/punch plus finer world/enemy assets | Both modes match full pixel references; toggle holds/releases/simultaneous events pass; target struct size unchanged |

The trimmed main menu contains only New Game and Options. Its original image
patches and finer menu scaling remain. Gameplay toggles between exact 8×8 LCD
blocks and all 160×93 gameplay samples, with detailed HUD in both modes.

## Current offline candidate

- App: 569,840 B, SHA-256 `0463c496f4bf44a12af49ef0ab653808bffd8bc85ceda6ea7d73f036f4f4ef76`.
- Static RAM: 474,488 B; linker heap: 49,068 B.
- Reviewed tasks/queues/idle: 42,584 B; initialization allowance: 800 B;
  measured USB heap requests: 1,236 B; reserve: 4,448 B before other allocations and padding.
- Normal CTest: 5/5; low-resolution x86 CTest: 4/4; Python tests: 30/30.
- Target compilation: 79 engine files and 7 port sources; SDK link passes.
- Current-baseline update/restore simulation passes; the latest image changes 140 sectors, directory last.

These checks establish the build and modeled memory budget. Physical boot and
serial recovery work. Stack high-water use, full-map visibility, sustained
physical controls and 30 FPS still need device evidence. Audio remains absent.

## App input and writer

The existing SDK downloader accepts `-app app.bin`; the protected local writer
uses prepared build requests. Doom now has an adapter for that preparation
step, so the service protocol needs no modification. The adapter uses the
protected session's verified current readback, preserves boot/configuration/reserved data and
updates application encoding and CRCs internally. Its full image buffer is
private; the writer programs only changed sectors.

The first Doom image's full readback and decoded app matched, but the user
reported a blank LCD after reset and cold boot. Offset `0x4120`, directory and
payload CRCs are verified. The second image passed 124-sector/full-readback
verification and reset; CDC reported stage 255, fault -30 and zero frames:
`Unknown configuration variable: 'joystick_physical_button'`. The target SDK
formatter drops the `%i` index; printf normalization corrects all three
configuration-name binders and integer diagnostics. CDC remained usable after
the fatal error, and serial STOP/UBOOT returned the device to download mode.
The stock/working NES initializer drives PA2 low; that behavior is retained.
At that point physical display/input acceptance remained open.

The final printf correction passed six-sector and full-readback verification.
After one reset, CDC reports stage 4, advancing frames and zero engine/LCD/key
faults. The user confirms gameplay is visible. Physical controls and the
30 FPS target remain open; see [DEPLOYMENT.md](DEPLOYMENT.md). Subsequent input
failure was diagnosed and corrected as described above. The revised graphics
and scanner image passed 140-sector/full-readback verification, reset and CDC
observation with zero errors.
