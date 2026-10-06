#include "fm1_doom_music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
extern const uint8_t fm1_doom_music_score[];
extern const uint32_t fm1_doom_music_score_len;

#ifdef FM1_TARGET_PI32V2
static unsigned backend_locked, backend_ready;
unsigned fm1_doom_sound_lock(void){CHECK(!backend_locked);backend_locked=1;return 7;}
void fm1_doom_sound_unlock(unsigned flags){CHECK(flags==7 && backend_locked);backend_locked=0;}
int fm1_doom_sound_is_ready(void){CHECK(!backend_locked);return backend_ready;}
static void module_tests(void)
{
    void *handle;
    CHECK(!fm1_music_module.Init());backend_ready=1;CHECK(fm1_music_module.Init());
    CHECK(!fm1_music_module.RegisterSong(0,1));
    handle=fm1_music_module.RegisterSong(0,0);CHECK(handle);
    fm1_music_module.SetMusicVolume(127);fm1_music_module.PlaySong(handle,true);
    CHECK(fm1_music_module.MusicIsPlaying());
    fm1_music_module.PauseMusic();CHECK(!fm1_doom_music_sample());
    fm1_music_module.ResumeMusic();fm1_music_module.StopSong();
    CHECK(!fm1_music_module.MusicIsPlaying());fm1_music_module.Shutdown();
    CHECK(!backend_locked);
}
#endif

static unsigned literal_score(uint8_t *output,const uint8_t *events,unsigned length)
{
    unsigned i;
    memcpy(output,"FMSC",4);output[4]=(uint8_t)length;output[5]=output[6]=output[7]=0;
    output[8]=255;output[9]=0;output[10]=140;output[11]=0;
    output[12]=(uint8_t)length;output[13]=output[14]=output[15]=0;
    for(i=0;i<255;i++){output[16+2*i]=(uint8_t)i;output[17+2*i]=255;}
    memcpy(output+526,events,length);return 526+length;
}

static void synthetic_tests(void)
{
    /* Original guitar A4 for140ticks. DOS OPL2 ignores MIDI pan and is mono. */
    static const uint8_t events[]={0,0x40,0,29,0,0x40,4,0,0,0x10,69,127,0x8c,1,0,69,1,0x60};
    uint8_t bank[1024];unsigned size=literal_score(bank,events,sizeof(events)),i,crossings=0;
    int16_t l,r,prior=0;int64_t power=0;
    fm1_doom_music_diagnostics d;
    CHECK(!fm1_doom_music_set_score(bank,size));CHECK(!fm1_doom_music_start(0));
    fm1_doom_music_set_volume(127);
    for(i=0;i<44100;i++){
        fm1_doom_music_sample_stereo(&l,&r);CHECK(r==l);power+=(int32_t)l*l;
        if(l>=0 && prior<0 && i>500)++crossings;prior=l;
    }
    /* The real two-operator patch includes strong harmonics; zero crossings
     * do not measure its fundamental as they did for the old triangle. */
    CHECK(power>1000000);CHECK(crossings>100);
    fm1_doom_music_pause(1);fm1_doom_music_get_diagnostics(&d);i=d.ticks;
    CHECK(!fm1_doom_music_sample());fm1_doom_music_get_diagnostics(&d);CHECK(d.ticks==i);
    fm1_doom_music_pause(0);fm1_doom_music_set_volume(0);CHECK(!fm1_doom_music_sample());
    fm1_doom_music_stop_locked();CHECK(!fm1_doom_music_is_playing());CHECK(!fm1_doom_music_sample());
    bank[17]=0;CHECK(fm1_doom_music_set_score(bank,size)<0); /* cycle */
    CHECK(fm1_doom_music_start(0)<0);CHECK(fm1_doom_music_set_score(0,size)<0);
    literal_score(bank,events,sizeof(events));bank[527]=0x50; /* unsupported event kind */
    CHECK(!fm1_doom_music_set_score(bank,size));CHECK(!fm1_doom_music_start(0));
    for(i=0;i<44100;i++)(void)fm1_doom_music_sample();
    fm1_doom_music_get_diagnostics(&d);CHECK(d.errors && !fm1_doom_music_is_playing());
}

static void synth_tests(void)
{
    /* Eight full-velocity melodic VCOs: hold3s, release, allow3s to settle.
     * This drives the resonant VCF harder than E1M1's five held melodic keys. */
    static const uint8_t events[]={0,0x40,0,29,0,0x40,3,127,
        0,0x10,40,127,0,0x10,47,127,0,0x10,52,127,0,0x10,59,127,
        0,0x10,64,127,0,0x10,71,127,0,0x10,76,127,0,0x10,83,127,
        0xa4,3,0,40,0,0,47,0,0,52,0,0,59,0,0,64,0,0,71,0,0,76,0,0,83,
        0xa4,3,0x60};
    uint8_t bank[1024];unsigned size=literal_score(bank,events,sizeof(events)),i,nonzero=0;
    int sample;fm1_doom_music_diagnostics d;
    fm1_doom_music_set_synth_mode(1);CHECK(fm1_doom_music_get_synth_mode()==1);
    CHECK(!fm1_doom_music_set_score(bank,size));CHECK(!fm1_doom_music_start(0));fm1_doom_music_set_volume(127);
    for(i=0;i<44100u*6u;i++){
        sample=fm1_doom_music_sample();CHECK(sample>=-8192 && sample<=8191);nonzero+=sample!=0;
        if(i>=44100u*5u)CHECK(sample==0); /* VCA release/VCF ring-down converge. */
    }
    fm1_doom_music_get_diagnostics(&d);CHECK(nonzero>44100 && !d.errors);
    CHECK(!fm1_doom_music_set_score(fm1_doom_music_score,fm1_doom_music_score_len));CHECK(!fm1_doom_music_start(1));
    for(i=0;i<44100;i++)(void)fm1_doom_music_sample();
    fm1_doom_music_get_diagnostics(&d);CHECK(!d.errors);
    fm1_doom_music_toggle_synth_mode();CHECK(!fm1_doom_music_get_synth_mode());
    for(i=0;i<128;i++)(void)fm1_doom_music_sample();
    fm1_doom_music_toggle_synth_mode();CHECK(fm1_doom_music_get_synth_mode()==1);
    fm1_doom_music_set_volume(0);for(i=0;i<1000;i++)CHECK(!fm1_doom_music_sample());
    fm1_doom_music_get_diagnostics(&d);CHECK(!d.errors && d.ticks>140);
    fm1_doom_music_stop_locked();fm1_doom_music_set_synth_mode(0);
}

enum {MONITOR_FRAMES=315u*70u};
/* Bass drum36, snare38 and open hi-hat46 are in the sparse E1M1 bank. */
static const uint8_t drum_events[]={0,0x1f,36,100,10,0x0f,36,10,
    0x1f,38,100,10,0x0f,38,10,0x1f,46,100,10,0x0f,46,10,0x60};
static const uint8_t melody_events[]={0,0x40,0,29,0,0x10,69,100,
    10,0x40,3,80,10,0x20,144,10,0x00,69,30,0x60};

static void monitor_start(const uint8_t *events,unsigned length,unsigned mode,unsigned analog)
{
    static uint8_t bank[1024];unsigned size=literal_score(bank,events,length);
    fm1_doom_music_set_synth_mode(analog);fm1_doom_music_set_monitor(mode);
    CHECK(!fm1_doom_music_set_score(bank,size));CHECK(!fm1_doom_music_start(0));
    fm1_doom_music_set_volume(64);
}

static void monitor_percussion_only_tests(void)
{
    int16_t full[MONITOR_FRAMES],l,r;unsigned analog,i,nonzero;
    fm1_doom_music_diagnostics d;
    for(analog=0;analog<2;analog++){
        monitor_start(drum_events,sizeof(drum_events),FM1_DOOM_MUSIC_MONITOR_FULL,analog);
        nonzero=0;
        for(i=0;i<MONITOR_FRAMES;i++){full[i]=(int16_t)fm1_doom_music_sample();nonzero+=full[i]!=0;}
        fm1_doom_music_get_diagnostics(&d);CHECK(nonzero>100 && d.events==6 && !d.errors);
        monitor_start(drum_events,sizeof(drum_events),FM1_DOOM_MUSIC_MONITOR_DRUMS,analog);
        for(i=0;i<MONITOR_FRAMES;i++){
            fm1_doom_music_sample_stereo(&l,&r);CHECK(l==r && l==full[i]);
        }
        fm1_doom_music_get_diagnostics(&d);CHECK(d.events==6 && !d.errors);
        monitor_start(drum_events,sizeof(drum_events),FM1_DOOM_MUSIC_MONITOR_MELODY,analog);
        for(i=0;i<MONITOR_FRAMES;i++)CHECK(!fm1_doom_music_sample());
        fm1_doom_music_get_diagnostics(&d);CHECK(d.events==6 && !d.errors);
    }
    fm1_doom_music_set_monitor(FM1_DOOM_MUSIC_MONITOR_FULL);fm1_doom_music_set_synth_mode(0);
}

static void monitor_melody_only_tests(void)
{
    int16_t full[MONITOR_FRAMES];unsigned analog,i,nonzero;
    fm1_doom_music_diagnostics d;
    for(analog=0;analog<2;analog++){
        monitor_start(melody_events,sizeof(melody_events),FM1_DOOM_MUSIC_MONITOR_FULL,analog);
        nonzero=0;
        for(i=0;i<MONITOR_FRAMES;i++){full[i]=(int16_t)fm1_doom_music_sample();nonzero+=full[i]!=0;}
        fm1_doom_music_get_diagnostics(&d);CHECK(nonzero>100 && d.events==5 && !d.errors);
        monitor_start(melody_events,sizeof(melody_events),FM1_DOOM_MUSIC_MONITOR_MELODY,analog);
        for(i=0;i<MONITOR_FRAMES;i++)CHECK(fm1_doom_music_sample()==full[i]);
        fm1_doom_music_get_diagnostics(&d);CHECK(d.events==5 && !d.errors);
        monitor_start(melody_events,sizeof(melody_events),FM1_DOOM_MUSIC_MONITOR_DRUMS,analog);
        for(i=0;i<MONITOR_FRAMES;i++)CHECK(!fm1_doom_music_sample());
        fm1_doom_music_get_diagnostics(&d);CHECK(d.events==5 && !d.errors);
    }
    fm1_doom_music_set_monitor(FM1_DOOM_MUSIC_MONITOR_FULL);fm1_doom_music_set_synth_mode(0);
}

static void monitor_crossfade_percussion_tests(void)
{
    int16_t full[MONITOR_FRAMES];unsigned i,mode;
    /* The diagnostic drum gain follows both directions of the live synth
     * crossfade, rather than treating an in-flight blend as settled OPL. */
    for(mode=FM1_DOOM_MUSIC_MONITOR_FULL;mode<=FM1_DOOM_MUSIC_MONITOR_DRUMS;mode+=2){
        monitor_start(drum_events,sizeof(drum_events),mode,0);
        for(i=0;i<MONITOR_FRAMES;i++){
            int sample;
            if(i==630u)fm1_doom_music_set_synth_mode(1);
            if(i==6300u)fm1_doom_music_set_synth_mode(0);
            sample=fm1_doom_music_sample();
            if(mode==FM1_DOOM_MUSIC_MONITOR_FULL)full[i]=(int16_t)sample;
            else {
                if(sample!=full[i])fprintf(stderr,"Monitor percussion crossfade frame %u: full=%d drums=%d\n",i,(int)full[i],sample);
                CHECK(sample==full[i]);
            }
        }
    }
    fm1_doom_music_set_monitor(FM1_DOOM_MUSIC_MONITOR_FULL);fm1_doom_music_set_synth_mode(0);
}

static void monitor_pause_preserves_stream_tests(void)
{
    int16_t full[MONITOR_FRAMES];unsigned analog,i;
    fm1_doom_music_diagnostics before,after,expected;
    for(analog=0;analog<2;analog++){
        monitor_start(melody_events,sizeof(melody_events),FM1_DOOM_MUSIC_MONITOR_FULL,analog);
        for(i=0;i<MONITOR_FRAMES;i++)full[i]=(int16_t)fm1_doom_music_sample();
        fm1_doom_music_get_diagnostics(&expected);
        monitor_start(melody_events,sizeof(melody_events),FM1_DOOM_MUSIC_MONITOR_FULL,analog);
        for(i=0;i<137;i++)CHECK(fm1_doom_music_sample()==full[i]);
        fm1_doom_music_get_diagnostics(&before);
        fm1_doom_music_set_monitor(FM1_DOOM_MUSIC_MONITOR_PAUSE);
        CHECK(fm1_doom_music_get_monitor()==FM1_DOOM_MUSIC_MONITOR_PAUSE);
        for(i=0;i<4410;i++)CHECK(!fm1_doom_music_sample());
        fm1_doom_music_get_diagnostics(&after);
        CHECK(after.ticks==before.ticks && after.events==before.events && after.loops==before.loops);
        CHECK(after.voice_steals==before.voice_steals && after.errors==before.errors && after.active_voices==before.active_voices);
        CHECK(fm1_doom_music_is_playing() && fm1_doom_music_get_synth_mode()==analog);
        fm1_doom_music_set_monitor(FM1_DOOM_MUSIC_MONITOR_FULL);
        for(i=137;i<MONITOR_FRAMES;i++)CHECK(fm1_doom_music_sample()==full[i]);
        fm1_doom_music_get_diagnostics(&after);
        CHECK(after.events==expected.events && after.ticks==expected.ticks && !after.errors);
    }
    fm1_doom_music_set_synth_mode(0);
}

static void monitor_preserves_engine_pause_tests(void)
{
    fm1_doom_music_diagnostics before,after;unsigned i;
    monitor_start(melody_events,sizeof(melody_events),FM1_DOOM_MUSIC_MONITOR_FULL,0);
    (void)fm1_doom_music_sample();fm1_doom_music_pause(1);fm1_doom_music_get_diagnostics(&before);
    fm1_doom_music_set_monitor(FM1_DOOM_MUSIC_MONITOR_PAUSE);
    for(i=0;i<315;i++)CHECK(!fm1_doom_music_sample());
    fm1_doom_music_set_monitor(FM1_DOOM_MUSIC_MONITOR_FULL);
    for(i=0;i<315;i++)CHECK(!fm1_doom_music_sample());
    fm1_doom_music_get_diagnostics(&after);
    CHECK(after.ticks==before.ticks && after.events==before.events);
    fm1_doom_music_pause(0);
    for(i=0;i<315;i++)(void)fm1_doom_music_sample();
    fm1_doom_music_get_diagnostics(&after);CHECK(after.ticks>before.ticks && !after.errors);
    fm1_doom_music_stop_locked();
}

static void monitor_invalid_mode_tests(void)
{
    fm1_doom_music_set_monitor(FM1_DOOM_MUSIC_MONITOR_DRUMS);
    fm1_doom_music_set_monitor(4);CHECK(fm1_doom_music_get_monitor()==FM1_DOOM_MUSIC_MONITOR_FULL);
    fm1_doom_music_set_monitor(FM1_DOOM_MUSIC_MONITOR_PAUSE);
    fm1_doom_music_set_monitor(UINT32_MAX);CHECK(fm1_doom_music_get_monitor()==FM1_DOOM_MUSIC_MONITOR_FULL);
}

enum {EDIT_FRAMES=44100u*2u, EDIT_COMPARE_FRAMES=8192};
static const uint8_t edit_events[]={0,0x40,0,29,0,0x40,3,127,0,0x10,57,127,
    0x8c,1,0,57,0x8c,1,0x60};
static void edit_start(const fm1_doom_music_edit *edit,const uint8_t *events,unsigned length)
{
    static uint8_t bank[1024];unsigned size=literal_score(bank,events,length);
    fm1_doom_music_set_edit_controls(edit);fm1_doom_music_set_synth_mode(edit->preset!=0);
    fm1_doom_music_set_monitor(FM1_DOOM_MUSIC_MONITOR_FULL);
    CHECK(!fm1_doom_music_set_score(bank,size));CHECK(!fm1_doom_music_start(0));
    fm1_doom_music_set_volume(64);
}
static int edit_sample(unsigned frame)
{
    if(!(frame&63u))fm1_doom_music_begin_block();
    return fm1_doom_music_sample();
}
static void edit_control_contract_tests(void)
{
    fm1_doom_music_edit edit=FM1_DOOM_MUSIC_EDIT_DEFAULT,read;
    fm1_doom_music_reset_edit_controls();fm1_doom_music_get_edit_controls(&read);
    CHECK(read.preset==0 && read.algorithm==0);
    CHECK(read.knob[0]==16 && read.knob[1]==72 && read.knob[2]==32 && read.knob[3]==0);
    memset(&edit,255,sizeof(edit));fm1_doom_music_set_edit_controls(&edit);
    fm1_doom_music_get_edit_controls(&read);
    CHECK(read.preset==3 && read.algorithm==3 && fm1_doom_music_get_synth_mode()==1);
    CHECK(read.knob[0]==127 && read.knob[1]==127 && read.knob[2]==127 && read.knob[3]==127);
    fm1_doom_music_set_synth_mode(0);read.algorithm=1;read.knob[2]=8;
    fm1_doom_music_set_edit_controls(&read);CHECK(!fm1_doom_music_get_synth_mode());
    fm1_doom_music_set_edit_controls(0);fm1_doom_music_get_edit_controls(0);
    fm1_doom_music_toggle_synth_mode();CHECK(fm1_doom_music_get_synth_mode()==1);
    fm1_doom_music_get_edit_controls(&edit);CHECK(!memcmp(&edit,&read,sizeof(edit)));
    edit.preset=0;fm1_doom_music_set_edit_controls(&edit);CHECK(!fm1_doom_music_get_synth_mode());
    edit.preset=1;fm1_doom_music_set_edit_controls(&edit);CHECK(fm1_doom_music_get_synth_mode()==1);
    fm1_doom_music_reset_edit_controls();CHECK(!fm1_doom_music_get_synth_mode());
}
static void edit_preserves_original_tests(void)
{
    static int16_t original[EDIT_COMPARE_FRAMES];
    fm1_doom_music_edit edit=FM1_DOOM_MUSIC_EDIT_DEFAULT;unsigned i,k;
    int16_t delay[1024];
    fm1_doom_music_set_reverb_buffer(0,0);edit_start(&edit,edit_events,sizeof(edit_events));
    for(i=0;i<EDIT_COMPARE_FRAMES;i++)original[i]=(int16_t)edit_sample(i);
    fm1_doom_music_set_reverb_buffer(delay,1024);edit_start(&edit,edit_events,sizeof(edit_events));
    for(i=0;i<EDIT_COMPARE_FRAMES;i++){
        if(!(i&63u)){
            edit.algorithm=(uint8_t)((i>>6)&3u);
            for(k=0;k<4;k++)edit.knob[k]=(uint8_t)((i*13u+k*31u)&127u);
            fm1_doom_music_set_edit_controls(&edit);
        }
        CHECK(edit_sample(i)==original[i]);
    }
    fm1_doom_music_set_reverb_buffer(0,0);fm1_doom_music_reset_edit_controls();
}
static void edit_dry_and_delay_bounds_tests(void)
{
    static int16_t dry[EDIT_COMPARE_FRAMES],guarded[1026];
    fm1_doom_music_edit edit=FM1_DOOM_MUSIC_EDIT_DEFAULT;unsigned i,n;
    edit.preset=1;
    fm1_doom_music_set_reverb_buffer(0,0);edit_start(&edit,edit_events,sizeof(edit_events));
    for(i=0;i<EDIT_COMPARE_FRAMES;i++)dry[i]=(int16_t)edit_sample(i);
    guarded[0]=12345;guarded[1025]=-23456;
    fm1_doom_music_set_reverb_buffer(guarded+1,SIZE_MAX);edit_start(&edit,edit_events,sizeof(edit_events));
    for(i=0;i<EDIT_COMPARE_FRAMES;i++)CHECK(edit_sample(i)==dry[i]);
    CHECK(guarded[0]==12345 && guarded[1025]==-23456);
    edit.knob[3]=127;
    for(n=32;n<=1024;n*=2){
        for(i=0;i<1026;i++)guarded[i]=12345;
        fm1_doom_music_set_reverb_buffer(guarded+1,n);edit_start(&edit,edit_events,sizeof(edit_events));
        for(i=0;i<EDIT_COMPARE_FRAMES;i++){
            int sample=edit_sample(i);CHECK(sample>=-8192 && sample<=8191);
        }
        CHECK(guarded[0]==12345 && guarded[n+1]==12345);
    }
    fm1_doom_music_set_reverb_buffer(0,0);edit_start(&edit,edit_events,sizeof(edit_events));
    for(i=0;i<EDIT_COMPARE_FRAMES;i++)dry[i]=(int16_t)edit_sample(i);
    for(i=0;i<1026;i++)guarded[i]=12345;
    fm1_doom_music_set_reverb_buffer(guarded+1,31);edit_start(&edit,edit_events,sizeof(edit_events));
    for(i=0;i<EDIT_COMPARE_FRAMES;i++)CHECK(edit_sample(i)==dry[i]);
    for(i=0;i<1026;i++)CHECK(guarded[i]==12345);
    fm1_doom_music_set_reverb_buffer(0,0);fm1_doom_music_reset_edit_controls();
}
static uint32_t edit_hash(fm1_doom_music_edit edit)
{
    unsigned i;uint32_t hash=2166136261u;
    edit_start(&edit,edit_events,sizeof(edit_events));
    for(i=0;i<EDIT_COMPARE_FRAMES;i++)hash=(hash^(uint16_t)edit_sample(i))*16777619u;
    return hash;
}
static void edit_timbre_and_tail_tests(void)
{
    static int16_t dry[EDIT_FRAMES];int16_t delay[1024];
    fm1_doom_music_edit edit=FM1_DOOM_MUSIC_EDIT_DEFAULT,changed;
    uint32_t original,hash[4];unsigned i,k,tail=0,nonzero=0;
    edit.preset=1;fm1_doom_music_set_reverb_buffer(delay,1024);original=edit_hash(edit);
    for(i=0;i<4;i++){
        changed=edit;changed.algorithm=(uint8_t)i;hash[i]=edit_hash(changed);
        for(k=0;k<i;k++)CHECK(hash[i]!=hash[k]);
    }
    for(i=0;i<4;i++){
        changed=edit;changed.knob[i]=127;
        CHECK(edit_hash(changed)!=original); /* Each knob changes audible PCM. */
    }
    changed=edit;changed.preset=2;CHECK(edit_hash(changed)!=original);
    changed.preset=3;CHECK(edit_hash(changed)!=original);
    edit_start(&edit,edit_events,sizeof(edit_events));
    for(i=0;i<EDIT_FRAMES;i++)dry[i]=(int16_t)edit_sample(i);
    edit.knob[3]=127;edit_start(&edit,edit_events,sizeof(edit_events));
    for(i=0;i<EDIT_FRAMES;i++){
        int sample=edit_sample(i);nonzero+=sample!=0;
        if(i>44100u && !dry[i] && sample)++tail;
    }
    CHECK(nonzero>44100u && tail>100u);
    /* The room send never processes the original OPL percussion stem. */
    edit.knob[3]=0;edit_start(&edit,drum_events,sizeof(drum_events));
    for(i=0;i<MONITOR_FRAMES;i++)dry[i]=(int16_t)edit_sample(i);
    edit.knob[3]=127;edit_start(&edit,drum_events,sizeof(drum_events));
    for(i=0;i<MONITOR_FRAMES;i++)CHECK(edit_sample(i)==dry[i]);
    fm1_doom_music_set_reverb_buffer(0,0);fm1_doom_music_reset_edit_controls();
}
static void edit_sweep_full_voice_tests(void)
{
    static const uint8_t events[]={0,0x40,0,29,0,0x40,3,127,
        0,0x10,40,127,0,0x10,47,127,0,0x10,52,127,0,0x10,59,127,
        0,0x10,64,127,0,0x10,71,127,0,0x10,76,127,0,0x10,83,127,
        0xa4,3,0,40,0,0,47,0,0,52,0,0,59,0,0,64,0,0,71,0,0,76,0,0,83,
        0xa4,3,0x60};
    int16_t guarded[1026];fm1_doom_music_edit edit=FM1_DOOM_MUSIC_EDIT_DEFAULT;
    fm1_doom_music_diagnostics d;unsigned i,k,nonzero=0;int sample;
    guarded[0]=12345;guarded[1025]=-23456;
    edit.preset=1;fm1_doom_music_set_reverb_buffer(guarded+1,1024);edit_start(&edit,events,sizeof(events));
    for(i=0;i<44100u*6u;i++){
        if(!(i&63u)){
            if(i<44100u*3u){
                edit.preset=(uint8_t)((i>>6)&3u);edit.algorithm=(uint8_t)((i>>8)&3u);
                for(k=0;k<4;k++)edit.knob[k]=(uint8_t)((i/64u*13u+k*31u)&127u);
            }else {edit.preset=3;edit.algorithm=2;for(k=0;k<4;k++)edit.knob[k]=127;}
            fm1_doom_music_set_edit_controls(&edit);
        }
        sample=edit_sample(i);CHECK(sample>=-8192 && sample<=8191);nonzero+=sample!=0;
        if(i>=44100u*5u)CHECK(sample==0);
    }
    fm1_doom_music_get_diagnostics(&d);
    CHECK(nonzero>44100 && !d.errors && d.events==18);
    CHECK(guarded[0]==12345 && guarded[1025]==-23456);
    fm1_doom_music_set_reverb_buffer(0,0);fm1_doom_music_reset_edit_controls();
}

int main(void)
{
    unsigned i,nonzero=0,peak=0;int32_t sample;uint64_t power=0;fm1_doom_music_diagnostics d;
    CHECK(fm1_doom_music_get_synth_mode()==0);
    CHECK(fm1_doom_music_get_monitor()==FM1_DOOM_MUSIC_MONITOR_FULL);
#ifdef FM1_TARGET_PI32V2
    module_tests();
#endif
    fm1_doom_music_set_synth_mode(0);
    synthetic_tests();
    synth_tests();
    monitor_percussion_only_tests();
    monitor_melody_only_tests();
    monitor_pause_preserves_stream_tests();
    monitor_preserves_engine_pause_tests();
    monitor_invalid_mode_tests();
    monitor_crossfade_percussion_tests();
    puts("PASS music monitor drum/melody isolation, crossfade, pause ownership and invalid modes");
    edit_control_contract_tests();
    edit_preserves_original_tests();
    edit_dry_and_delay_bounds_tests();
    edit_timbre_and_tail_tests();
    edit_sweep_full_voice_tests();
    puts("PASS live synth controls, OPL preservation, dry bypass, guarded room delay, timbre and release sweeps");
    CHECK(!fm1_doom_music_set_score(fm1_doom_music_score,fm1_doom_music_score_len));
    CHECK(!fm1_doom_music_start(1));
    for(i=0;i<44100u*97u;i++){
        sample=fm1_doom_music_sample();CHECK(sample>=-8192 && sample<=8191);nonzero+=sample!=0;
        power+=(int64_t)sample*sample;if((unsigned)abs(sample)>peak)peak=(unsigned)abs(sample);
    }
    fm1_doom_music_get_diagnostics(&d);
    printf("Measured original OPL2: peak=%u RMS=%.1f events=%lu loops=%lu errors=%lu\n",
           peak,sqrt((double)power/(44100u*97u)),(unsigned long)d.events,
           (unsigned long)d.loops,(unsigned long)d.errors);
    CHECK(nonzero>44100);CHECK(d.loops==1);CHECK(d.events>5824 && !d.errors);CHECK(d.active_voices<=9);
    CHECK(peak>300);CHECK(power/(44100u*97u)>10000u);
    printf("PASS original score loop: ticks=%lu events=%lu steals=%lu active=%u\n",
           (unsigned long)d.ticks,(unsigned long)d.events,(unsigned long)d.voice_steals,d.active_voices);
    printf("Music PCM16: peak=%u RMS=%.1f\n",peak,sqrt((double)power/(44100u*97u)));
    fm1_doom_music_stop_locked();CHECK(!fm1_doom_music_sample());
    CHECK(!fm1_doom_music_start(0));
    for(i=0;i<44100u*97u;i++)(void)fm1_doom_music_sample();
    fm1_doom_music_get_diagnostics(&d);CHECK(!fm1_doom_music_is_playing() && !d.errors);
    return 0;
}
