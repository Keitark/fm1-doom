# Cold-boot USB investigation — 2026-10-07

## Current result and constraint

The user identified the exact cause: the programmer cable was connected.
Switching to a normal USB data cable restored the connection. With Doom
running, Windows exposes healthy COM5, composite USB and the
UAC recording endpoint. The existing installed application responds to HELLO
as DOOM-FM1/1; stage4/fault0, frames1375->1409, LCD/key errors0. No additional
firmware was flashed. This connection failure no longer supports a claim of a
confirmed Doom cold-startup defect. PCM and master volume remain accepted.

The user's instruction remains: do not flash speculative firmware before
identifying a cause. Earlier task-order changes were not a proved fix.

Installed application: `3cf7f681cd4ae08aab2507a33a816a704bb87ed8b21392d16f1c8cdd410bad7a`.
Full flash readback: `af92a30b658ee2f878c9b410ee53bf50391af9e3f3d86470500ada01de12f1f9`.
USB/peripheral startup sequencing was changed and flashed before its causal
role was proved. Warm reset succeeded; the next cold power cycle failed.
That experiment is unsuccessful, not a confirmed fix.

## Evidence

- Warm reset: COM4 and UAC recording endpoint enumerate, HELLO identifies
  DOOM-FM1/1, stage4/fault0, frames123->157, LCD/key errors0.
- The writer's `reset 1` maps to loader `RUN_APP(1)` (command 0xFC0C). The
  exact retained RAM helper disables USB, then sets CLOCK.PWR_CON bit4 and
  waits for software reset. It is not a direct jump to another application;
  argument1 selects zero delay, not an application boot argument. It does
  not remove device power, and retained peripheral state versus power loss
  was not measured. Do not equate this test with a cold power cycle.
- Cold power cycle: user reports no USB; Windows finds no FM-1 interfaces.
- Earlier `ce8176d8` cold-boot LCD photo: USB stage2/error0, advancing
  heartbeat; USB0_CON0 startup snapshots `0x8001 -> 0x2883D`. PHY/reset
  enable bits are set. These are startup snapshots, not live register reads.
- The snapshot's USB0_CON0 read includes interrupt status. Available primary
  WL82 headers do not establish its read side effects. The SDK itself reads
  CON0 before each indirect SIE transaction, including IRQ-status collection;
  the separate BR17/BR21/BR25 documentation does not prove reset/setup loss on
  WL82. Treat this measurement as potentially intrusive, not a confirmed
  regression or cause; the cold USB report predates this diagnostic.
- A subsequent read-only Windows hub query finds port22 connected to
  VID4C4A/PID8057 (UBOOT). This is a different physical state; it does not
  reveal the failed application's port status. No further reset/write was
  issued after the user's no-flash instruction.
- With the working cable, the actual device is on port11 of the same root
  hub, not the earlier port22. Always resolve current PnP parent/location
  before querying a cached hub port; port22 now correctly reports no device.
- Read-only writer observation run47dc57e02e7b46b8b41f9f511fa5c15f verifies
  COM5/DOOM-FM1/1 and advancing game frames. Windows also reports a healthy
  recording endpoint. This check did not record new audio or change firmware.

## NES comparison

Reference source: public NES `9fa8d235`; retained compiled reference:
`fm1-public/build/firmware-mic-smb1`, application `b43dc28c...`, same pinned
SDK `e30b1ee3`. Source and compiled-reference provenance are distinct.

- Shared board.c and USB app_config.h match public NES.
- Compiling the 19 SDK C sources with boot versus USB app_config changes
  source-location metadata only, not functional LLVM IR. This does not by
  itself prove every reused object matches a fresh full build.
- Comparing all 34 actual reused baseline objects against the retained NES
  build found 33 functionally identical LLVM modules. The remaining setup
  object differs only in build date/time print strings; all 12 assembly
  objects are byte-identical. The emitted 154-byte application entry matches
  after relocation normalization, and its RAM-copy/BSS-clear operands match
  the current ELF.
- Complete disassemblies of `clk_early_init`, `usb_g_sie_init`,
  `gpio_set_die`, and `usb_slave_init` match after normalizing relocated
  addresses and annotations. `usb_write_power` and `usb_write_intr_usbe`
  have the same operations/call targets; longer relocated call encodings
  shift local branch offsets by two bytes.
- The generated controller initialization follows the same SDK sequence:
  allocate configuration/EP0, install filter, initialize SIE, write power,
  initialize slave, assign DMA, enable reset/EP0 interrupts, register ISR.
- Capture-only Doom and duplex NES have different descriptors/stream
  adapters. Doom's policy admits its advertised interfaces2/3 and EP0x81;
  NES's earlier CDC-only policy regression is not present in that form.
- The linked configuration is 174 bytes within the SDK's 768-byte buffer.
  Descriptor construction and interface/reset handler registration happen
  before attachment; later GET_DESCRIPTOR requests reuse that buffer and
  allocate no heap. Configuration, CDC and EP0 allocation failures propagate
  to a nonzero USB startup error in the emitted application.
- USB0 control IRQ9 is registered on CPU0 at priority3. No later startup
  request/unrequest or vector/enable-register write replaces or disables it.
  CPU1 startup clears CPU1 interrupt enables, not CPU0 USB enables.
- Clock initialization and its linked frequency/divider tables match NES.
  It reads inherited registers and conditionally initializes USBPLL when
  USBPLL_CON0 bit1 is clear; the earlier reads-only description was wrong.
  The photo shows that bit enabled and PLL48 selected. Doom, NES and MDX use the same IIS platform parameters
  and 44100-Hz branch; that branch does not change USBPLL or CLK_CON1.
- LCD transfers leave interrupts enabled. The photographed F4 diagnostic
  runs before doomgeneric_Create and music initialization, so heavy OPL
  rendering cannot explain that particular failure.
- Endpoint DMA is aligned internal RAM covered by BSS clearing: Doom's
  CDC/EP0 pool is 0x01c6d280..0x01c6d680 and capture buffer is
  0x01c6c7c0..0x01c6c8c0. SDK setters store the full address with csync;
  internal RAM is unchanged by its SDRAM cache-alias helper.
- The packaged image decodes to the exact current application, with valid
  area/application CRCs and preserved stock boot/config bytes. There is one
  app_dir_head at 0x4000, one executable app.bin entry and no secondary
  app_dir_head2. No image-directory route selects an older Doom copy.

Local comparison artifacts are in ignored `build/cold-usb-audit/`:
`compare_boot_config.py`, `boot-config-diff.txt`, `compare_linked_usb.py`,
`linked-usb-comparison.json`, `linked-usb-diff.txt`, and a Windows hub reader
that issues only GET_NODE_CONNECTION_INFORMATION_EX for the cached FM-1 port.

## Acceptance limit

The programmer cable's electrical/protocol behavior was not captured. Do not
infer that the cable is broken or invent its internal wiring/trigger mechanism.
Use a normal USB data cable for application CDC/UAC operation. The new connection verifies serial operation
and UAC enumeration without a firmware update. Sustained unplug/replug or USB
audio streaming was not repeated for this connection check. Reopen this
investigation only if cold-boot failure reproduces with the working cable.
