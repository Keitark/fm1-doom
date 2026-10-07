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
    fm1_doom_music_set_edit_bank(FM1_DOOM_EDIT_SYNTH);
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

static const fm1_doom_music_edit nes_presets[]={
    {0,0,{112,0,19,0}}, {1,1,{92,24,9,0}},
    {2,1,{76,60,9,72}}, {3,2,{84,100,29,96}}
};
static void nes_start(const fm1_doom_music_edit *edit,const uint8_t *events,
                      unsigned length,unsigned source,int loop)
{
    static uint8_t bank[1024];unsigned size=literal_score(bank,events,length);
    fm1_doom_music_set_edit_bank(FM1_DOOM_EDIT_NES_FX);
    fm1_doom_music_set_edit_controls(edit);fm1_doom_music_set_synth_mode(source);
    fm1_doom_music_set_monitor(FM1_DOOM_MUSIC_MONITOR_FULL);
    CHECK(!fm1_doom_music_set_score(bank,size));CHECK(!fm1_doom_music_start(loop));
    fm1_doom_music_set_volume(64);
}
static uint32_t nes_hash(fm1_doom_music_edit edit)
{
    unsigned i;uint32_t hash=2166136261u;
    nes_start(&edit,edit_events,sizeof(edit_events),0,0);
    for(i=0;i<EDIT_FRAMES;i++)hash=(hash^(uint16_t)edit_sample(i))*16777619u;
    return hash;
}
static void nes_banks_preserve_source_and_independent_controls_tests(void)
{
    fm1_doom_music_edit synth={2,3,{1,2,3,4}},nes={3,2,{84,100,29,96}},read;
    static const fm1_doom_music_edit default_synth=FM1_DOOM_MUSIC_EDIT_DEFAULT;
    static const fm1_doom_music_edit default_nes=FM1_DOOM_NES_FX_EDIT_DEFAULT;
    fm1_doom_music_reset_edit_controls();
    CHECK(fm1_doom_music_get_edit_bank()==FM1_DOOM_EDIT_SYNTH);
    fm1_doom_music_set_edit_controls(&synth);CHECK(fm1_doom_music_get_synth_mode()==1);
    fm1_doom_music_set_synth_mode(0);fm1_doom_music_set_edit_bank(FM1_DOOM_EDIT_NES_FX);
    CHECK(!fm1_doom_music_get_synth_mode());
    fm1_doom_music_get_edit_controls(&read);CHECK(!memcmp(&read,&default_nes,sizeof(read)));
    fm1_doom_music_set_edit_controls(&nes);CHECK(!fm1_doom_music_get_synth_mode());
    fm1_doom_music_set_edit_bank(FM1_DOOM_EDIT_SYNTH);
    fm1_doom_music_get_edit_controls(&read);CHECK(!memcmp(&read,&synth,sizeof(read)));
    CHECK(!fm1_doom_music_get_synth_mode());
    fm1_doom_music_set_edit_bank(FM1_DOOM_EDIT_NES_FX);
    fm1_doom_music_get_edit_controls(&read);CHECK(!memcmp(&read,&nes,sizeof(read)));
    fm1_doom_music_toggle_synth_mode();CHECK(fm1_doom_music_get_synth_mode()==1);
    fm1_doom_music_set_edit_bank(FM1_DOOM_EDIT_SYNTH);CHECK(fm1_doom_music_get_synth_mode()==1);
    fm1_doom_music_reset_edit_controls();CHECK(!fm1_doom_music_get_synth_mode());
    CHECK(fm1_doom_music_get_edit_bank()==FM1_DOOM_EDIT_SYNTH);
    fm1_doom_music_get_edit_controls(&read);CHECK(!memcmp(&read,&default_synth,sizeof(read)));
    fm1_doom_music_set_edit_bank(FM1_DOOM_EDIT_NES_FX);
    fm1_doom_music_get_edit_controls(&read);CHECK(!memcmp(&read,&default_nes,sizeof(read)));
    fm1_doom_music_reset_edit_controls();
}
static void nes_control_setter_clamps_without_reloading_or_toggling_source_tests(void)
{
    fm1_doom_music_edit edit,read,before;
    fm1_doom_music_reset_edit_controls();fm1_doom_music_set_edit_bank(FM1_DOOM_EDIT_NES_FX);
    memset(&edit,255,sizeof(edit));fm1_doom_music_set_edit_controls(&edit);
    fm1_doom_music_get_edit_controls(&read);
    CHECK(read.preset==3 && read.algorithm==3 && !fm1_doom_music_get_synth_mode());
    CHECK(read.knob[0]==127 && read.knob[1]==127 && read.knob[2]==127 && read.knob[3]==127);
    before=read;fm1_doom_music_set_edit_controls(0);fm1_doom_music_get_edit_controls(0);
    fm1_doom_music_get_edit_controls(&read);CHECK(!memcmp(&read,&before,sizeof(read)));
    edit.preset=1;edit.algorithm=2;edit.knob[0]=14;edit.knob[1]=25;edit.knob[2]=36;edit.knob[3]=47;
    fm1_doom_music_set_edit_controls(&edit);fm1_doom_music_get_edit_controls(&read);
    CHECK(!memcmp(&read,&edit,sizeof(read)) && !fm1_doom_music_get_synth_mode());
    fm1_doom_music_reset_edit_controls();
}
static void nes_dry_and_algorithm_bypass_preserve_original_pcm_tests(void)
{
    static int16_t original[EDIT_COMPARE_FRAMES];
    fm1_doom_music_edit synth=FM1_DOOM_MUSIC_EDIT_DEFAULT,nes=FM1_DOOM_NES_FX_EDIT_DEFAULT;
    unsigned i;
    fm1_doom_music_reset_edit_controls();edit_start(&synth,edit_events,sizeof(edit_events));
    for(i=0;i<EDIT_COMPARE_FRAMES;i++)original[i]=(int16_t)edit_sample(i);
    nes_start(&nes,edit_events,sizeof(edit_events),0,0);
    for(i=0;i<EDIT_COMPARE_FRAMES;i++)CHECK(edit_sample(i)==original[i]);
    nes.preset=3;memset(nes.knob,127,sizeof(nes.knob));
    nes_start(&nes,edit_events,sizeof(edit_events),0,0);
    for(i=0;i<EDIT_COMPARE_FRAMES;i++)CHECK(edit_sample(i)==original[i]);
    fm1_doom_music_reset_edit_controls();
}
static void nes_presets_and_filter_algorithms_change_pcm_tests(void)
{
    uint32_t hashes[4],algorithms[4];fm1_doom_music_edit edit=nes_presets[1];unsigned i,k;
    fm1_doom_music_reset_edit_controls();
    for(i=0;i<4;i++){
        hashes[i]=nes_hash(nes_presets[i]);
        for(k=0;k<i;k++)CHECK(hashes[i]!=hashes[k]);
    }
    edit.knob[0]=64;edit.knob[1]=80;
    for(i=0;i<4;i++){
        edit.algorithm=(uint8_t)i;algorithms[i]=nes_hash(edit);
        for(k=0;k<i;k++)CHECK(algorithms[i]!=algorithms[k]);
    }
    fm1_doom_music_reset_edit_controls();
}
static void nes_cutoff_resonance_rate_and_depth_each_change_pcm_tests(void)
{
    fm1_doom_music_edit edit=nes_presets[3],changed;unsigned i;uint32_t original;
    static const uint8_t alternate[]={20,12,100,0};
    fm1_doom_music_reset_edit_controls();original=nes_hash(edit);
    for(i=0;i<4;i++){
        changed=edit;changed.knob[i]=alternate[i];CHECK(nes_hash(changed)!=original);
    }
    fm1_doom_music_reset_edit_controls();
}
static void nes_filter_processes_original_opl_percussion_tests(void)
{
    static int16_t dry[MONITOR_FRAMES];unsigned i,changed=0;
    fm1_doom_music_edit edit=nes_presets[0];
    fm1_doom_music_reset_edit_controls();nes_start(&edit,drum_events,sizeof(drum_events),0,0);
    for(i=0;i<MONITOR_FRAMES;i++)dry[i]=(int16_t)edit_sample(i);
    edit=nes_presets[3];nes_start(&edit,drum_events,sizeof(drum_events),0,0);
    for(i=0;i<MONITOR_FRAMES;i++)changed+=edit_sample(i)!=dry[i];
    CHECK(changed>100);fm1_doom_music_reset_edit_controls();
}
static void nes_bank_disable_fades_back_to_exact_original_pcm_tests(void)
{
    enum {FRAMES=32768};static int16_t original[FRAMES];
    fm1_doom_music_edit synth=FM1_DOOM_MUSIC_EDIT_DEFAULT,nes=nes_presets[3];
    unsigned i,changed=0;int sample;
    fm1_doom_music_reset_edit_controls();edit_start(&synth,edit_events,sizeof(edit_events));
    for(i=0;i<FRAMES;i++)original[i]=(int16_t)edit_sample(i);
    nes_start(&nes,edit_events,sizeof(edit_events),0,0);
    for(i=0;i<FRAMES;i++){
        if(i==8192)fm1_doom_music_set_edit_bank(FM1_DOOM_EDIT_SYNTH);
        sample=edit_sample(i);CHECK(sample>=-8192 && sample<=8191);
        if(i<8192)changed+=sample!=original[i];
        if(i>=16384)CHECK(sample==original[i]);
    }
    CHECK(changed>100 && !fm1_doom_music_get_synth_mode());
    fm1_doom_music_reset_edit_controls();
}
static void nes_restart_retains_controls_and_resets_filter_state_tests(void)
{
    fm1_doom_music_edit edit=nes_presets[3],read;uint32_t first,second;
    fm1_doom_music_reset_edit_controls();first=nes_hash(edit);
    fm1_doom_music_get_edit_controls(&read);CHECK(!memcmp(&read,&edit,sizeof(read)));
    second=nes_hash(edit);CHECK(first==second);
    CHECK(fm1_doom_music_get_edit_bank()==FM1_DOOM_EDIT_NES_FX);
    fm1_doom_music_reset_edit_controls();
}
static void nes_partition_and_loop_preserve_score_tempo_tests(void)
{
    enum {FRAMES=MONITOR_FRAMES*3};static int16_t output[FRAMES];
    static const unsigned partitions[]={1,17,128,3,79};
    fm1_doom_music_edit edit=nes_presets[3];fm1_doom_music_diagnostics dry,wet,replay;
    unsigned i,frame,part,remaining;int16_t left,right;
    fm1_doom_music_reset_edit_controls();nes_start(nes_presets,melody_events,sizeof(melody_events),0,1);
    for(i=0;i<FRAMES;i++)(void)edit_sample(i);
    fm1_doom_music_get_diagnostics(&dry);
    nes_start(&edit,melody_events,sizeof(melody_events),0,1);
    for(i=0;i<FRAMES;i++)output[i]=(int16_t)edit_sample(i);
    fm1_doom_music_get_diagnostics(&wet);
    CHECK(dry.ticks==wet.ticks && dry.events==wet.events && dry.loops==wet.loops && wet.loops>0);
    CHECK(dry.voice_steals==wet.voice_steals && dry.active_voices==wet.active_voices && !wet.errors);
    nes_start(&edit,melody_events,sizeof(melody_events),0,1);
    for(frame=0,part=0;frame<FRAMES;part++){
        remaining=partitions[part%5];if(remaining>FRAMES-frame)remaining=FRAMES-frame;
        for(i=0;i<remaining;i++,frame++){
            if(!(frame&63u))fm1_doom_music_begin_block();
            fm1_doom_music_sample_stereo(&left,&right);CHECK(left==right && left==output[frame]);
        }
    }
    fm1_doom_music_get_diagnostics(&replay);
    CHECK(replay.ticks==wet.ticks && replay.events==wet.events && replay.loops==wet.loops && !replay.errors);
    fm1_doom_music_stop_locked();fm1_doom_music_reset_edit_controls();
}
static void nes_live_sweep_preserves_headroom_and_silent_tail_tests(void)
{
    static const uint8_t events[]={0,0x40,0,29,0,0x40,3,127,
        0,0x10,40,127,0,0x10,47,127,0,0x10,52,127,0,0x10,59,127,
        0,0x10,64,127,0,0x10,71,127,0,0x10,76,127,0,0x10,83,127,
        0xa4,3,0,40,0,0,47,0,0,52,0,0,59,0,0,64,0,0,71,0,0,76,0,0,83,
        0xa4,3,0x60};
    fm1_doom_music_edit edit=nes_presets[3];fm1_doom_music_diagnostics d;
    unsigned i,k,source,nonzero;int sample;
    for(source=0;source<2;source++){
        fm1_doom_music_reset_edit_controls();nes_start(&edit,events,sizeof(events),source,0);nonzero=0;
        for(i=0;i<44100u*6u;i++){
            if(!(i&63u) && i<44100u*3u){
                edit.preset=(uint8_t)(1u+(i>>6)%3u);edit.algorithm=(uint8_t)(1u+(i>>8)%3u);
                for(k=0;k<4;k++)edit.knob[k]=(uint8_t)((i/64u*13u+k*31u)&127u);
                fm1_doom_music_set_edit_controls(&edit);
            }
            sample=edit_sample(i);CHECK(sample>=-8192 && sample<=8191);nonzero+=sample!=0;
            if(i>=44100u*5u)CHECK(sample==0);
        }
        fm1_doom_music_get_diagnostics(&d);CHECK(nonzero>44100 && !d.errors && d.events==18);
        CHECK(fm1_doom_music_get_synth_mode()==source);
    }
    fm1_doom_music_reset_edit_controls();
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
    nes_banks_preserve_source_and_independent_controls_tests();
    nes_control_setter_clamps_without_reloading_or_toggling_source_tests();
    nes_dry_and_algorithm_bypass_preserve_original_pcm_tests();
    nes_presets_and_filter_algorithms_change_pcm_tests();
    nes_cutoff_resonance_rate_and_depth_each_change_pcm_tests();
    nes_filter_processes_original_opl_percussion_tests();
    nes_bank_disable_fades_back_to_exact_original_pcm_tests();
    nes_restart_retains_controls_and_resets_filter_state_tests();
    nes_partition_and_loop_preserve_score_tempo_tests();
    nes_live_sweep_preserves_headroom_and_silent_tail_tests();
    puts("PASS NES FX banks, source ownership, exact dry bypass, filter/LFO controls, restart, tempo, headroom and tails");
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
