# FM-1 Doom deployment, 2026-10-05–06

## Current result

The original engine reaches running stage 4 on the FM-1. The user confirms
that Doom gameplay is visible. Serial diagnostics report zero engine, LCD and
key-scanner errors in the revised build. The previous build's movement/menu/fire
worked after a power cycle, then keys stopped with `key_error=-3` while frames
continued. That scanner clock/recovery failure is corrected; sustained physical
   input and the new presentation toggle were confirmed working by the user.
Audio is disabled, and the requested 30 FPS target is not met by this profile:
the 60-second Detailed-mode capture shows roughly 11 frame-counter increments per second.
This is not a distinct-frame benchmark or a full E1M1 traversal.

## Exact artifacts

| Item | Value |
| --- | --- |
| Plain UBOOT `app.bin` | 569,840 B |
| App SHA-256 | `0463c496f4bf44a12af49ef0ab653808bffd8bc85ceda6ea7d73f036f4f4ef76` |
| Verified full-image SHA-256 | `9e51d686f98c3b735db3aa70f634727f1e9e216d1f55bffccebdc21423896f1c` |
| Application flash offset | `0x4120` |
| ELF/XIP entry | `0x02000120` |
| SDK revision | `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d` |
| Static RAM | 474,488 B |
| Linker heap | 49,068 B |
| Reviewed startup reserve | 4,448 B before other allocations/padding |
| Graphics archive | 181,995 B; original pistol/punch, 4×4 world/enemies |

The local build manifest freezes the source/header/configuration hashes. The
compiled archive remains private and is not included in the repository.

## Installation and observation

1. First image: 121 changed sectors and complete 1 MiB readback matched; user
   reported blank LCD after reset and cold boot.
2. USB diagnostic image: 124 changed sectors and complete readback matched.
   Reset and CDC boot succeeded. Doom reported stage 255, fault -30, frame zero:
   `Unknown configuration variable: 'joystick_physical_button'`.
3. Actual SDK formatter IR omits `%i` and consumes no argument for it. Generated
   printf conversions now use equivalent `%d`, including all three indexed
   configuration binders and fatal integer messages. Scanf `%i` is unchanged.
4. Serial STOP/UBOOT returned the device to download mode after the fatal error.
5. Final correction changed only `0x6d000`, `0x6f000`, `0x70000`, `0x71000`,
   `0x72000`, and directory sector `0x4000` last. One complete 1 MiB readback
   matched; its decoded app reproduces the built bytes and valid payload CRC.
6. One reset succeeded. COM10 identified `DOOM-FM1/1`; frames advanced 133 to
   167 across fresh status polls, with zero faults. User then confirmed gameplay.
7. After a power cycle the user confirmed movement, menu and firing, followed
   by a permanent input freeze. COM6 showed stage 4, advancing frames and
   `key_error=-3`. SDK IR confirms `timer_get_ms()` advances in 10 ms steps,
   equal to the scanner's entire 10 ms watchdog. Rapid polls across one tick
   can falsely timeout, and three lifetime retries then disabled input.
8. The scanner now uses `jiffies_half_msec()*500`, with 50–1000 ms recovery
   backoff and a one-second healthy period before resetting retries.
   A separate host stress case exposed 16 KiB composite texture allocation
   failure; a bounded 135-byte column cache removes that allocation.
9. Original pistol/punch patches and finer enemy/world assets are installed.
   D♯4 toggles Smooth 8×8 and Detailed 160×93 gameplay, both filling 240×240.
   Original menus stay detailed. Weapon projection uses the original 320-wide
   art coordinates. Normal CTest 5/5, x86 CTest 4/4 and Python 30/30 pass;
   all 145 archive blocks decode with the bounded inflater. Hybrid assets
   pass 600 firing/moving host ticks at a 296 KiB zone; Detailed passes 120.
10. This revised image wrote 140 changed sectors, directory last. Full 1 MiB
    readback matched, one reset succeeded, and COM6 showed stage 4, frames
    114→148 with zero engine/LCD/key errors.
11. A 60-second live capture produced 61 status lines: frames 629→1292,
    every engine/LCD/key error zero. Detailed mode remained selected, scanner
    sequences and IRQ completions advanced, retry count stayed zero and no
    scanner failure was recorded. The approximate counter rate is 11.05/s;
    this is not a distinct-render-frame benchmark. The user confirmed stable
    movement/firing and both presentation modes working without a freeze.

Representative device status:

```text
DOOM STATUS stage=4 fault=0 frames=148 stopped=0 lcd_stage=4 lcd_error=0 key_error=0 sys_hz=360000000 lsb_hz=60000000 error=
```

Protected receipts/logs are in the local session
`C:\Program Files\FM1FlashSession-3ff488471a1f45859099b12c58b418dc`:

- Current deployment/readback: `runs/9c3f25636fef418e8250f22e60c86b69`.
- Current reset: `runs/127184c5570047c39c0333c7ee712541`.
- Current CDC observation: `runs/955a2b220b1c44578f0789995aab41f7`.
- Current serial UBOOT entry: `runs/b18950ffa4a641609e572ae626ba7a18`.
- Prior verified image: `runs/a0bcbb98b06f4c0694dd9a9c87eefefd`.

No raw image, game data, device key or binary is committed.
Shared support changes are in
[fm1-tracker PR #5](https://github.com/Keitark/fm1-tracker/pull/5), based on
the existing tracker branch. The Doom PR remains draft pending runtime
stack/heap, full-level behavior, audio and performance acceptance.
