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
void fm1_doom_music_begin_block(void) {}
static unsigned audio_lock_calls, volume_lock_calls;
static unsigned cpu0_masked, cpu1_masked;
static int fail_open, fail_rate;
enum init_event {
    INIT_IIS_OPEN, INIT_IIS_HANDLER, INIT_IIS_RATE, INIT_ADC_MMIO,
    INIT_ALINK_IRQ, INIT_CHANNEL_ON, INIT_CHANNEL_OFF, INIT_IIS_CLOSE
};
static unsigned trace_init, init_event_count, first_adc_seen, init_events[8];
static int16_t music_left, music_right;
static int32_t dma[128];
static int32_t dma_before_render[128];
static unsigned inspect_dma_during_music, inspected_music_frames;
static void (*handler)(void *, u8 *, int, u8);
static void (*irq_handler)(void);

static void init_event(unsigned event)
{
    if (!trace_init) return;
    REQUIRE(init_event_count < sizeof(init_events) / sizeof(init_events[0]));
    init_events[init_event_count++] = event;
}

static void adc_mmio_event(void)
{
    if (trace_init && !first_adc_seen) {
        first_adc_seen = 1;
        init_event(INIT_ADC_MMIO);
    }
}

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
    adc_mmio_event();
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
    adc_mmio_event();
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
    if (inspect_dma_during_music) {
        /* The DAC may consume this SDK buffer while the renderer is late.
         * Every generation call must still see the previous published block. */
        REQUIRE(!memcmp(dma, dma_before_render, sizeof(dma)));
        ++inspected_music_frames;
    }
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
    init_event(INIT_IIS_OPEN);
    return fail_open;
}
void iis_close(u8 index)
{
    REQUIRE(!index && !registered && !lock_depth);
    init_event(INIT_IIS_CLOSE);
}
int iis_set_sample_rate(int rate, u8 index)
{
    REQUIRE(rate == 44100 && !index);
    init_event(INIT_IIS_RATE);
    return fail_rate;
}
void iis_set_dec_data_handler(void *context, void (*cb)(void *, u8 *, int, u8), u8 index)
{
    REQUIRE(!context && !index);
    init_event(INIT_IIS_HANDLER);
    handler = cb;
}
void iis_channel_on(u8 channel, u8 index)
{
    REQUIRE(channel == 8 && !index && registered);
    init_event(INIT_CHANNEL_ON);
}
void iis_channel_off(u8 channel, u8 index)
{
    REQUIRE(channel == 8 && !index && !registered);
    init_event(INIT_CHANNEL_OFF);
}
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
    init_event(INIT_ALINK_IRQ);
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
    inspect_dma_during_music = inspected_music_frames = 0;
    trace_init = 0;
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
    fail_open = fail_rate = 0;
    first_adc_seen = init_event_count = 0;
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

static void require_dma_unchanged_during_render(void)
{
    memcpy(dma_before_render, dma, sizeof(dma));
    inspected_music_frames = 0;
    inspect_dma_during_music = 1;
    dma_tick();
    inspect_dma_during_music = 0;
    REQUIRE(inspected_music_frames == 64);
}

static void test_low_volume_dma_publishes_only_completed_scaled_block(void)
{
    fm1_doom_sound_diagnostics before, after;
    reset_fixture(); result = 64; start_constant_music(); run_for_ms(1300);
    before = diagnostics();
    REQUIRE(before.ready && !before.error && before.volume_target == 8 && before.volume_gain == 8);
    require_audio_samples(48000, -80000);
    /* New loud input cannot replace any of the old low-volume samples until
     * all64 stereo frames have been generated and the master gain applied. */
    music_left = 8191; music_right = -8192;
    require_dma_unchanged_during_render();
    require_audio_samples(131056, -131072); /* PCM16*256*8/128. */
    after = diagnostics();
    REQUIRE(after.output_frames == before.output_frames + 64 && after.volume_gain == 8 && !after.error);
    music_left = -6144; music_right = 4095;
    require_dma_unchanged_during_render();
    require_audio_samples(-98304, 65520);
    fm1_doom_sound_shutdown();
}

static void test_muted_dma_stays_silent_during_nonzero_music_generation(void)
{
    fm1_doom_sound_diagnostics before, after;
    reset_fixture(); result = 1023; start_constant_music(); run_for_ms(1300);
    fm1_doom_sound_set_speaker_muted(1); run_for_ms(200);
    before = diagnostics();
    REQUIRE(before.ready && before.speaker_muted && before.volume_target == 127 && !before.volume_gain);
    require_audio_samples(0, 0);
    music_left = 8191; music_right = -8192;
    require_dma_unchanged_during_render();
    require_audio_samples(0, 0);
    after = diagnostics();
    REQUIRE(after.output_frames == before.output_frames + 64 && after.speaker_muted && !after.volume_gain && !after.error);
    fm1_doom_sound_shutdown();
}

static void test_default_init_orders_iis_rate_before_adc_and_alink(void)
{
    static const unsigned expected[] = {
        INIT_IIS_OPEN, INIT_IIS_HANDLER, INIT_IIS_RATE, INIT_ADC_MMIO,
        INIT_ALINK_IRQ, INIT_CHANNEL_ON
    };
    reset_fixture(); result = 512;
    trace_init = 1;
    REQUIRE(!fm1_doom_sound_init() && registered);
    trace_init = 0;
    REQUIRE(init_event_count == sizeof(expected) / sizeof(expected[0]));
    REQUIRE(!memcmp(init_events, expected, sizeof(expected)) && first_adc_seen);
    REQUIRE(reads && writes && (adc & 0x10) && !(adc & 0x20));
    fm1_doom_sound_set_speaker_muted(0);
    music_left = 3000; music_right = -5000;
    run_for_ms(1300);
    REQUIRE(diagnostics().volume_samples == 650 && diagnostics().volume_gain == 64);
    require_audio_samples(384000, -640000);
    fm1_doom_sound_shutdown();
}

static void test_iis_failures_never_launch_unstarted_adc(void)
{
    static const unsigned open_failure[] = {INIT_IIS_OPEN};
    static const unsigned rate_failure[] = {
        INIT_IIS_OPEN, INIT_IIS_HANDLER, INIT_IIS_RATE, INIT_CHANNEL_OFF, INIT_IIS_CLOSE
    };
    unsigned i;
    uint32_t samples_before;
    reset_fixture(); fail_open = -17; trace_init = 1;
    samples_before = diagnostics().volume_samples;
    REQUIRE(fm1_doom_sound_init() == -17 && !registered);
    trace_init = 0;
    REQUIRE(init_event_count == sizeof(open_failure) / sizeof(open_failure[0]));
    REQUIRE(!memcmp(init_events, open_failure, sizeof(open_failure)));
    REQUIRE(!reads && !writes && !first_adc_seen && !(adc & 0x30));
    for (i = 0; i < 20; ++i) volume_tick();
    REQUIRE(!reads && !writes && diagnostics().volume_samples == samples_before);

    reset_fixture(); fail_rate = -18; trace_init = 1;
    samples_before = diagnostics().volume_samples;
    REQUIRE(fm1_doom_sound_init() == -18 && !registered);
    trace_init = 0;
    REQUIRE(init_event_count == sizeof(rate_failure) / sizeof(rate_failure[0]));
    REQUIRE(!memcmp(init_events, rate_failure, sizeof(rate_failure)));
    REQUIRE(!reads && !writes && !first_adc_seen && !(adc & 0x30));
    for (i = 0; i < 20; ++i) volume_tick();
    REQUIRE(!reads && !writes && diagnostics().volume_samples == samples_before);
    fm1_doom_sound_shutdown();
}

static void test_early_adc_prime_survives_audio_initialization(void)
{
    fm1_doom_volume_hardware before, after;
    uint32_t reads_before, writes_before;
    unsigned i;
    reset_fixture();
    result = 512;
    REQUIRE(!fm1_doom_sound_volume_start() && !registered);
    REQUIRE(fm1_doom_sound_speaker_is_muted() == !!FM1_DOOM_BOOT_MUTED);
    for (i = 0; i < 12; ++i) volume_tick();
    fm1_doom_sound_get_volume_hardware(&before);
    REQUIRE(before.running && before.valid && before.raw == 512 && before.accepted == 512 &&
            before.target == 64 && before.samples == 12 && !before.errors && !before.gain);
    /* Repeated start and audio initialization cannot reset the accepted knob
     * or reconfigure ADC/GPIO gates while the timer owns live conversions. */
    reads_before = reads; writes_before = writes;
    REQUIRE(!fm1_doom_sound_volume_start());
    REQUIRE(reads == reads_before && writes == writes_before);
    fm1_doom_sound_set_speaker_muted(!FM1_DOOM_BOOT_MUTED);
    REQUIRE(!fm1_doom_sound_init() && registered);
    REQUIRE(reads == reads_before && writes == writes_before);
    REQUIRE(fm1_doom_sound_speaker_is_muted() == !FM1_DOOM_BOOT_MUTED);
    fm1_doom_sound_get_volume_hardware(&after);
    REQUIRE(!memcmp(&before, &after, sizeof(before)));
    fm1_doom_sound_set_speaker_muted(0);
    music_left = 3000; music_right = -5000;
    run_for_ms(1300);
    REQUIRE(diagnostics().volume_gain == 64 && diagnostics().volume_samples == 662);
    require_audio_samples(384000, -640000);
    fm1_doom_sound_shutdown();
}

static void test_early_adc_errors_are_not_retried_by_audio_initialization(void)
{
    fm1_doom_volume_hardware hardware;
    uint32_t reads_before, writes_before;
    unsigned i;
    reset_fixture();
    adc = 0x10; /* Another owner blocks the initial explicit ADC start. */
    REQUIRE(fm1_doom_sound_volume_start() == -1 && !writes);
    adc = 0; /* Releasing that owner must not trigger a hidden retry. */
    reads_before = reads; writes_before = writes;
    REQUIRE(fm1_doom_sound_volume_start() == -1);
    REQUIRE(!fm1_doom_sound_init() && registered);
    REQUIRE(reads == reads_before && writes == writes_before);
    fm1_doom_sound_get_volume_hardware(&hardware);
    REQUIRE(!hardware.running && !hardware.valid && hardware.errors == 1 && !hardware.samples);
    music_left = 3000; music_right = -5000;
    run_for_ms(1300);
    REQUIRE(diagnostics().ready && !diagnostics().volume_gain && diagnostics().volume_errors == 1);
    require_audio_samples(0, 0);
    fm1_doom_sound_shutdown();

    reset_fixture();
    REQUIRE(!fm1_doom_sound_volume_start());
    conversion_ready = 0;
    for (i = 0; i < 10; ++i) volume_tick();
    reads_before = reads; writes_before = writes;
    REQUIRE(fm1_doom_sound_volume_start() == -1);
    REQUIRE(!fm1_doom_sound_init() && registered);
    REQUIRE(reads == reads_before && writes == writes_before);
    fm1_doom_sound_get_volume_hardware(&hardware);
    REQUIRE(!hardware.running && !hardware.valid && hardware.errors == 1 && !hardware.samples);
    fm1_doom_sound_shutdown();
    cpu0_masked = cpu1_masked = 0;
    conversion_ready = 1; result = 1023;
    REQUIRE(!fm1_doom_sound_volume_start()); /* Explicit shutdown permits restart. */
    REQUIRE(!fm1_doom_sound_init() && registered);
    fm1_doom_sound_set_speaker_muted(0);
    music_left = 3000; music_right = -5000;
    run_for_ms(1300);
    REQUIRE(diagnostics().volume_gain == 127 && !diagnostics().volume_errors);
    require_audio_samples(762000, -1270000);
    fm1_doom_sound_shutdown();
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
    uint32_t reads_before, writes_before, samples_before;
    fm1_doom_sound_diagnostics status;
    reset_fixture();
    for (i = 0; i < 100; ++i) volume_tick();
    REQUIRE(!reads && !writes && !registered && !dma_calls);
    samples_before = diagnostics().volume_samples;
    fail_open = -17;
    REQUIRE(fm1_doom_sound_init() == -17 && !registered);
    REQUIRE(!(adc & 0x30));
    reads_before = reads;
    writes_before = writes;
    for (i = 0; i < 100; ++i) volume_tick();
    status = diagnostics();
    REQUIRE(reads == reads_before && writes == writes_before && !status.ready &&
            status.error == -17 && !status.volume_valid && !status.volume_gain &&
            status.volume_samples == samples_before && !status.irq_count && !status.output_frames);
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
    test_low_volume_dma_publishes_only_completed_scaled_block();
    test_muted_dma_stays_silent_during_nonzero_music_generation();
    test_default_init_orders_iis_rate_before_adc_and_alink();
    test_iis_failures_never_launch_unstarted_adc();
    test_early_adc_prime_survives_audio_initialization();
    test_early_adc_errors_are_not_retried_by_audio_initialization();
    test_volume_hardware_snapshot_is_read_only_and_lock_bounded();
    test_real_adc_knob_controls_nonzero_dma_audio();
    test_volume_sampling_uses_independent_timer_not_dma();
    test_conversion_timeout_mutes_without_stopping_audio();
    test_volume_sampling_is_safe_before_init_and_after_iis_failure();
    test_speaker_mute_retains_knob_without_adc_or_render_lock();
    test_boot_mute_is_applied_on_each_initialization();
    puts("Doom real ADC4 IIS-before-ADC startup/state preservation, CPU1 scaled DMA publication, speaker mute/unmute, boot default, timeout and IIS failure contract passed");
    return 0;
}
