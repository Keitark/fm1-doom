import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from make_lowres_engine import install_target_integer_parsers, install_target_music, install_target_sfx_guard
from tests.test_lowres_render import c_function


class TargetEngineTests(unittest.TestCase):
    def test_generated_target_parsers_and_private_music_lifecycle(self):
        cmake = shutil.which("cmake")
        if not cmake:
            self.skipTest("CMake and a host compiler are required")
        upstream = ROOT / "vendor/doomgeneric/doomgeneric"
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name in ("m_misc.c", "m_config.c", "s_sound.c"):
                shutil.copyfile(upstream / name, directory / name)
            install_target_integer_parsers(directory)
            install_target_music(directory / "s_sound.c")
            install_target_sfx_guard(directory / "s_sound.c")
            misc = (directory / "m_misc.c").read_text(encoding="utf-8")
            config = (directory / "m_config.c").read_text(encoding="utf-8")
            music = (directory / "s_sound.c").read_text(encoding="utf-8")
            reference = (upstream / "m_misc.c").read_text(encoding="utf-8")
            source = r'''
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#define FM1_TARGET_PI32V2 1
#define true 1
#define false 0
typedef int boolean;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1); } } while (0)
'''
            source += "boolean " + c_function(reference, "M_StrToInt").replace("M_StrToInt", "reference_int")
            source += "\nboolean " + c_function(misc, "M_StrToInt")
            source += "\nstatic int " + c_function(config, "ParseIntParameter")
            source += r'''
enum { mus_None, mus_e1m1, mus_intro, mus_introa, NUMMUSIC };
enum { SNDDEVICE_ADLIB=3, SNDDEVICE_SB=4 };
typedef struct { char *name; int lumpnum; void *data; void *handle; } musicinfo_t;
static musicinfo_t S_music[NUMMUSIC], *mus_playing;
static int snd_musicdevice=SNDDEVICE_SB, mus_paused, played, stopped, registered, resumed, unregistered;
static void S_StopMusic(void);
static void I_Error(const char *s, int n) { (void)s; (void)n; abort(); }
static void *I_RegisterSong(void *data, int length) { CHECK(!data && !length); ++registered; return &registered; }
static void I_PlaySong(void *handle, int looping) { CHECK(handle==&registered && looping); ++played; }
static void I_StopSong(void) { ++stopped; }
static void I_UnRegisterSong(void *handle) { CHECK(handle==&registered); ++unregistered; }
static void I_ResumeSong(void) { ++resumed; mus_paused=0; }
'''
            source += "\nvoid " + c_function(music, "S_ChangeMusic")
            source += "\nstatic void " + c_function(music, "S_StopMusic")
            source += r'''
#define NUMSFX 3
#define NORM_SEP 128
typedef struct { int x,y; } mobj_t;
typedef struct sfxinfo_s { struct sfxinfo_s *link; int volume, usefulness, lumpnum; } sfxinfo_t;
static sfxinfo_t S_sfx[NUMSFX];
static struct { mobj_t *mo; } players[1];
static struct { int handle; } channels[1];
static int consoleplayer, snd_SfxVolume=80, sfx_stops, sfx_channels, sfx_starts, sfx_lookups, active_pistol;
static int S_AdjustSoundParams(mobj_t *a,mobj_t *b,int *v,int *s) { (void)a;(void)b;(void)v;*s=NORM_SEP;return 1; }
static void S_StopSound(mobj_t *origin) { (void)origin;++sfx_stops;active_pistol=0; }
static int S_GetChannel(mobj_t *origin,sfxinfo_t *sfx) { (void)origin;(void)sfx;++sfx_channels;return 0; }
static int I_GetSfxLumpNum(sfxinfo_t *sfx) { ++sfx_lookups;return sfx==&S_sfx[1]?0:-1; }
static int I_StartSound(sfxinfo_t *sfx,int channel,int volume,int sep) { CHECK(sfx==&S_sfx[1] && channel==0 && volume==80 && sep==NORM_SEP);++sfx_starts;active_pistol=1;return 42; }
'''
            source += "\nvoid " + c_function(music, "S_StartSound")
            source += r'''
int main(void) {
    static const char *cases[]={"", " ", "no", "0", "1", "-1", "+1", "077", "08", "09", "0129", "-077", "+077", "0x12", "0X12", "0xffffffff", "0x-1", "0x", "0xz", "+0x12", "-0x12", "0 12", "0 -12", "  -2147483648", "2147483647", "17tail"};
    unsigned i; int a,b,got,want;
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        a=b=12345;got=M_StrToInt(cases[i],&a);want=reference_int(cases[i],&b);
        CHECK(got==want && a==b);
    }
    CHECK(ParseIntParameter("077")==63 && ParseIntParameter("-077")==-63);
    CHECK(ParseIntParameter("0xffffffff")==-1 && ParseIntParameter("-2147483648")==(-2147483647-1));
    S_ChangeMusic(mus_intro,1); CHECK(!played && !registered);
    S_ChangeMusic(mus_e1m1,1); CHECK(played==1 && registered==1 && mus_playing==&S_music[mus_e1m1]);
    CHECK(mus_playing->lumpnum==-1 && !mus_playing->data);
    S_ChangeMusic(mus_e1m1,1); CHECK(played==1 && registered==1);
    mus_paused=1;S_ChangeMusic(mus_intro,1);
    CHECK(stopped==1 && unregistered==1 && resumed==1 && !mus_playing);
    S_ChangeMusic(mus_e1m1,1); CHECK(played==2 && registered==2);
    S_StopMusic(); S_StopMusic(); CHECK(stopped==2 && unregistered==2 && !mus_playing);
    {
        mobj_t player={0,0};players[0].mo=&player;S_sfx[1].lumpnum=S_sfx[2].lumpnum=-1;
        S_StartSound(&player,1); CHECK(active_pistol && sfx_stops==1 && sfx_channels==1 && sfx_starts==1 && sfx_lookups==1);
        S_StartSound(&player,2); S_StartSound(NULL,2);
        CHECK(active_pistol && sfx_stops==1 && sfx_channels==1 && sfx_starts==1 && sfx_lookups==3);
        S_StartSound(&player,1); CHECK(active_pistol && sfx_stops==2 && sfx_channels==2 && sfx_starts==2 && sfx_lookups==3);
    }
    puts("target integer parsers and private music lifecycle passed");return 0;
}
'''
            # No WAD lookup/cache/release stubs: any accidental target call must fail to link.
            (directory / "contract.c").write_text(source, encoding="utf-8")
            (directory / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.20)\nproject(target_engine C)\n"
                "add_executable(contract contract.c)\n"
                "target_compile_definitions(contract PRIVATE _CRT_SECURE_NO_WARNINGS)\n",
                encoding="utf-8")
            configure = [cmake, "-S", str(directory), "-B", str(directory / "build")]
            if os.name == "nt":
                configure += ["-G", "Visual Studio 17 2022", "-A", "x64"]
            for command in (configure, [cmake, "--build", str(directory / "build"), "--config", "Release"]):
                result = subprocess.run(command, capture_output=True, text=True, errors="replace")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            executable = directory / ("build/Release/contract.exe" if os.name == "nt" else "build/contract")
            result = subprocess.run([str(executable)], capture_output=True, text=True, errors="replace")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
