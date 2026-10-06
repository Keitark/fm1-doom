#include "fm1_doom_music.h"
#include "fm1_doom_opl.h"
#include <string.h>

enum {TICK_FRAMES=315, STACK_SIZE=16, MAX_EVENTS_PER_TICK=128};
extern const uint8_t fm1_doom_genmidi_bank[];
extern const uint32_t fm1_doom_genmidi_bank_len;
static struct {
    const uint8_t *bank, *nodes, *tokens;
    uint32_t raw_size, packed_size, position, raw_left, delay;
    uint32_t ticks, events, loops, errors;
    uint8_t stack[STACK_SIZE], stack_count, node_count, playing, paused, looping, master;
    uint16_t sample_count;
} music;

enum {SYNTH_VOICES=8, ATTACK=0, DECAY=1, SUSTAIN=2, RELEASE=3};
typedef struct {
    uint32_t phase,detuned,increment;
    uint16_t envelope;
    uint8_t note,channel,velocity,state;
} synth_voice;
static struct {
    synth_voice voices[SYNTH_VOICES];
    struct {uint8_t program,volume,wheel;} channels[16];
    struct {int32_t low,band;} filters[3];
    uint16_t blend;
} synth;
static uint8_t synth_mode;
static uint8_t monitor_mode;
static fm1_doom_music_edit edit_controls=FM1_DOOM_MUSIC_EDIT_DEFAULT;
static uint8_t last_synth_preset=1;
typedef struct {
    uint16_t detune,cutoff,damping,attack,release,wet,band_mix,contour;
} synth_parameters;
static synth_parameters parameters;
static uint16_t algorithm_weights[4]={256,0,0,0};
static struct {
    int16_t *buffer;
    uint16_t size,position,tap[3];
    uint8_t phase;
    int32_t input,filtered,previous,current;
} room;
/* C0..B0 phase increments at44.1kHz. Octaves are exact powers of two. */
static const uint32_t synth_notes[12]={796254u,843601u,893765u,946911u,1003217u,1062871u,
    1126073u,1193033u,1263974u,1339134u,1418763u,1503127u};

static uint32_t synth_note(unsigned note){return synth_notes[note%12]<<(note/12);}
static void synth_pitch(synth_voice *v)
{
    unsigned wheel=synth.channels[v->channel].wheel;
    uint32_t base=synth_note(v->note),limit;
    if(wheel>=128){limit=synth_note(v->note>125?127:v->note+2);v->increment=base+((limit-base)>>7)*(wheel-128);}
    else{limit=synth_note(v->note<2?0:v->note-2);v->increment=base-((base-limit)>>7)*(128-wheel);}
}
static void synth_restart(void)
{
    unsigned i;for(i=0;i<16;i++){
        synth.channels[i].program=0;synth.channels[i].volume=100;synth.channels[i].wheel=128;
    }
}
static synth_parameters synth_targets(void)
{
    unsigned preset=edit_controls.preset?edit_controls.preset:last_synth_preset;
    unsigned algorithm=edit_controls.algorithm,cutoff=edit_controls.knob[1];
    synth_parameters p;
    p.detune=(uint16_t)(edit_controls.knob[0]>>1);
    p.cutoff=(uint16_t)(preset==2?768u+cutoff*64u:preset==3?768u+cutoff*32u:512u+cutoff*48u);
    p.damping=(uint16_t)(algorithm==1?28000u:algorithm==3?24000u:19661u);
    p.attack=(uint16_t)(8u+(127u-edit_controls.knob[2])*2u);
    if(preset==3)p.attack=(uint16_t)((p.attack+1u)>>1);
    p.release=(uint16_t)(8u+((127u-edit_controls.knob[2])>>2));
    if(preset==3)p.release=(uint16_t)((p.release+1u)>>1);
    /* Convex dry/wet mix: even Room's maximum send cannot amplify the dry
     * signal. The separate bounded feedback remains fixed below 0.6. */
    p.wet=(uint16_t)(edit_controls.knob[3]*(preset==3?192u:128u));
    p.band_mix=(uint16_t)(algorithm==3?160u:0u);
    p.contour=(uint16_t)(preset==2?10u:preset==3?4u:8u);
    return p;
}
static uint16_t approach(uint16_t current,uint16_t target)
{
    if(current<target)return (uint16_t)(current+((target-current+7u)>>3));
    if(current>target)return (uint16_t)(current-((current-target+7u)>>3));
    return current;
}
void fm1_doom_music_begin_block(void)
{
    synth_parameters target=synth_targets();unsigned i,removed=0;
    parameters.detune=approach(parameters.detune,target.detune);
    parameters.cutoff=approach(parameters.cutoff,target.cutoff);
    parameters.damping=approach(parameters.damping,target.damping);
    parameters.attack=approach(parameters.attack,target.attack);
    parameters.release=approach(parameters.release,target.release);
    parameters.wet=approach(parameters.wet,target.wet);
    parameters.band_mix=approach(parameters.band_mix,target.band_mix);
    parameters.contour=approach(parameters.contour,target.contour);
    /* Maintain a sum of 256 while fading the four VCO configurations. This
     * also handles a second algorithm change before the first has settled. */
    for(i=0;i<4;i++)if(i!=edit_controls.algorithm){
        unsigned amount=(algorithm_weights[i]+7u)>>3;
        algorithm_weights[i]=(uint16_t)(algorithm_weights[i]-amount);removed+=amount;
    }
    algorithm_weights[edit_controls.algorithm]=(uint16_t)(algorithm_weights[edit_controls.algorithm]+removed);
}
static void room_reset(void)
{
    room.position=0;room.phase=0;room.input=room.filtered=room.previous=room.current=0;
    if(room.buffer)memset(room.buffer,0,(size_t)room.size*sizeof(*room.buffer));
}
void fm1_doom_music_set_reverb_buffer(int16_t *buffer,size_t samples)
{
    if(!buffer || samples<32){room.buffer=0;room.size=0;}
    else{room.buffer=buffer;room.size=(uint16_t)(samples>1024?1024:samples);}
    room.tap[0]=(uint16_t)(((unsigned)room.size*239u)>>10);
    room.tap[1]=(uint16_t)(((unsigned)room.size*449u)>>10);
    room.tap[2]=(uint16_t)(((unsigned)room.size*701u)>>10);
    room_reset();
}
void fm1_doom_music_set_edit_controls(const fm1_doom_music_edit *edit)
{
    fm1_doom_music_edit bounded;unsigned i,changed;
    if(!edit)return;
    bounded=*edit;
    if(bounded.preset>3)bounded.preset=3;
    if(bounded.algorithm>3)bounded.algorithm=3;
    for(i=0;i<4;i++)if(bounded.knob[i]>127)bounded.knob[i]=127;
    changed=bounded.preset!=edit_controls.preset;edit_controls=bounded;
    if(bounded.preset)last_synth_preset=bounded.preset;
    if(changed)fm1_doom_music_set_synth_mode(bounded.preset!=0);
}
void fm1_doom_music_get_edit_controls(fm1_doom_music_edit *edit){if(edit)*edit=edit_controls;}
void fm1_doom_music_reset_edit_controls(void)
{
    static const fm1_doom_music_edit defaults=FM1_DOOM_MUSIC_EDIT_DEFAULT;
    fm1_doom_music_set_edit_controls(&defaults);last_synth_preset=1;fm1_doom_music_set_synth_mode(0);
}
static void synth_reset(void)
{
    memset(&synth,0,sizeof(synth));synth_restart();synth.blend=synth_mode?256:0;
    parameters=synth_targets();memset(algorithm_weights,0,sizeof(algorithm_weights));
    algorithm_weights[edit_controls.algorithm]=256;room_reset();
}
static void synth_event(unsigned kind,unsigned ch,unsigned a,unsigned b)
{
    unsigned i,chosen=0,best=0xffffffffu;
    if(ch==15)return;
    if(kind==0 || (kind==1 && !b)){
        for(i=0;i<SYNTH_VOICES;i++)if(synth.voices[i].channel==ch && synth.voices[i].note==a)
            synth.voices[i].state=(uint8_t)((synth.voices[i].state&~3u)|RELEASE);
    }else if(kind==1){
        unsigned program=synth.channels[ch].program,group=program==29?0:program==30?1:2;
        for(i=0;i<SYNTH_VOICES;i++){
            synth_voice *v=&synth.voices[i];
            unsigned priority=v->envelope;
            if(!priority){chosen=i;break;}
            if((v->state&3u)!=RELEASE)priority+=32768u;
            if(priority<best){best=priority;chosen=i;}
        }
        synth.voices[chosen]=(synth_voice){0,0,0,1,(uint8_t)a,(uint8_t)ch,(uint8_t)b,(uint8_t)(group<<2)};
        synth_pitch(&synth.voices[chosen]);
    }else if(kind==2){
        synth.channels[ch].wheel=(uint8_t)a;
        for(i=0;i<SYNTH_VOICES;i++)if(synth.voices[i].envelope && synth.voices[i].channel==ch)synth_pitch(&synth.voices[i]);
    }else if(kind==3){
        if(a==10 || a==11 || a==13 || a==14)for(i=0;i<SYNTH_VOICES;i++)if(synth.voices[i].channel==ch)
            synth.voices[i].state=(uint8_t)((synth.voices[i].state&~3u)|RELEASE);
    }else if(kind==4){
        if(!a)synth.channels[ch].program=(uint8_t)b;
        else if(a==3)synth.channels[ch].volume=(uint8_t)b;
    }
}
static int synth_triangle(uint32_t phase)
{
    unsigned p=phase>>20;return p<2048?(int)p-1024:3071-(int)p;
}
static int synth_bound(int value){return value>32767?32767:value<-32767?-32767:value;}
static int synth_wave(unsigned group,unsigned algorithm,uint32_t phase,uint32_t detuned)
{
    if(algorithm==1)return (synth_triangle(phase)+synth_triangle(detuned))/2;
    if(algorithm==2)return ((phase<0x80000000u?1024:-1024)+(((int)(detuned>>20)-2048)>>1))>>1;
    if(algorithm==3)return (((int)(phase>>20)-2048)+synth_triangle(detuned))>>2;
    if(group==0)return (((int)(phase>>20)-2048)+((int)(detuned>>20)-2048))/4;
    if(group==1)return (synth_triangle(phase)+(detuned<0x80000000u?1024:-1024))/2;
    return (synth_triangle(phase)+synth_triangle(detuned)/2)/2;
}
static int room_sample(int dry)
{
    unsigned i,index;int reflection,wet,difference,feedback;
    if(!room.buffer || !parameters.wet)return dry;
    /* The delay runs at 11025 Hz; four input frames are averaged. Its wet
     * output is filtered and linearly interpolated back to 44100 Hz. */
    room.input+=synth_bound(dry);
    wet=room.previous+(((room.current-room.previous)*(int)room.phase)>>2);
    if(++room.phase==4){
        reflection=0;
        for(i=0;i<3;i++){
            index=room.position>=room.tap[i]?room.position-room.tap[i]:room.position+room.size-room.tap[i];
            reflection+=(int)room.buffer[index]*(i?1:2);
        }
        reflection>>=2;difference=reflection-room.filtered;
        room.filtered+=difference<0?-((-difference+3)>>2):(difference+3)>>2;
        /* 17000/32768 = 0.519 feedback; all taps are a normalized average.
         * PCM16 storage and bounded inputs keep every multiply in int32. */
        feedback=room.filtered*17000;
        feedback=feedback<0?-((-feedback)>>15):feedback>>15;
        room.buffer[room.position]=(int16_t)synth_bound((room.input>>2)+feedback);
        if(++room.position==room.size)room.position=0;
        room.previous=room.current;room.current=room.filtered;room.input=0;room.phase=0;
    }
    return (dry*(32768-(int)parameters.wet)+wet*(int)parameters.wet)>>15;
}
static int synth_sample(void)
{
    int input[3]={0,0,0},output=0;unsigned envelopes[3]={0,0,0},i;
    for(i=0;i<SYNTH_VOICES;i++){
        synth_voice *v=&synth.voices[i];unsigned group=v->state>>2,stage=v->state&3u,sustain,level,step;
        int wave=0,sample;unsigned algorithm;
        if(!v->envelope)continue;
        if(stage==ATTACK){step=group==0?parameters.attack>>1:group==1?(parameters.attack*11u)>>4:(parameters.attack*7u)>>4;if(!step)step=1;level=v->envelope+step;if(level>=32767){level=32767;v->state=(uint8_t)((group<<2)|DECAY);}v->envelope=(uint16_t)level;}
        else if(stage==DECAY){sustain=group==0?12288u:group==1?8192u:16384u;step=((v->envelope-sustain)>>11)+1u;if(v->envelope<=sustain+step){v->envelope=(uint16_t)sustain;v->state=(uint8_t)((group<<2)|SUSTAIN);}else v->envelope=(uint16_t)(v->envelope-step);}
        else if(stage==RELEASE){step=((v->envelope*(unsigned)parameters.release)>>(group==2?16:15))+1u;v->envelope=(uint16_t)(v->envelope>step?v->envelope-step:0);}
        v->phase+=v->increment;
        v->detuned+=group==2?(v->increment>>1)+(v->increment>>13)*parameters.detune:v->increment+(v->increment>>12)*parameters.detune;
        /* Two VCOs: detuned saws for29; pulse/triangle for30; triangle and
         * a sub-oscillator for bass34. Each is centered and bounded. */
        if(algorithm_weights[edit_controls.algorithm]==256)
            wave=synth_wave(group,edit_controls.algorithm,v->phase,v->detuned);
        else{
            for(algorithm=0;algorithm<4;algorithm++)if(algorithm_weights[algorithm])
                wave+=synth_wave(group,algorithm,v->phase,v->detuned)*(int)algorithm_weights[algorithm];
            wave>>=8;
        }
        sample=(wave*(int)v->envelope)>>15;
        sample=sample*(int)v->velocity/127;
        sample=sample*(int)synth.channels[v->channel].volume/127;
        input[group]+=sample;if(v->envelope>envelopes[group])envelopes[group]=v->envelope;
    }
    for(i=0;i<3;i++){
        int low=synth.filters[i].low,band=synth.filters[i].band;
        int coefficient=(int)(i==2?parameters.cutoff>>1:parameters.cutoff)+(int)((envelopes[i]*parameters.contour)>>(i==2?7:5));
        int damping=(int)parameters.damping+(i==2?4096:0),high;
        if(coefficient>12287)coefficient=12287;
        if(damping>30000)damping=30000;
        /* Q15 Chamberlin VCF: coefficient<=12287/32768, damping>=0.6.
         * Its poles stay inside the unit circle; clamped state also bounds
         * every32-bit intermediate even for eight full-scale held notes. */
        low=synth_bound(low+((coefficient*band)>>15));
        high=input[i]-low-((damping*band)>>15);
        band=synth_bound(band+((coefficient*high)>>15));
        if(!envelopes[i] && low>-32 && low<32 && band>-32 && band<32)low=band=0;
        synth.filters[i].low=low;synth.filters[i].band=band;
        output+=(low*(256-(int)parameters.band_mix)+band*(int)parameters.band_mix)>>8;
    }
    /* Room affects only analog melodic synthesis. OPL percussion remains
     * outside this path, and the sound backend still applies master volume
     * once to the completed music/PCM mix. */
    return room_sample(synth_bound(output*2))*(int)music.master/64;
}

static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void fail(void){music.playing=0;++music.errors;fm1_doom_opl_stop();}

static int score_byte(uint8_t *output)
{
    unsigned work;
    uint8_t token,left,right;
    if(!music.raw_left)return 0;
    for(work=0;work<STACK_SIZE*2u;work++){
        if(music.stack_count)token=music.stack[--music.stack_count];
        else {
            if(music.position>=music.packed_size)return 0;
            token=music.tokens[music.position++];
        }
        if(token>=music.node_count)return 0;
        left=music.nodes[token*2u];right=music.nodes[token*2u+1u];
        if(right==255u){*output=left;--music.raw_left;return 1;}
        if(music.stack_count>STACK_SIZE-2u)return 0;
        music.stack[music.stack_count++]=right;music.stack[music.stack_count++]=left;
    }
    return 0;
}

static int next_delay(void)
{
    uint32_t value=0;unsigned shift;
    uint8_t part;
    for(shift=0;shift<28;shift+=7){
        if(!score_byte(&part))return 0;
        value|=(uint32_t)(part&127u)<<shift;
        if(!(part&128u)){music.delay=value;return 1;}
    }
    return 0;
}

static void reset_stream(void)
{
    music.position=0;music.raw_left=music.raw_size;music.stack_count=0;music.sample_count=0;
    fm1_doom_opl_restart();
    synth_restart();
    if(!next_delay())fail();
}

int fm1_doom_music_set_score(const uint8_t *score,size_t length)
{
    uint32_t raw,packed;unsigned count,i;
    uint8_t depths[255];
    fm1_doom_music_stop_locked();music.bank=0;
    if(!score || length<16 || memcmp(score,"FMSC",4))return -1;
    raw=le32(score+4);count=(unsigned)score[8]|(unsigned)score[9]<<8;packed=le32(score+12);
    if(!raw || raw>1000000u || !count || count>255u || score[10]!=140 || score[11]
       || length!=16u+count*2u+packed || !packed)return -1;
    for(i=0;i<count;i++){
        unsigned left=score[16+i*2u],right=score[17+i*2u],depth=1;
        if(right!=255u){
            if(left>=i || right>=i)return -1;
            depth=1u+(depths[left]>depths[right]?depths[left]:depths[right]);
        }
        if(depth>STACK_SIZE)return -1;
        depths[i]=(uint8_t)depth;
    }
    music.bank=score;music.nodes=score+16;music.tokens=score+16+count*2u;
    music.raw_size=raw;music.packed_size=packed;music.node_count=(uint8_t)count;
    if(fm1_doom_opl_set_bank(fm1_doom_genmidi_bank,fm1_doom_genmidi_bank_len)){music.bank=0;return -1;}
    music.master=64;
    music.ticks=music.events=music.loops=music.errors=0;
    return 0;
}

int fm1_doom_music_start(int loop)
{
    if(!music.bank)return -1;
    music.playing=1;music.paused=0;music.looping=loop!=0;fm1_doom_opl_start(music.master);synth_reset();reset_stream();
    return music.playing?0:-1;
}
void fm1_doom_music_stop_locked(void){music.playing=0;music.paused=0;fm1_doom_opl_stop();memset(&synth,0,sizeof(synth));}
void fm1_doom_music_pause(int paused){music.paused=paused!=0;}
void fm1_doom_music_set_volume(unsigned volume){music.master=(uint8_t)(volume>127?127:volume);fm1_doom_opl_volume(music.master);}
void fm1_doom_music_set_synth_mode(unsigned mode){synth_mode=mode!=0;if(!music.playing)synth.blend=synth_mode?256:0;}
unsigned fm1_doom_music_get_synth_mode(void){return synth_mode;}
void fm1_doom_music_toggle_synth_mode(void){fm1_doom_music_set_synth_mode(!synth_mode);}
void fm1_doom_music_set_monitor(unsigned mode){monitor_mode=(uint8_t)(mode<=FM1_DOOM_MUSIC_MONITOR_PAUSE?mode:FM1_DOOM_MUSIC_MONITOR_FULL);}
unsigned fm1_doom_music_get_monitor(void){return monitor_mode;}
int fm1_doom_music_is_playing(void){return music.playing;}

static int event(void)
{
    uint8_t descriptor,a=0,b=0;unsigned kind,ch;
    if(!score_byte(&descriptor))return 0;
    kind=descriptor>>4;ch=descriptor&15;
    if(kind==6){
        if(music.raw_left || music.stack_count || music.position!=music.packed_size)return 0;
        if(music.looping){++music.loops;reset_stream();}else fm1_doom_music_stop_locked();
        return 1;
    }
    if(kind>4 || !score_byte(&a))return 0;
    if((kind==1 || kind==4) && !score_byte(&b))return 0;
    if((kind==0 || kind==1) && (a>127 || b>127))return 0;
    if(fm1_doom_opl_event(kind,ch,a,b))return 0;
    synth_event(kind,ch,a,b);
    ++music.events;
    return next_delay();
}

static void tick(void)
{
    unsigned work=0;
    if(music.delay)--music.delay;
    while(music.playing && !music.delay){
        if(++work>MAX_EVENTS_PER_TICK || !event()){fail();break;}
    }
    ++music.ticks;
}

static int16_t clip(int value){return (int16_t)(value>8191?8191:value<-8192?-8192:value);}

void fm1_doom_music_sample_stereo(int16_t *left,int16_t *right)
{
    int sample,analog;int16_t full,drums;
    if(!left || !right)return;
    *left=*right=0;
    if(!music.playing || music.paused || monitor_mode==FM1_DOOM_MUSIC_MONITOR_PAUSE)return;
    if(!music.sample_count){tick();music.sample_count=TICK_FRAMES;}
    --music.sample_count;
    if(!music.playing)return;
    /* Settled DOS mode needs only the original mono OPL sample. Keep its
     * clock and MUS events running exactly as before, while the inaudible
     * optional VCO/VCF and percussion stem do no per-sample work. MIDI
     * events still update synth voices; their oscillators/envelopes resume
     * when the user switches back, through the existing short crossfade. */
    if(!synth_mode && !synth.blend && !monitor_mode){
        sample=(int)fm1_doom_opl_sample()*4;
        *left=*right=music.master?clip(sample):0;
        return;
    }
    /* DOS OPL2 is mono. Original DMX music-volume and GENMIDI operator-level
     * rules run inside the driver. A fixed gain leaves room for
     * the independent PCM gunshot mixer; no instrument is rebalanced. */
    fm1_doom_opl_sample_split(&full,&drums);
    analog=(synth_mode || synth.blend)?synth_sample():0;
    if(synth_mode){if(synth.blend<256)synth.blend+=4;}
    else if(synth.blend)synth.blend-=4;
    if(monitor_mode==FM1_DOOM_MUSIC_MONITOR_MELODY)
        sample=(((int)full-(int)drums)*4*(256-(int)synth.blend)+analog*(int)synth.blend)/256;
    else if(monitor_mode==FM1_DOOM_MUSIC_MONITOR_DRUMS)
        sample=((int)drums*4*(256-(int)synth.blend)+(int)drums*2*(int)synth.blend)/256;
    else
        sample=((int)full*4*(256-(int)synth.blend)+(analog+(int)drums*2)*(int)synth.blend)/256;
    *left=*right=music.master?clip(sample):0;
}
int32_t fm1_doom_music_sample(void){int16_t l,r;fm1_doom_music_sample_stereo(&l,&r);return ((int32_t)l+r)/2;}
void fm1_doom_music_get_diagnostics(fm1_doom_music_diagnostics *d)
{
    if(!d)return;
    *d=(fm1_doom_music_diagnostics){music.ticks,music.events,music.loops,
        fm1_doom_opl_steals(),music.errors,fm1_doom_opl_active()};
}

#ifdef FM1_TARGET_PI32V2
extern const uint8_t fm1_doom_music_score[];
extern const uint32_t fm1_doom_music_score_len;
extern unsigned fm1_doom_sound_lock(void);
extern void fm1_doom_sound_unlock(unsigned);
extern int fm1_doom_sound_is_ready(void);
static boolean module_init(void){unsigned f;int rc;if(!fm1_doom_sound_is_ready())return false;f=fm1_doom_sound_lock();rc=fm1_doom_music_set_score(fm1_doom_music_score,fm1_doom_music_score_len);fm1_doom_sound_unlock(f);return rc==0;}
static void module_stop(void){unsigned f=fm1_doom_sound_lock();fm1_doom_music_stop_locked();fm1_doom_sound_unlock(f);}
static void module_volume(int volume){unsigned f=fm1_doom_sound_lock();fm1_doom_music_set_volume(volume<0?0:(unsigned)volume);fm1_doom_sound_unlock(f);}
static void module_pause(void){unsigned f=fm1_doom_sound_lock();fm1_doom_music_pause(1);fm1_doom_sound_unlock(f);}
static void module_resume(void){unsigned f=fm1_doom_sound_lock();fm1_doom_music_pause(0);fm1_doom_sound_unlock(f);}
static void *module_register(void *data,int length){(void)data;return length==0?(void *)fm1_doom_music_score:0;}
static void module_unregister(void *handle){(void)handle;}
static void module_play(void *handle,boolean loop){unsigned f;if(handle!=(void *)fm1_doom_music_score)return;f=fm1_doom_sound_lock();fm1_doom_music_start(loop);fm1_doom_sound_unlock(f);}
static boolean module_playing(void){unsigned f=fm1_doom_sound_lock();int playing=fm1_doom_music_is_playing();fm1_doom_sound_unlock(f);return playing!=0;}
static void module_poll(void){}
static snddevice_t devices[]={SNDDEVICE_SB};
music_module_t fm1_music_module={devices,1,module_init,module_stop,module_volume,module_pause,module_resume,
                              module_register,module_unregister,module_play,module_stop,module_playing,module_poll};
#endif
