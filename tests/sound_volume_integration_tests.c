#include "fm1_doom_sound.h"
#include "fm1_doom_music.h"
#include "fake_sound_sdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

/* One clock unit is 1 / 44,100,000 second. A DMA half is exactly 64,000
 * units; the independent 2 ms volume timer is exactly 88,200 units. */
#define DMA_PERIOD 64000u
#define VOLUME_PERIOD 88200u
#define ADC 0x13100u
#define PB 0x50040u
#define ANA 0x11900u

int snd_channels = 8;
static unsigned irq_disabled, lock_depth, registered, in_dma, conversion_ready;
static spinlock_t *locks[2];
static spinlock_t *audio_lock_seen, *volume_lock_seen;
static uint32_t adc, result, ana, pll, pb[9], reads, writes;
static uint64_t now, next_dma, next_volume;
static unsigned dma_calls, volume_calls, synth_mode = 1;
static unsigned audio_lock_calls, volume_lock_calls;
static unsigned cpu0_masked, cpu1_masked;
static int fail_open;
static int16_t music_left, music_right;
static int32_t dma[128];
static void (*handler)(void *, u8 *, int, u8);
static void (*irq_handler)(void);

unsigned fm1_sound_test_irq_save(void)
{
    unsigned old = irq_disabled;
    irq_disabled = 1;
    return old;
}
void fm1_sound_test_irq_restore(unsigned flags)
{
    REQUIRE(irq_disabled && (flags || !lock_depth));
    irq_disabled = flags;
}
void fm1_sound_test_lock(spinlock_t *lock)
{
    unsigned i;
    REQUIRE(irq_disabled && lock_depth < 2 && !*lock);
    for (i = 0; i < lock_depth; ++i) REQUIRE(locks[i] != lock);
    if (lock == audio_lock_seen) ++audio_lock_calls;
    if (lock == volume_lock_seen) ++volume_lock_calls;
    locks[lock_depth++] = lock;
    *lock = 1;
}
void fm1_sound_test_unlock(spinlock_t *lock)
{
    REQUIRE(irq_disabled && lock_depth && locks[lock_depth - 1] == lock && *lock);
    --lock_depth;
    *lock = 0;
}

uint32_t fm1_volume_test_read(uint32_t address)
{
    REQUIRE(irq_disabled && lock_depth && !in_dma);
    if (!volume_lock_seen) volume_lock_seen = locks[lock_depth - 1];
    REQUIRE(locks[lock_depth - 1] == volume_lock_seen && volume_lock_seen != audio_lock_seen);
    ++reads;
    if (address == ADC) return adc;
    if (address == ADC + 4) return result;
    if (address == ANA) return ana;
    if (address == ANA + 0xa4) return pll;
    REQUIRE(address >= PB && address <= PB + 32 && !(address & 3));
    return pb[(address - PB) / 4];
}
uint32_t fm1_sound_test_hardware_read(uint32_t address)
{
    return fm1_volume_test_read(address);
}
void fm1_volume_test_write(uint32_t address, uint32_t value)
{
    REQUIRE(irq_disabled && lock_depth && !in_dma);
    REQUIRE(locks[lock_depth - 1] == volume_lock_seen && volume_lock_seen != audio_lock_seen);
    ++writes;
    if (address == ADC) {
        REQUIRE(!(value & 0x20)); /* The polling driver never enables ADC IRQ. */
        adc = value & ~0xc0u; /* KICK/clear removes the hardware completion bit. */
        return;
    }
    if (address == ANA) { ana = value; return; }
    if (address == ANA + 0xa4) { pll = value; return; }
    REQUIRE(address >= PB + 8 && address <= PB + 32 && !(address & 3));
    pb[(address - PB) / 4] = value;
}

void fm1_doom_music_sample_stereo(int16_t *left, int16_t *right)
{
    REQUIRE(irq_disabled && lock_depth && in_dma);
    REQUIRE(locks[lock_depth - 1] == audio_lock_seen && audio_lock_seen != volume_lock_seen);
    *left = music_left;
    *right = music_right;
}
void fm1_doom_music_stop_locked(void)
{
    REQUIRE(irq_disabled && lock_depth);
    if (!audio_lock_seen) audio_lock_seen = locks[lock_depth - 1];
    REQUIRE(locks[lock_depth - 1] == audio_lock_seen && audio_lock_seen != volume_lock_seen);
    music_left = music_right = 0;
}
void fm1_doom_music_toggle_synth_mode(void)
{
    REQUIRE(irq_disabled && lock_depth);
    synth_mode ^= 1u;
}
unsigned fm1_doom_music_get_synth_mode(void)
{
    return synth_mode;
}

int iis_open(struct iis_platform_data *pd, u8 index)
{
    REQUIRE(!index && pd->port_sel == IIS_PORTC && pd->channel_out == 8 &&
            pd->data_width == 8 && pd->mclk_output == 1 && pd->sr_points == 128);
    return fail_open;
}
void iis_close(u8 index) { REQUIRE(!index && !registered && !lock_depth); }
int iis_set_sample_rate(int rate, u8 index)
{
    REQUIRE(rate == 44100 && !index);
    return 0;
}
void iis_set_dec_data_handler(void *context, void (*cb)(void *, u8 *, int, u8), u8 index)
{
    REQUIRE(!context && !index);
    handler = cb;
}
void iis_channel_on(u8 channel, u8 index) { REQUIRE(channel == 8 && !index && registered); }
void iis_channel_off(u8 channel, u8 index) { REQUIRE(channel == 8 && !index && !registered); }
void iis_irq_handler(u8 index)
{
    REQUIRE(!index && irq_disabled && lock_depth && in_dma && handler);
    handler(0, (u8 *)dma, sizeof(dma), 3);
    ++dma_calls;
}
unsigned long jiffies_half_msec(void)
{
    REQUIRE(irq_disabled && lock_depth && in_dma);
    return (unsigned long)(now / 22050u);
}
void request_irq(unsigned irq, int priority, void (*cb)(void), unsigned cpu)
{
    REQUIRE(irq == IRQ_ALNK_IDX && priority == 3 && cpu == 1 && !registered && cpu0_masked);
    irq_handler = cb;
    registered = 1;
}
void bit_clr_ie(unsigned irq, unsigned cpu)
{
    REQUIRE(irq == IRQ_ALNK_IDX);
    if (!cpu) {
        REQUIRE(!registered && !cpu0_masked);
        cpu0_masked = 1;
    } else {
        REQUIRE(cpu == 1 && registered && !cpu1_masked);
        cpu1_masked = 1;
    }
}
void unrequest_irq(unsigned irq, unsigned cpu)
{
    REQUIRE(irq == IRQ_ALNK_IDX && cpu == 1 && registered && !lock_depth && cpu1_masked);
    registered = 0;
    irq_handler = 0;
}

static fm1_doom_sound_diagnostics diagnostics(void)
{
    fm1_doom_sound_diagnostics status;
    fm1_doom_sound_get_diagnostics(&status);
    REQUIRE(!irq_disabled && !lock_depth);
    return status;
}

static void volume_tick(void)
{
    uint32_t reads_before = reads, writes_before = writes;
    if (conversion_ready && (adc & 0x10)) adc |= 0x80;
    fm1_doom_sound_volume_tick();
    ++volume_calls;
    /* The real driver reads completion once and never waits for conversion. */
    REQUIRE(reads - reads_before <= 4 && writes - writes_before <= 5);
    REQUIRE(!irq_disabled && !lock_depth);
}

static void dma_tick(void)
{
    uint32_t reads_before = reads, writes_before = writes;
    REQUIRE(irq_handler);
    in_dma = 1;
    irq_handler();
    in_dma = 0;
    REQUIRE(reads == reads_before && writes == writes_before);
    REQUIRE(!irq_disabled && !lock_depth);
}

static void run_for_ms(unsigned milliseconds)
{
    uint64_t end = now + (uint64_t)milliseconds * 44100u;
    while (next_volume <= end || next_dma <= end) {
        if (next_volume <= next_dma) {
            now = next_volume;
            volume_tick();
            next_volume += VOLUME_PERIOD;
        } else {
            now = next_dma;
            dma_tick();
            next_dma += DMA_PERIOD;
        }
    }
    now = end;
}

static void reset_fixture(void)
{
    unsigned i;
    fm1_doom_sound_shutdown();
    REQUIRE(!registered && !lock_depth && !irq_disabled);
    adc = result = reads = writes = 0;
    ana = pll = 0xffffffffu;
    for (i = 0; i < 9; ++i) pb[i] = 0xffffffffu;
    now = 0;
    next_dma = DMA_PERIOD;
    next_volume = VOLUME_PERIOD;
    dma_calls = volume_calls = 0;
    cpu0_masked = cpu1_masked = 0;
    conversion_ready = 1;
    fail_open = 0;
    handler = 0;
    memset(dma, 0, sizeof(dma));
}

static void start_constant_music(void)
{
    REQUIRE(!fm1_doom_sound_init() && registered);
    REQUIRE(fm1_doom_sound_speaker_is_muted() == !!FM1_DOOM_BOOT_MUTED);
    fm1_doom_sound_set_speaker_muted(0);
    music_left = 3000;
    music_right = -5000;
}

static void require_audio_samples(int32_t left, int32_t right)
{
    unsigned frame;
    for (frame = 0; frame < 64; ++frame) {
        REQUIRE(dma[frame * 2] == left);
        REQUIRE(dma[frame * 2 + 1] == right);
    }
}

static void test_volume_hardware_snapshot_is_read_only_and_lock_bounded(void)
{
    fm1_doom_volume_hardware hardware;
    fm1_doom_sound_diagnostics before, after;
    uint32_t reads_before, writes_before;
    int32_t audio_before[128];
    reset_fixture();
    result = 512;
    start_constant_music();
    run_for_ms(1300);
    before = diagnostics();
    memcpy(audio_before, dma, sizeof(dma));
    reads_before = reads;
    writes_before = writes;
    fm1_doom_sound_get_volume_hardware(0);
    REQUIRE(reads == reads_before && writes == writes_before && !lock_depth && !irq_disabled);
    fm1_doom_sound_get_volume_hardware(&hardware);
    REQUIRE(reads == reads_before + 11 && writes == writes_before && !lock_depth && !irq_disabled);
    REQUIRE(hardware.adc_con == adc && hardware.adc_res == result);
    REQUIRE(hardware.pb_dir == pb[2] && hardware.pb_die == pb[3] &&
            hardware.pb_pu == pb[4] && hardware.pb_pd == pb[5] && hardware.pb_hd0 == pb[6] &&
            hardware.pb_hd1 == pb[7] && hardware.pb_dieh == pb[8]);
    REQUIRE(hardware.wla_con0 == ana && hardware.pll_con1 == pll);
    REQUIRE(hardware.raw == 512 && hardware.accepted == 512 && hardware.target == 64 &&
            hardware.gain == 64 && hardware.running && hardware.valid &&
            !hardware.waiting && hardware.samples == before.volume_samples && !hardware.errors);
    after = diagnostics();
    REQUIRE(after.volume_samples == before.volume_samples && after.volume_target == before.volume_target &&
            after.volume_gain == before.volume_gain && after.output_frames == before.output_frames);
    REQUIRE(!memcmp(audio_before, dma, sizeof(dma)) && reads == reads_before + 11 && writes == writes_before);
    /* Report actual unexpected register values, preserving them for diagnosis. */
    adc = 0x12345678u; result = 0xabcdef01u; pb[3] |= 0x40u; ana ^= 0x4000u;
    fm1_doom_sound_get_volume_hardware(&hardware);
    REQUIRE(hardware.adc_con == 0x12345678u && hardware.adc_res == 0xabcdef01u &&
            hardware.pb_die == pb[3] && hardware.wla_con0 == ana && writes == writes_before);
    fm1_doom_sound_shutdown();
}

static void test_real_adc_knob_controls_nonzero_dma_audio(void)
{
    fm1_doom_sound_diagnostics status;
    reset_fixture();
    result = 1023;
    start_constant_music();
    {
        unsigned flags = fm1_doom_sound_lock();
        unsigned depth = lock_depth;
        /* USB must read scalar state even while the renderer owns its lock. */
        fm1_doom_sound_get_diagnostics(&status);
        REQUIRE(status.ready && irq_disabled && lock_depth == depth);
        fm1_doom_sound_unlock(flags);
    }
    run_for_ms(1300); /* Clear the real startup mute and complete the gain ramp. */
    status = diagnostics();
    REQUIRE(status.ready && !status.error && status.volume_raw == 1023 &&
            status.volume_valid && !status.volume_errors && status.volume_target == 127 &&
            status.volume_gain == 127 && status.volume_samples == 650);
    require_audio_samples(762000, -1270000);

    result = 512;
    run_for_ms(20); /* Ten real ADC completions accept the half-volume target. */
    status = diagnostics();
    REQUIRE(status.volume_raw == 512 && status.volume_valid && status.volume_target == 64 &&
            status.volume_samples == 660);
    run_for_ms(100);
    status = diagnostics();
    REQUIRE(status.volume_gain == 64 && !status.volume_errors);
    require_audio_samples(384000, -640000);

    result = 0;
    run_for_ms(120);
    status = diagnostics();
    REQUIRE(!status.volume_raw && status.volume_valid && !status.volume_target &&
            !status.volume_gain && !status.volume_errors);
    require_audio_samples(0, 0);

    result = 1023;
    run_for_ms(220);
    status = diagnostics();
    REQUIRE(status.volume_raw == 1023 && status.volume_valid && status.volume_target == 127 &&
            status.volume_gain == 127 && !status.volume_errors);
    REQUIRE(status.irq_count == dma_calls && status.output_frames == dma_calls * 64u);
    require_audio_samples(762000, -1270000);
    fm1_doom_sound_shutdown();
}

static void test_volume_sampling_uses_independent_timer_not_dma(void)
{
    unsigned i;
    uint32_t reads_before, writes_before;
    fm1_doom_sound_diagnostics status;
    reset_fixture();
    result = 1023;
    start_constant_music();
    reads_before = reads;
    writes_before = writes;
    for (i = 0; i < 1000; ++i) {
        now += DMA_PERIOD;
        dma_tick();
    }
    status = diagnostics();
    REQUIRE(reads == reads_before && writes == writes_before && !volume_calls);
    REQUIRE(status.ready && !status.error && !status.volume_samples &&
            !status.volume_valid && !status.volume_target && !status.volume_gain);
    require_audio_samples(0, 0);
    for (i = 0; i < 9; ++i) {
        now += VOLUME_PERIOD;
        volume_tick();
    }
    status = diagnostics();
    REQUIRE(status.volume_samples == 9 && status.volume_raw == 1023 &&
            status.volume_valid && !status.volume_target);
    now += VOLUME_PERIOD;
    volume_tick();
    status = diagnostics();
    REQUIRE(status.volume_samples == 10 && status.volume_target == 127);
    for (i = 0; i < 127; ++i) dma_tick();
    status = diagnostics();
    REQUIRE(status.volume_gain == 127);
    require_audio_samples(762000, -1270000);
    fm1_doom_sound_shutdown();
}

static void test_conversion_timeout_mutes_without_stopping_audio(void)
{
    uint32_t frames_before, samples_before;
    fm1_doom_sound_diagnostics status;
    reset_fixture();
    result = 1023;
    start_constant_music();
    run_for_ms(1300);
    status = diagnostics();
    REQUIRE(status.volume_gain == 127 && status.volume_valid);
    require_audio_samples(762000, -1270000);
    frames_before = status.output_frames;
    samples_before = status.volume_samples;
    conversion_ready = 0;
    run_for_ms(18);
    status = diagnostics();
    REQUIRE(status.volume_valid && !status.volume_errors && status.volume_target == 127);
    run_for_ms(2);
    status = diagnostics();
    REQUIRE(!status.volume_valid && !status.volume_target && status.volume_errors == 1 &&
            status.volume_samples == samples_before && !(adc & 0x30));
    run_for_ms(200);
    status = diagnostics();
    REQUIRE(status.ready && !status.error && !status.volume_gain && !status.volume_valid &&
            status.volume_errors == 1 && status.output_frames > frames_before);
    require_audio_samples(0, 0);
    fm1_doom_sound_shutdown();
}

static void test_volume_sampling_is_safe_before_init_and_after_iis_failure(void)
{
    unsigned i;
    uint32_t reads_before, writes_before;
    fm1_doom_sound_diagnostics status;
    reset_fixture();
    for (i = 0; i < 100; ++i) volume_tick();
    REQUIRE(!reads && !writes && !registered && !dma_calls);
    fail_open = -17;
    REQUIRE(fm1_doom_sound_init() == -17 && !registered);
    REQUIRE(!(adc & 0x30));
    reads_before = reads;
    writes_before = writes;
    for (i = 0; i < 100; ++i) volume_tick();
    status = diagnostics();
    REQUIRE(reads == reads_before && writes == writes_before && !status.ready &&
            status.error == -17 && !status.volume_valid && !status.volume_gain &&
            !status.volume_samples && !status.irq_count && !status.output_frames);
    fm1_doom_sound_shutdown();
}

static void test_speaker_mute_retains_knob_without_adc_or_render_lock(void)
{
    fm1_doom_sound_diagnostics status;
    fm1_doom_volume_hardware hardware;
    uint32_t old_reads, old_writes, old_audio_locks, old_volume_locks;
    reset_fixture(); result = 512; start_constant_music(); run_for_ms(1300);
    status = diagnostics();
    REQUIRE(!status.speaker_muted && status.volume_target == 64 && status.volume_gain == 64);
    require_audio_samples(384000, -640000);
    old_reads = reads; old_writes = writes;
    old_audio_locks = audio_lock_calls; old_volume_locks = volume_lock_calls;
    fm1_doom_sound_set_speaker_muted(1);
    REQUIRE(reads == old_reads && writes == old_writes && audio_lock_calls == old_audio_locks);
    REQUIRE(volume_lock_calls == old_volume_locks + 1 && !irq_disabled && !lock_depth);
    status = diagnostics();
    REQUIRE(status.speaker_muted && status.volume_target == 64 && status.volume_gain == 64);
    REQUIRE(fm1_doom_sound_speaker_is_muted() && reads == old_reads && writes == old_writes);
    {
        unsigned flags = fm1_doom_sound_lock(), depth = lock_depth;
        unsigned old_locks = audio_lock_calls + volume_lock_calls;
        /* Flag and AUDIO snapshots must remain usable while rendering owns
         * its lock; neither getter changes controller or envelope state. */
        REQUIRE(fm1_doom_sound_speaker_is_muted());
        fm1_doom_sound_get_diagnostics(&status);
        REQUIRE(status.speaker_muted && status.volume_target == 64 && lock_depth == depth);
        REQUIRE(audio_lock_calls + volume_lock_calls == old_locks);
        fm1_doom_sound_unlock(flags);
    }
    fm1_doom_sound_get_volume_hardware(&hardware);
    REQUIRE(hardware.target == 64 && hardware.gain == 64 && hardware.raw == 512);
    run_for_ms(100);
    status = diagnostics();
    REQUIRE(status.ready && status.speaker_muted && status.volume_valid && !status.volume_errors);
    REQUIRE(status.volume_target == 64 && !status.volume_gain);
    require_audio_samples(0, 0);
    /* The timer continues sampling a changed physical target while muted. */
    result = 1023; run_for_ms(100); status = diagnostics();
    REQUIRE(status.speaker_muted && status.volume_raw == 1023 && status.volume_target == 127);
    REQUIRE(!status.volume_gain && !status.volume_errors); require_audio_samples(0, 0);
    old_reads = reads; old_writes = writes;
    old_audio_locks = audio_lock_calls; old_volume_locks = volume_lock_calls;
    fm1_doom_sound_set_speaker_muted(0);
    REQUIRE(reads == old_reads && writes == old_writes && audio_lock_calls == old_audio_locks);
    REQUIRE(volume_lock_calls == old_volume_locks + 1 && !irq_disabled && !lock_depth);
    status = diagnostics();
    REQUIRE(!status.speaker_muted && status.volume_target == 127 && !status.volume_gain);
    run_for_ms(200); status = diagnostics();
    REQUIRE(!status.speaker_muted && status.volume_gain == 127 && status.volume_target == 127);
    require_audio_samples(762000, -1270000);
    fm1_doom_sound_shutdown();
}

static void test_boot_mute_is_applied_on_each_initialization(void)
{
    fm1_doom_sound_diagnostics status;
    reset_fixture(); result = 1023;
    REQUIRE(!fm1_doom_sound_init() && registered);
    music_left = 3000; music_right = -5000;
    status = diagnostics();
    REQUIRE(status.speaker_muted == !!FM1_DOOM_BOOT_MUTED && !status.volume_gain);
    run_for_ms(1300); status = diagnostics();
    REQUIRE(status.volume_target == 127 && status.volume_samples == 650);
    REQUIRE(status.speaker_muted == !!FM1_DOOM_BOOT_MUTED);
    if (FM1_DOOM_BOOT_MUTED) {
        REQUIRE(!status.volume_gain); require_audio_samples(0, 0);
        fm1_doom_sound_set_speaker_muted(0); run_for_ms(200);
        REQUIRE(diagnostics().volume_gain == 127); require_audio_samples(762000, -1270000);
    } else {
        REQUIRE(status.volume_gain == 127); require_audio_samples(762000, -1270000);
    }
    fm1_doom_sound_set_speaker_muted(!FM1_DOOM_BOOT_MUTED);
    fm1_doom_sound_shutdown();
    REQUIRE(fm1_doom_sound_speaker_is_muted() == !FM1_DOOM_BOOT_MUTED);
    cpu0_masked = cpu1_masked = 0;
    REQUIRE(!fm1_doom_sound_init() && registered);
    REQUIRE(fm1_doom_sound_speaker_is_muted() == !!FM1_DOOM_BOOT_MUTED);
    fm1_doom_sound_shutdown();
}

int main(void)
{
    test_volume_hardware_snapshot_is_read_only_and_lock_bounded();
    test_real_adc_knob_controls_nonzero_dma_audio();
    test_volume_sampling_uses_independent_timer_not_dma();
    test_conversion_timeout_mutes_without_stopping_audio();
    test_volume_sampling_is_safe_before_init_and_after_iis_failure();
    test_speaker_mute_retains_knob_without_adc_or_render_lock();
    test_boot_mute_is_applied_on_each_initialization();
    puts("Doom real ADC4 volume integration, CPU1 DMA, speaker mute/unmute, boot default, timeout and IIS failure contract passed");
    return 0;
}
