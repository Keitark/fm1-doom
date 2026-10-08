# Doom allocator capacity correction, 2026-10-08

The NES FX candidate passed the former raw linker-heap gate but cannot fit the
allocation state measured in the previous running live-editor image. This is a
confirmed capacity and build-gate regression. It does not establish which
allocation failed on the remote device or prove that it caused every observed
startup symptom.

## Exact comparison

| Item | Running live editor | NES FX candidate |
| --- | --- | --- |
| Application SHA-256 prefix | `70553874` | `4f3871be` |
| Complete encoded-image SHA-256 prefix | `a4c3fc49` | `d6884aae` |
| Heap begin | `0x01c74fc0` | `0x01c75000` |
| Heap end | `0x01c7fd2c` | `0x01c7fd2c` |
| Raw linker heap | 44,396 B | 44,332 B |
| Maximum page-grown allocator footprint | 41,024 B | 40,960 B |
| Upper bound on reported used bytes | 40,936 B | 40,872 B |
| Measured reported used bytes | 40,888 B | No successful runtime sample |

The former running image reported `heap_free=3508`; its used value is
44,396 - 3,508 = 40,888 B. The new upper bound is 16 B smaller than that
observed state, even before accounting for a positive top chunk or free chunks.
The 48 B difference between the old upper bound and measured usage is an
optimistic mathematical bound margin, **not a measured runtime reserve**.

The exact linked `system.a(malloc.c.o)` uses 4 KiB page/granularity settings.
Initial arena growth adds the distance to the next page boundary; subsequent
positive growth is in 4 KiB increments. Its `sbrk` rejects a resulting break
greater than or equal to `_HEAP_END`. The greatest reachable endpoint is
`0x01c7f000`. `get_malloc_remain_heap_size` subtracts used bytes from the raw
linker range, where used = footprint - 88 - top size - free chunks. Consequently
reported free space includes bytes which cannot be reached by another page
growth. It cannot by itself establish that a requested allocation can succeed.

This model is specific to the pinned SDK allocator object and linked call
graph. A changed allocator, a new `sbrk` caller, or runtime allocator tuning
requires another review. The build checks that scope rather than silently
reusing this calculation for another implementation.

## Bounded correction

Reduce fatal diagnostic storage from 256 to 192 B, preserving its symbol and
formatting through `sizeof`. Fatal messages can contain at most 191 characters
plus a terminating NUL. Keep the 256 B status field unchanged, copy only the
source's extent and zero its unused tail. The Doom zone, reverb, archive cache,
IIS DMA, task stacks and synth/NES FX behavior retain their existing sizes and
configuration.

The added gate requires at least the previous 40,936 B upper bound. The existing
raw startup budget and USB stack gates remain. This prevents the demonstrated
capacity regression; it is a necessary bound, not an allocation replay or a
runtime high-water qualification.

Build with `--out-dir build/target-candidate-allocator-20261008` to preserve the
failed candidate's outputs. Use the same private FMD1 archive and sound, music
and GENMIDI banks. The manifest records allocator identity, addresses,
page-grown capacity, reference usage and the gate result. Export under a new
immutable catalog ID only after the usual source, ELF, application-slot,
descriptor, stack and private-package checks pass.

The corrected offline build restores `_HEAP_BEGIN=0x01c74fc0`, a 41,024 B
maximum page-grown arena and the previous 40,936 B reported-used upper bound.
The app is 567,376 B (`ca1da356...`); the complete encoded image is
`46d857fc07f02345f95bcaaa988c2ac95492ffbd64c6d3d72e14cd34aa546a3f`.
All 13 native contracts and 27 focused Python checks pass. The USB diagnostic
chain remains 1,480 B plus a 1,024 B SDK margin within its 2,560 B stack.
Offline package validation passes with 139 sectors, directory last and
protected ranges retained. Exact artifacts are listed in
[TARGET_CANDIDATE.md](TARGET_CANDIDATE.md#corrected-allocator-artifact-2026-10-08-unflashed).

## Remote observation and acceptance

The laptop reported 139/139 written sectors verified and an exact complete
1 MiB readback of `d6884aae995f6e170c9ceb981783376eca154aec3746cf78f617d536fde2dcef`.
Serial observation failed when Windows could not configure COM14; the port
open sent neither HELLO nor DOOM STATUS. The user reported a blank/frozen screen
after one manual cold start. A present-only audio child was subsequently listed,
with no CDC or UBOOT device; that child alone does not prove a healthy running
application.

IIS allocation failure alone is nonfatal to rendering. Game-task creation
failure still attempts USB recovery. USB configuration/CDC allocation failures
can prevent recovery enumeration, while an engine fatal error stops LCD/audio.
These paths are compatible with memory pressure, but no unique failing
allocation is known. The added FX preparation is after the board-ready/LCD
startup gate and does not have a missing division helper.

No hardware operation accompanies this correction. The laptop separately
reported a user-authorized UBOOT restore to the working NES image, with all
139 sectors and full readback verified and successful serial boot. Its earlier
failed Doom session remains retained; the new protected session is idle with
the verified NES baseline. Physical NES screen confirmation remains pending.
This establishes that the same unit can boot the working stock-container
profile. The offline Doom package does not authorize another write. A later
explicitly authorized hardware test must use the supported session workflow
and establish serial, display, controls and audio acceptance separately.

Historical runtime and deployment evidence is in [DEPLOYMENT.md](DEPLOYMENT.md).
The old private producer log, candidate, full readback and telemetry are
retained outside the public source; original game data and device material
remain private.
