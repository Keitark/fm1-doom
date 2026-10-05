#include "fm1_doom_music.h"
#include <string.h>

enum {VOICE_COUNT=8, SAMPLE_RATE=44100, TICK_FRAMES=315, STACK_SIZE=16, MAX_EVENTS_PER_TICK=128};
typedef struct {
    uint32_t phase, increment, age;
    uint16_t envelope;
    uint8_t note, channel, velocity, program, released, held;
} voice;
typedef struct {uint8_t program, volume, pan, wheel, sustain, expression;} channel;
static struct {
    const uint8_t *bank, *nodes, *tokens;
    uint32_t raw_size, packed_size, position, raw_left, delay;
    uint32_t ticks, events, loops, steals, errors, noise;
    uint8_t stack[STACK_SIZE], stack_count, node_count, playing, paused, looping, master;
    uint16_t sample_count;
    voice voices[VOICE_COUNT];
    channel channels[16];
} music;

/* Equal-tempered MIDI note phase increments at 44.1 kHz. */
static const uint32_t increments[128]={
796254u,843601u,893765u,946911u,1003217u,1062871u,1126073u,1193033u,1263974u,1339134u,1418763u,1503127u,1592507u,1687203u,1787529u,1893821u,2006434u,2125742u,2252146u,2386065u,2527948u,2678268u,2837526u,3006254u,3185015u,3374406u,3575058u,3787642u,4012867u,4251485u,4504291u,4772130u,5055896u,5356535u,5675051u,6012507u,6370030u,6748811u,7150117u,7575285u,8025735u,8502970u,9008582u,9544261u,10111792u,10713070u,11350103u,12025015u,12740059u,13497623u,14300233u,15150569u,16051469u,17005939u,18017165u,19088521u,20223584u,21426141u,22700205u,24050030u,25480119u,26995246u,28600467u,30301139u,32102938u,34011878u,36034330u,38177043u,40447168u,42852281u,45400411u,48100060u,50960238u,53990491u,57200933u,60602278u,64205876u,68023757u,72068660u,76354085u,80894335u,85704563u,90800821u,96200119u,101920476u,107980983u,114401866u,121204555u,128411753u,136047513u,144137319u,152708170u,161788671u,171409126u,181601643u,192400238u,203840952u,215961966u,228803732u,242409110u,256823506u,272095026u,288274639u,305416341u,323577341u,342818251u,363203285u,384800477u,407681904u,431923931u,457607465u,484818220u,513647012u,544190053u,576549277u,610832681u,647154683u,685636503u,726406571u,769600953u,815363807u,863847862u,915214929u,969636441u,1027294024u,1088380105u,1153098554u,1221665363u};

static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void fail(void){music.playing=0;++music.errors;memset(music.voices,0,sizeof(music.voices));}

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
    unsigned i;
    music.position=0;music.raw_left=music.raw_size;music.stack_count=0;music.sample_count=0;
    memset(music.voices,0,sizeof(music.voices));
    for(i=0;i<16;i++){
        music.channels[i]=(channel){0,100,64,128,0,127};
    }
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
    music.master=80;music.noise=1;
    music.ticks=music.events=music.loops=music.steals=music.errors=0;
    return 0;
}

int fm1_doom_music_start(int loop)
{
    if(!music.bank)return -1;
    music.playing=1;music.paused=0;music.looping=loop!=0;reset_stream();
    return music.playing?0:-1;
}
void fm1_doom_music_stop_locked(void){music.playing=0;music.paused=0;memset(music.voices,0,sizeof(music.voices));}
void fm1_doom_music_pause(int paused){music.paused=paused!=0;}
void fm1_doom_music_set_volume(unsigned volume){music.master=(uint8_t)(volume>127?127:volume);}
int fm1_doom_music_is_playing(void){return music.playing;}

static void pitch(voice *v)
{
    uint32_t base=increments[v->note];
    int change=(int)music.channels[v->channel].wheel-128;
    v->increment=base+(int32_t)(base>>10)*change; /* approximately +/-2 semitones */
}

static void note_on(unsigned ch,unsigned note,unsigned velocity)
{
    unsigned i,chosen=0;uint32_t best=0xffffffffu;
    for(i=0;i<VOICE_COUNT;i++){
        voice *v=&music.voices[i];
        uint32_t priority=v->envelope;
        if(!v->envelope){chosen=i;best=0;break;}
        if(v->released || v->channel==15)priority>>=2;
        if(priority<best){best=priority;chosen=i;}
    }
    if(best)++music.steals;
    music.voices[chosen]=(voice){0,0,0,1,(uint8_t)note,(uint8_t)ch,(uint8_t)velocity,
                              music.channels[ch].program,0,0};
    pitch(&music.voices[chosen]);
}

static void note_off(unsigned ch,unsigned note)
{
    unsigned i;
    for(i=0;i<VOICE_COUNT;i++)if(music.voices[i].channel==ch && music.voices[i].note==note){
        music.voices[i].held=music.channels[ch].sustain;
        music.voices[i].released=!music.channels[ch].sustain;
    }
}

static int event(void)
{
    uint8_t descriptor,a=0,b=0;unsigned kind,ch,i;
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
    if(kind==0)note_off(ch,a);
    else if(kind==1){if(b)note_on(ch,a,b);else note_off(ch,a);}
    else if(kind==2){music.channels[ch].wheel=a;for(i=0;i<VOICE_COUNT;i++)if(music.voices[i].channel==ch)pitch(&music.voices[i]);}
    else if(kind==3){
        if(a==10 || a==11 || a==13 || a==14)for(i=0;i<VOICE_COUNT;i++)if(music.voices[i].channel==ch){
            if(a==10)music.voices[i].envelope=0;else music.voices[i].released=1;
        }
        if(a==14)music.channels[ch]=(channel){0,100,64,128,0,127};
    }else {
        if(a>9 || b>127)return 0;
        if(a==0)music.channels[ch].program=b;
        else if(a==3)music.channels[ch].volume=b;
        else if(a==4)music.channels[ch].pan=b;
        else if(a==5)music.channels[ch].expression=b;
        else if(a==8){
            music.channels[ch].sustain=b>=64;
            if(b<64)for(i=0;i<VOICE_COUNT;i++)if(music.voices[i].channel==ch && music.voices[i].held){music.voices[i].held=0;music.voices[i].released=1;}
        }
    }
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

static int triangle(uint32_t phase)
{
    unsigned p=phase>>20;
    return p<2048u?(int)p-1024:3071-(int)p;
}
static int16_t clip(int value){return (int16_t)(value>8191?8191:value<-8192?-8192:value);}

void fm1_doom_music_sample_stereo(int16_t *left,int16_t *right)
{
    int l=0,r=0;unsigned i;
    if(!left || !right)return;
    *left=*right=0;
    if(!music.playing || music.paused)return;
    if(!music.sample_count){tick();music.sample_count=TICK_FRAMES;}
    --music.sample_count;
    if(!music.playing)return;
    music.noise^=music.noise<<13;music.noise^=music.noise>>17;music.noise^=music.noise<<5;
    for(i=0;i<VOICE_COUNT;i++){
        voice *v=&music.voices[i];channel *c;int wave,sample;unsigned sustain;
        if(!v->envelope)continue;
        c=&music.channels[v->channel];++v->age;v->phase+=v->increment;
        if(v->channel==15){
            unsigned lifetime=(v->note==42 || v->note==44)?1323u:4410u;
            if(v->age>lifetime)v->released=1;
            wave=(int)(music.noise&2047u)-1024;
            if(v->note==35 || v->note==36)wave=triangle(v->age*7791324u);
        }else {
            wave=triangle(v->phase);
            if(v->program==29 || v->program==30){
                wave+=triangle(v->phase*2u)/2;wave*=2;
                if(wave>1023)wave=1023;if(wave<-1024)wave=-1024;
            }else if(v->program<8)wave+=triangle(v->phase*2u)/4;
        }
        if(v->released){
            unsigned decay=(v->envelope>>10)+1u;
            v->envelope=(uint16_t)(v->envelope>decay?v->envelope-decay:0);
        }else if(v->age<=180u){
            unsigned envelope=v->envelope+364u;v->envelope=(uint16_t)(envelope>65535u?65535u:envelope);
        }else {
            sustain=v->channel==15?0u:(v->program<8?8192u:24576u);
            if(v->envelope>sustain)v->envelope=(uint16_t)(v->envelope-((v->envelope-sustain)>>12)-1u);
        }
        sample=(wave*(int)v->envelope)>>16;
        sample=sample*(int)v->velocity/127;
        sample=sample*(int)c->volume/127;
        sample=sample*(int)c->expression/127;
        l+=sample*(127-(int)c->pan)/127;r+=sample*(int)c->pan/127;
    }
    /* Headroom for gunfire remains after bringing the quiet oscillator mix
     * to a useful PCM16 level; each channel is capped at one quarter scale. */
    *left=clip(l*(int)music.master*8/127);*right=clip(r*(int)music.master*8/127);
}
int32_t fm1_doom_music_sample(void){int16_t l,r;fm1_doom_music_sample_stereo(&l,&r);return ((int32_t)l+r)/2;}
void fm1_doom_music_get_diagnostics(fm1_doom_music_diagnostics *d)
{
    unsigned i;if(!d)return;
    *d=(fm1_doom_music_diagnostics){music.ticks,music.events,music.loops,music.steals,music.errors,0};
    for(i=0;i<VOICE_COUNT;i++)d->active_voices+=music.voices[i].envelope!=0;
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
