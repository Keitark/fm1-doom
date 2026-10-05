"""Prepare a private independent stock Doom OPL2/core reference host fixture.

Only ignored build/ output is written. The unchanged pinned event handlers and
emulator are compiled independently; reference events are read directly from
the original MUS bytes rather than the port's compressed score decoder.
"""
import argparse
import hashlib
from pathlib import Path
import re
from make_genmidi_bank import wad_genmidi
from make_music_score import wad_music

HASHES = {
    "src/i_oplmusic.c": "b3232c2b71016a1d596e2c666b6c7388dfef1a76d56cae4fd00ccc99d129276a",
    "opl/emu8950.c": "266e4fdee00c4f72c8b7839aa3398eab039a7082717969e5f7aba1ac4f317474",
    "opl/opl_api.c": "4586d9860bd8c4d7d2cebea44065eb4dc3c89d5ff0731c4de79f6c8efa6fe542",
}


def array(name, raw):
    rows = [",".join(str(x) for x in raw[i:i + 24]) for i in range(0, len(raw), 24)]
    return "static const unsigned char " + name + "[]={\n" + ",\n".join(rows) + "\n};\n"


def function(source, name):
    start = source.index("void " + name + "(")
    pos = source.index("{", start)
    depth = 1
    end = pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iwad", type=Path)
    parser.add_argument("upstream", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    for name, digest in HASHES.items():
        if hashlib.sha256((args.upstream / name).read_bytes()).hexdigest() != digest:
            raise ValueError("reference upstream source changed: " + name)
    original = (args.upstream / "src/i_oplmusic.c").read_text(encoding="utf-8")
    body = original[original.index("#define MAXMIDLENGTH"):original.index("static void MetaSetTempo")]
    init = original[original.index("static void InitChannel(opl_channel_data_t *channel)\n{"):
                    original.index("// Start a MIDI track playing:")]
    registers = function((args.upstream / "opl/opl_api.c").read_text(encoding="utf-8"), "OPL_InitRegisters")
    prefix = '''/* Generated private reference: original driver functions below are unchanged. */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t byte;
typedef bool boolean;
typedef int16_t isb_int16_t;
typedef const char *constcharstar;
#define should_be_const
#ifdef _MSC_VER
#define PACKED_STRUCT(...) __pragma(pack(push,1)) struct __VA_ARGS__ __pragma(pack(pop))
#else
#define PACKED_STRUCT(...) struct __VA_ARGS__ __attribute__((packed))
#endif
#define SHORT(x) ((int16_t)(x))
#define DEH_String(x) (x)
#define PU_STATIC 1
#define DOOM_TINY 1
#define DOOM_SMALL 1
void *W_CacheLumpName(const char *name,int tag);
typedef enum {opl_doom1_1_666,opl_doom2_1_666,opl_doom_1_9} opl_driver_ver_t;
'''
    prefix += '#include "' + (args.upstream / "opl/opl.h").resolve().as_posix() + '"\n'
    prefix += '#include "' + (args.upstream / "src/midifile.h").resolve().as_posix() + '"\n'
    source = args.iwad.read_bytes()
    private = array("original_genmidi", wad_genmidi(source)) + array("original_mus", wad_music(source))
    core = '''
/* Independently compile the untouched original portable core and its512B
 * key-scale-rate table. No optimized FM-1 source/header is included here. */
#define USE_EMU8950_OPL 1
#define EMU8950_NO_RATECONV 1
#define EMU8950_NO_FLOAT 1
#define EMU8950_NO_TLL 1
#define EMU8950_NO_TIMER 1
#define EMU8950_NO_WAVE_TABLE_MAP 1
#define EMU8950_NO_TEST_FLAG 1
#define OPL_new reference_OPL_new
#define OPL_delete reference_OPL_delete
#define OPL_reset reference_OPL_reset
#define OPL_setRate reference_OPL_setRate
#define OPL_setQuality reference_OPL_setQuality
#define OPL_setPan reference_OPL_setPan
#define OPL_calc reference_OPL_calc
#define OPL_calc_buffer reference_OPL_calc_buffer
#define OPL_calc_buffer_stereo reference_OPL_calc_buffer_stereo
#define OPL_writeReg reference_OPL_writeReg
#define OPL_setMask reference_OPL_setMask
'''
    core += '#include "' + (args.upstream / "opl/emu8950.c").resolve().as_posix() + '"\n'
    suffix = '''
extern void opl_reference_trace(unsigned reg,unsigned value);
static OPL *reference_chip;
static unsigned mus_pos,mus_end,mus_tick,mus_done;
static uint8_t note_velocity[16];
static uint64_t rendered_frames;
static unsigned native_samples;
static int16_t ref_previous,ref_current;
void OPL_WriteRegister(int reg,int value)
{
    opl_reference_trace((unsigned)reg,(unsigned)value);
    reference_OPL_writeReg(reference_chip,(unsigned)reg,(uint8_t)value);
}
void opl_reference_init(unsigned volume)
{
    unsigned i;
    if(reference_chip)reference_OPL_delete(reference_chip);
    reference_chip=reference_OPL_new(3579545,49716);
    main_instrs=(genmidi_instr_t *)(original_genmidi+8);
    percussion_instrs=main_instrs+128;
    current_music_volume=start_music_volume=volume;
    memset(voices,0,sizeof(voices));InitVoices();OPL_InitRegisters(0);
    for(i=0;i<16;i++)InitChannel(&channels[i]);
    mus_pos=(unsigned)original_mus[6]|(unsigned)original_mus[7]<<8;
    mus_end=mus_pos+((unsigned)original_mus[4]|(unsigned)original_mus[5]<<8);
    mus_tick=mus_done=0;memset(note_velocity,100,sizeof(note_velocity));
    rendered_frames=native_samples=0;ref_previous=ref_current=0;
}
void opl_reference_volume(unsigned v){I_OPL_SetMusicVolume(v);}
void opl_reference_events(unsigned tick)
{
    static const uint8_t map[]={0x00,0x20,0x01,0x07,0x0a,0x0b,0x5b,0x5d,0x40,0x43,0x78,0x7b,0x7e,0x7f,0x79};
    while(!mus_done && mus_tick<=tick && mus_pos<mus_end){
        unsigned descriptor=original_mus[mus_pos++],kind=(descriptor>>4)&7,ch=descriptor&15;
        unsigned a,b,i; midi_event_t e={0};
        e.data.channel.channel=ch==15?9:ch==9?15:ch;
        if(kind==6){for(i=0;i<16;i++)AllNotesOff(&channels[i],0);mus_done=1;break;}
        a=original_mus[mus_pos++];e.data.channel.param1=a;
        switch(kind){
        case 0:e.data.channel.param1=a&127;KeyOffEvent(NULL,&e);break;
        case 1:
            if(a&128)note_velocity[ch]=original_mus[mus_pos++];
            e.data.channel.param1=a&127;e.data.channel.param2=note_velocity[ch];KeyOnEvent(NULL,&e);break;
        case 2:e.data.channel.param1=(a*64)&127;e.data.channel.param2=(a*64)>>7;PitchBendEvent(NULL,&e);break;
        case 3:e.data.channel.param1=map[a];ControllerEvent(NULL,&e);break;
        case 4:
            b=original_mus[mus_pos++];
            if(!a){e.data.channel.param1=b;ProgramChangeEvent(NULL,&e);}
            else{e.data.channel.param1=map[a];e.data.channel.param2=b;ControllerEvent(NULL,&e);}break;
        default:abort();
        }
        if(descriptor&128){
            unsigned delay=0;
            do{b=original_mus[mus_pos++];delay=(delay<<7)|(b&127);}while(b&128);
            mus_tick+=delay;
        }
    }
}
int16_t opl_reference_sample(void)
{
    /* Independent rational-time calculation, not the port's phase accumulator. */
    uint64_t time=rendered_frames++*49716u;
    unsigned needed=(unsigned)(time/44100u),fraction=(unsigned)(time%44100u);
    while(native_samples<needed){ref_previous=ref_current;reference_OPL_calc_buffer(reference_chip,&ref_current,1);++native_samples;}
    return (int16_t)((int32_t)ref_previous+((int64_t)ref_current-ref_previous)*fraction/44100);
}
unsigned opl_reference_active(void){return (unsigned)voice_alloced_num;}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(prefix + body + init + registers + private + core + suffix, encoding="utf-8")
    print(args.output)


if __name__ == "__main__":
    main()
