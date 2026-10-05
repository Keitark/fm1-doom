# FM-1 Doom performance target

The requested target is **at least 30 completed, distinct gameplay frames per
second** on the physical 240×240 FM-1 LCD with the selected 8×8 E1M1 assets.
Measure after a 10-second warm-up during at least 60 seconds of active E1M1
play. Count LCD DMA completions, not engine ticks or attempted presentations.
The 60-second interval must contain at least 1,800 completed frames. Record
the distribution of completion intervals and any skipped/partial frames.

At 240×240 RGB565, one full frame transfers 115,200 pixel bytes; 30 frames/s
need at least 3,456,000 pixel bytes/s (27.648 Mbit/s), before LCD commands and
protocol overhead. A nominal 30 MHz SPI line would need 30.72 ms just for
those pixels, leaving only 2.61 ms per 33.33 ms frame if drawing and transfer
are serialized. The port now converts eight rows per callback, matching the
existing FM-1 DMA pixel buffer size of 3,840 bytes and halving callback count
compared with four-row strips. This is a transport preparation, not a speed
measurement or proof of overlap.

The separate NES firmware's documented bench telemetry reached 31.51 displayed
FPS over about three minutes. That establishes a board precedent for a similar
full-screen transport, not Doom rendering performance. See the
[FM-1 LCD handoff bench note](https://github.com/Keitark/fm1-tracker/blob/main/docs/FM1_LCD_HANDOFF_FIX_20260929.md).

Before claiming the target, add device telemetry for logic ticks, completed
LCD frames, dropped frames, CPU render time, RGB565 conversion time, archive
block decodes and decode time, transfer submit/completion time, and audio
underruns. Measure p50/p95 frame intervals and each stage's busy time on the
physical device. The host already reports archive decode counts and host
inflate time, but those timings cannot predict the AC7911B's throughput.

The direct-E1M1 engine now passes a short 32-bit host smoke with a 296 KiB
zone. Blocking prerequisites are a target allocator with measured stacks and
decoder scratch, a reviewed flash layout, and an FM-1 LCD/clock task binding.
No current build can be used to measure Doom FPS on hardware.
