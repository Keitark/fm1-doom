#include "fm1_doom_sound.h"
#include "fm1_doom_music.h"
#include "fake_sound_sdk.h"
#include "fm1_doom_sound_bank_ref.h"
#include "fm1_volume.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

int snd_channels = 8;
static unsigned irq_disabled, locked, opens, closes, registered, masked, stopped;
static int fail_open, fail_rate;
static struct iis_platform_data *retained_pd;
static void (*handler)(void *, u8 *, int, u8);
static void (*irq_handler)(void);
static int32_t dma[128];
static int16_t music_left, music_right;
static uint32_t half_msec, callback_half_msec;
static unsigned volume_starts, volume_stops, volume_ticks;
static int volume_fail_start, volume_fail_tick;
static uint16_t volume_raw = 1023;
static unsigned synth_mode = 1;

void fm1_doom_music_toggle_synth_mode(void) { REQUIRE(locked); synth_mode ^= 1u; }
unsigned fm1_doom_music_get_synth_mode(void) { REQUIRE(locked); return synth_mode; }

int fm1_volume_start(fm1_volume *state)
{
    REQUIRE(locked);
    memset(state,0,sizeof(*state));
    ++volume_starts;
    if(volume_fail_start){state->errors=1;return -1;}
    state->running=1;
    return 0;
}
void fm1_volume_stop(fm1_volume *state)
{
    REQUIRE(locked);
    if(state->running)++volume_stops;
    state->running=state->valid=state->target=0;
}
void fm1_volume_tick(fm1_volume *state)
{
    REQUIRE(locked);
    ++volume_ticks;
    if(!state->running)return;
    if(volume_fail_tick){++state->errors;state->running=state->valid=state->target=0;return;}
    ++state->samples;state->raw=volume_raw;state->valid=1;state->target=(uint8_t)(volume_raw>>3);
}

void fm1_doom_music_sample_stereo(int16_t *left, int16_t *right)
{
    *left = music_left; *right = music_right;
}
void fm1_doom_music_stop_locked(void) { REQUIRE(locked); music_left = music_right = 0; }
unsigned fm1_sound_test_irq_save(void)
{
    unsigned old = irq_disabled;
    irq_disabled = 1;
    return old;
}
void fm1_sound_test_irq_restore(unsigned flags) { REQUIRE(!locked); irq_disabled = flags; }
void fm1_sound_test_lock(spinlock_t *lock) { REQUIRE(irq_disabled && !locked); *lock = locked = 1; }
void fm1_sound_test_unlock(spinlock_t *lock) { REQUIRE(locked && *lock); *lock = locked = 0; }
int iis_open(struct iis_platform_data *pd, u8 index)
{
    REQUIRE(!index && pd->port_sel == IIS_PORTC);
    REQUIRE(pd->channel_out == 8 && pd->data_width == 8 && pd->mclk_output == 1);
    REQUIRE(!pd->update_edge && !pd->f32e && !pd->slave_mode && pd->sr_points == 128);
    retained_pd = pd;
    ++opens;
    return fail_open;
}
void iis_close(u8 index) { REQUIRE(!index && !registered && !locked); ++closes; }
int iis_set_sample_rate(int rate, u8 index) { REQUIRE(rate == 44100 && !index); return fail_rate; }
void iis_set_dec_data_handler(void *ctx, void (*cb)(void *,u8 *,int,u8),u8 index)
{
    REQUIRE(!ctx && !index); handler = cb;
}
void iis_channel_on(u8 channel,u8 index) { REQUIRE(channel == 8 && !index && registered); }
void iis_channel_off(u8 channel,u8 index) { REQUIRE(channel == 8 && !index && !registered); ++stopped; }
unsigned long jiffies_half_msec(void) { REQUIRE(locked); return half_msec; }
void iis_irq_handler(u8 index)
{
    REQUIRE(!index && locked && handler); handler(0,(u8 *)dma,512,3);
    half_msec += callback_half_msec;
}
void request_irq(unsigned irq,int priority,void (*cb)(void),unsigned cpu)
{
    REQUIRE(irq == 11 && priority == 3 && !cpu && !registered); irq_handler = cb; registered = 1;
}
void bit_clr_ie(unsigned irq,unsigned cpu) { REQUIRE(irq == 11 && !cpu && registered); ++masked; }
void unrequest_irq(unsigned irq,unsigned cpu)
{
    REQUIRE(irq == 11 && !cpu && registered && !locked && masked); registered = 0; irq_handler = 0;
}

static void test_decoder(void)
{
    uint8_t fixture[] = {3,128,17,43,9,0,0,0,0,0,0,0,0x17,0x80,0xf7,0x11};
    static const int16_t expected[] = {0,11,17,18,17,39,-7,12,30};
    fm1_doom_sound_voice voices[2] = {{0}};
    int32_t pcm[128];
    unsigned i;
    REQUIRE(!fm1_doom_sound_voice_start(&voices[0],fixture,sizeof(fixture)));
    fm1_doom_sound_mix(voices,pcm);
    for (i=0;i<sizeof(expected)/sizeof(expected[0]);++i)
        REQUIRE(pcm[i*8u] == expected[i]*127 && pcm[i*8u+1u] == expected[i]*127);
    REQUIRE(!voices[0].playing);
    for (i=72;i<128;++i) REQUIRE(pcm[i] == 0);
    for (i=1;i<=3;++i) {
        REQUIRE(fm1_doom_sound_voice_start(&voices[0],fixture,sizeof(fixture)-i));
        REQUIRE(!voices[0].playing);
    }
    fixture[10]=89;
    REQUIRE(fm1_doom_sound_voice_start(&voices[0],fixture,sizeof(fixture)));
    fixture[10]=0; fixture[11]=1;
    REQUIRE(fm1_doom_sound_voice_start(&voices[0],fixture,sizeof(fixture)));
    fixture[11]=0; fixture[4]=0;
    REQUIRE(fm1_doom_sound_voice_start(&voices[0],fixture,sizeof(fixture)));
    fixture[4]=9; fixture[2]=64; fixture[3]=31; /* 8000 Hz also supported. */
    REQUIRE(!fm1_doom_sound_voice_start(&voices[0],fixture,sizeof(fixture)));
    REQUIRE(voices[0].step == 11888);
    fixture[2]=0; fixture[3]=0;
    REQUIRE(fm1_doom_sound_voice_start(&voices[0],fixture,sizeof(fixture)));
    REQUIRE(fm1_doom_sound_voice_start(&voices[0],0,100));

    fixture[2]=17; fixture[3]=43; fixture[8]=255; fixture[9]=127;
    REQUIRE(!fm1_doom_sound_voice_start(&voices[0],fixture,sizeof(fixture)));
    REQUIRE(!fm1_doom_sound_voice_start(&voices[1],fixture,sizeof(fixture)));
    music_left=music_right=8191;
    fm1_doom_sound_mix(voices,pcm);
    REQUIRE(pcm[0] == 8388607 && pcm[1] == 8388607);
    fixture[8]=0; fixture[9]=128;
    REQUIRE(!fm1_doom_sound_voice_start(&voices[0],fixture,sizeof(fixture)));
    REQUIRE(!fm1_doom_sound_voice_start(&voices[1],fixture,sizeof(fixture)));
    music_left=music_right=-8192;
    fm1_doom_sound_mix(voices,pcm);
    REQUIRE(pcm[0] == -8388608 && pcm[1] == -8388608);
    memset(voices,0,sizeof(voices)); music_left=8191; music_right=-8192;
    fm1_doom_sound_mix(voices,pcm);
    REQUIRE(pcm[0] == 8191*256 && pcm[1] == -8192*256);
    music_left=music_right=0;
}

static void test_rice_decoder(void)
{
    uint8_t escaped[]={3,129,17,43,2,0,0,0,0,0,255,127};
    uint8_t blocks[44]={3,129,17,43,1,1,0,0};
    uint8_t invalid[20];
    fm1_doom_sound_voice voices[2]={{0}};
    int32_t pcm[128];
    unsigned i,block;
    REQUIRE(!fm1_doom_sound_voice_start(&voices[0],escaped,sizeof(escaped)));
    REQUIRE(voices[0].encoding && voices[0].samples_left==2);
    fm1_doom_sound_mix(voices,pcm);
    for(i=0;i<8;i++)REQUIRE(pcm[i]==-32768*127);
    for(i=8;i<128;i++)REQUIRE(pcm[i]==0);
    REQUIRE(!voices[0].playing && !voices[0].samples_left && voices[0].next==voices[0].end);
    for(i=0;i<sizeof(escaped);i++){
        REQUIRE(fm1_doom_sound_voice_start(&voices[0],escaped,i));
        REQUIRE(!voices[0].playing);
    }
    memcpy(invalid,escaped,sizeof(escaped));invalid[9]=8;
    REQUIRE(fm1_doom_sound_voice_start(&voices[0],invalid,sizeof(escaped)));
    memcpy(invalid,escaped,sizeof(escaped));invalid[11]=255;
    REQUIRE(fm1_doom_sound_voice_start(&voices[0],invalid,sizeof(escaped)));
    memcpy(invalid,escaped,sizeof(escaped));invalid[12]=0;
    REQUIRE(fm1_doom_sound_voice_start(&voices[0],invalid,sizeof(escaped)+1));
    memcpy(invalid,escaped,sizeof(escaped));invalid[4]=0;
    REQUIRE(fm1_doom_sound_voice_start(&voices[0],invalid,sizeof(escaped)));
    memcpy(invalid,escaped,sizeof(escaped));invalid[4]=4;
    REQUIRE(fm1_doom_sound_voice_start(&voices[0],invalid,sizeof(escaped)));
    memcpy(invalid,escaped,sizeof(escaped));invalid[9]=7;invalid[10]=63;invalid[11]=0;
    REQUIRE(fm1_doom_sound_voice_start(&voices[0],invalid,sizeof(escaped)));
    blocks[8]=144;blocks[42]=192; /*256 constant samples, then one distinct sample.*/
    REQUIRE(!fm1_doom_sound_voice_start(&voices[0],blocks,sizeof(blocks)));
    for(block=0;block<16;block++){
        fm1_doom_sound_mix(voices,pcm);
        for(i=0;i<128;i++)REQUIRE(pcm[i]==4096*127);
    }
    REQUIRE(voices[0].playing && voices[0].samples_left==1);
    fm1_doom_sound_mix(voices,pcm);
    for(i=0;i<8;i++)REQUIRE(pcm[i]==16384*127);
    for(i=8;i<128;i++)REQUIRE(!pcm[i]);
    REQUIRE(!voices[0].playing && !voices[0].samples_left && voices[0].next==voices[0].end);
    /* Both source rates obey exact count despite non-integral output phase. */
    blocks[2]=64;blocks[3]=31;
    REQUIRE(!fm1_doom_sound_voice_start(&voices[0],blocks,sizeof(blocks)));
    REQUIRE(voices[0].step==11888);
    for(block=0;voices[0].playing && block<30;block++)fm1_doom_sound_mix(voices,pcm);
    REQUIRE(!voices[0].playing && !voices[0].samples_left && voices[0].next==voices[0].end);
    blocks[43]=8;
    REQUIRE(fm1_doom_sound_voice_start(&voices[0],blocks,sizeof(blocks)));
}

static void test_effect_gain_and_music_independence(void)
{
    const uint8_t one_sample[]={3,129,17,43,1,0,0,0,129,0};
    fm1_doom_sound_voice voices[2]={{0}};
    int32_t pcm[128];unsigned i;
    REQUIRE(!fm1_doom_sound_voice_start(&voices[0],one_sample,sizeof(one_sample)));
    music_left=300;music_right=-400;
    fm1_doom_sound_mix(voices,pcm);
    for(i=0;i<64;i++) {
        int32_t effect=i<4?256*127:0;
        /* FX gain1 instead of2 is exactly half; music stays PCM16*256. */
        REQUIRE(pcm[2*i]==300*256+effect);
        REQUIRE(pcm[2*i+1]==-400*256+effect);
    }
    music_left=music_right=0;
}

static fm1_doom_sound_diagnostics diagnostics(void)
{
    fm1_doom_sound_diagnostics result;
    unsigned flags=fm1_doom_sound_lock();
    fm1_doom_sound_get_diagnostics(&result);
    fm1_doom_sound_get_diagnostics(0);
    fm1_doom_sound_unlock(flags);
    return result;
}

static void test_private_bank_reference(void)
{
    unsigned sound;
    REQUIRE(fm1_doom_sound_entry_count == sizeof(sound_reference)/sizeof(sound_reference[0]));
    for (sound=0;sound<fm1_doom_sound_entry_count;++sound) {
        const fm1_doom_sound_entry *entry=&fm1_doom_sound_entries[sound];
        fm1_doom_sound_voice voices[2]={{0}};
        int32_t pcm[128];
        uint32_t fnv=2166136261u, frames=0, previous=UINT32_MAX, count=0;
        REQUIRE(!fm1_doom_sound_voice_start(&voices[0],entry->data,entry->bytes));
        while (voices[0].playing) {
            unsigned i;
            REQUIRE(frames < 10000000u);
            fm1_doom_sound_mix(voices,pcm);
            for (i=0;i<64;++i,++frames) {
                uint32_t sample=(uint32_t)((uint64_t)frames*voices[0].step/65536u);
                if (sample<sound_reference[sound].samples && sample!=previous) {
                    int16_t value=(int16_t)(pcm[2*i]/127);
                    fnv=(fnv^(uint8_t)value)*16777619u;
                    fnv=(fnv^(uint8_t)((uint16_t)value>>8))*16777619u;
                    previous=sample; ++count;
                }
            }
        }
        REQUIRE(count == sound_reference[sound].samples);
        REQUIRE(fnv == sound_reference[sound].fnv);
        REQUIRE(voices[0].next == voices[0].end);
        if(entry->data[1]==129u) {
            REQUIRE(!voices[0].samples_left);
            REQUIRE(sound_reference[sound].samples==
                    ((uint32_t)entry->data[4]|(uint32_t)entry->data[5]<<8|
                     (uint32_t)entry->data[6]<<16|(uint32_t)entry->data[7]<<24));
        }
    }
}

static void test_module(void)
{
    sfxinfo_t pistol={0}, missing={0}, linked={0}, alias={0}, swtchn={0}, swtchx={0};
    unsigned i, closed, ticks_before, stops_before;
    fm1_doom_sound_diagnostics status;
    strcpy(pistol.name,"pistol"); strcpy(missing.name,"shotgn");
    strcpy(linked.name,"chgun"); linked.link=&pistol; strcpy(alias.name,"noway");
    strcpy(swtchn.name,"swtchn");strcpy(swtchx.name,"swtchx");
    fm1_doom_sound_shutdown(); /* Safe before first init and on repeated stop. */
    REQUIRE(!opens && !closes && !registered && !fm1_doom_sound_is_ready());
    status=diagnostics();
    REQUIRE(!status.ready && !status.error && !status.active_voices && !status.irq_count);
    fail_open=-17;
    REQUIRE(!fm1_sound_module.Init(true) && fm1_doom_sound_error == -17);
    REQUIRE(!registered && !fm1_sound_module.SoundIsPlaying(0));
    status=diagnostics(); REQUIRE(!status.ready && status.error == -17);
    fail_open=0; fail_rate=-18; closed=closes;
    REQUIRE(fm1_doom_sound_init() == -18 && closes == closed+1 && !registered);
    fail_rate=0;
    REQUIRE(fm1_sound_module.Init(true) && snd_channels == 2);
    REQUIRE(fm1_doom_sound_is_ready());
    status=diagnostics();REQUIRE(status.synth_mode==1);
    fm1_doom_sound_toggle_music_mode();status=diagnostics();REQUIRE(status.synth_mode==0);
    fm1_doom_sound_toggle_music_mode();status=diagnostics();REQUIRE(status.synth_mode==1);
    REQUIRE(retained_pd && retained_pd->sr_points == 128);
    REQUIRE(fm1_sound_module.GetSfxLumpNum(&pistol) > 0);
    REQUIRE(fm1_sound_module.GetSfxLumpNum(&linked) == fm1_sound_module.GetSfxLumpNum(&pistol));
    REQUIRE(fm1_sound_module.GetSfxLumpNum(&alias) > 0);
    for(i=0;i<fm1_doom_sound_entry_count;i++) {
        if(!strcmp(fm1_doom_sound_entries[i].name,"swtchx")) {
            REQUIRE(fm1_sound_module.GetSfxLumpNum(&swtchx)==(int)i+1);
            REQUIRE(fm1_sound_module.GetSfxLumpNum(&swtchn)>0);
            REQUIRE(fm1_sound_module.GetSfxLumpNum(&swtchn)!=fm1_sound_module.GetSfxLumpNum(&swtchx));
        }
    }
    REQUIRE(fm1_sound_module.GetSfxLumpNum(&missing) == -1);
    REQUIRE(fm1_sound_module.GetSfxLumpNum(0) == -1);
    REQUIRE(fm1_sound_module.StartSound(&missing,0,127,127) == -1);
    REQUIRE(fm1_sound_module.StartSound(&pistol,2,127,127) == -1);
    linked.link=&linked;
    REQUIRE(fm1_sound_module.GetSfxLumpNum(&linked) == -1);
    REQUIRE(fm1_sound_module.StartSound(&linked,0,127,127) == -1);
    ticks_before=volume_ticks;
    for (i=0;i<690;++i) {
        irq_handler();
        REQUIRE(!dma[0] && !dma[127]);
    }
    REQUIRE(fm1_doom_sound_frames == 44160 && fm1_doom_sound_irqs == 690);
    REQUIRE(volume_ticks-ticks_before==500u);
    status=diagnostics();
    REQUIRE(status.ready && !status.error && status.irq_count == 690 &&
            status.output_frames == 44160 && !status.sfx_started && !status.active_voices &&
            !status.max_irq_us);
    REQUIRE(status.volume_raw==1023 && status.volume_valid && !status.volume_errors);
    REQUIRE(fm1_sound_module.StartSound(&pistol,0,127,127) == 0);
    REQUIRE(fm1_sound_module.SoundIsPlaying(0));
    callback_half_msec=1; irq_handler();
    REQUIRE(fm1_doom_sound_started == 1);
    status=diagnostics();
    REQUIRE(status.sfx_started == 1 && status.active_voices == 1 && status.irq_count == 691 &&
            status.max_irq_us == 500);
    fm1_sound_module.UpdateSoundParams(0,0,127);
    half_msec=UINT32_MAX; callback_half_msec=2; irq_handler();
    status=diagnostics(); REQUIRE(status.max_irq_us == 1000);
    callback_half_msec=0; irq_handler();
    status=diagnostics(); REQUIRE(status.max_irq_us == 1000);
    for(i=0;i<128;++i) REQUIRE(!dma[i]);
    fm1_sound_module.StopSound(0);
    REQUIRE(!fm1_sound_module.SoundIsPlaying(0));
    closed=closes;
    fm1_doom_sound_shutdown();
    REQUIRE(!registered && closes == closed+1 && !fm1_doom_sound_is_ready());
    status=diagnostics(); REQUIRE(!status.ready && !status.active_voices && status.sfx_started == 1);
    fm1_doom_sound_shutdown(); REQUIRE(closes == closed+1);
    REQUIRE(fm1_sound_module.StartSound(&pistol,0,127,127) == -1);
    REQUIRE(fm1_doom_sound_init() == 0);
    status=diagnostics(); REQUIRE(!status.max_irq_us);
    handler(0,(u8 *)dma,256,3);
    REQUIRE(fm1_doom_sound_error == -42);
    status=diagnostics(); REQUIRE(!status.ready && status.error == -42);
    fm1_sound_module.Update();
    REQUIRE(!registered && !fm1_sound_module.SoundIsPlaying(0));
    /* A pre-existing ADC owner is reported and never stopped by this backend.
     * Unvalidated knob input keeps startup muted while IIS remains observable. */
    volume_fail_start=1;stops_before=volume_stops;
    REQUIRE(!fm1_doom_sound_init());
    REQUIRE(fm1_sound_module.StartSound(&pistol,0,127,127)==0);
    for(i=0;i<710;i++){
        irq_handler();
        REQUIRE(!dma[0] && !dma[127]);
    }
    status=diagnostics();
    REQUIRE(status.ready && !status.error && !status.volume_valid && !status.volume_gain && status.volume_errors==1);
    fm1_doom_sound_shutdown();
    REQUIRE(volume_stops==stops_before);
    volume_fail_start=0;
    REQUIRE(!fm1_doom_sound_init());
    for(i=0;i<850;i++)irq_handler();
    status=diagnostics();
    REQUIRE(status.volume_valid && status.volume_gain==127 && !status.volume_errors);
    volume_raw=0;
    for(i=0;i<140;i++)irq_handler();
    status=diagnostics();
    REQUIRE(status.volume_valid && !status.volume_raw && !status.volume_gain);
    volume_raw=1023;
    for(i=0;i<140;i++)irq_handler();
    status=diagnostics();REQUIRE(status.volume_gain==127);
    volume_fail_tick=1;
    for(i=0;i<140;i++)irq_handler();
    status=diagnostics();
    REQUIRE(status.ready && !status.error && !status.volume_valid && !status.volume_gain && status.volume_errors==1);
    fm1_doom_sound_shutdown();volume_fail_tick=0;
    REQUIRE(!locked && !irq_disabled && stopped >= 3);
}

int main(void)
{
    test_decoder();
    test_rice_decoder();
    test_effect_gain_and_music_independence();
    test_private_bank_reference();
    test_module();
    puts("Doom SFX exact PCM8/Rice and legacy IMA, mixer, IIS/knob and shutdown contract passed");
    return 0;
}
