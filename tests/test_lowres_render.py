import re
import sys
import tempfile
import unittest
import ctypes
import os
import shutil
import struct
import subprocess
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from make_lowres_engine import bound_texture_columns, scale_weapon_sprites


class LowresWeaponTests(unittest.TestCase):
    def generated_scales(self):
        upstream = (Path(__file__).resolve().parents[1]
                    / "vendor/doomgeneric/doomgeneric/r_main.c")
        with tempfile.TemporaryDirectory() as temporary:
            generated = Path(temporary) / "r_main.c"
            generated.write_text(upstream.read_text(encoding="utf-8"), encoding="utf-8")
            scale_weapon_sprites(generated)
            source = generated.read_text(encoding="utf-8")
        denominator = int(re.search(r"pspritescale = FRACUNIT\*viewwidth/(\d+);", source)[1])
        numerator = int(re.search(r"pspriteiscale = FRACUNIT\*(\d+)/viewwidth;", source)[1])
        return denominator, numerator, source

    def test_weapon_coordinates_keep_original_reference_width(self):
        denominator, numerator, source = self.generated_scales()
        self.assertEqual((denominator, numerator), (320, 320))
        self.assertIn("viewheight = SCREENHEIGHT;", source)
        self.assertIn("scaledviewwidth = SCREENWIDTH;", source)

    def test_full_and_low_detail_scales_are_reciprocal(self):
        denominator, numerator, _ = self.generated_scales()
        unit = 1 << 16
        for viewwidth, detail in ((160, 0), (80, 1)):
            scale = unit * viewwidth // denominator
            inverse = unit * numerator // viewwidth
            self.assertEqual(scale * inverse, unit * unit)
            self.assertEqual(scale << detail, unit // 2)

    def test_weapon_projection_is_half_of_original_screen(self):
        denominator, _, _ = self.generated_scales()
        scale = 160 / denominator
        # Ready weapon coordinates, offsets, and sprite widths stay in the
        # original art space; project them into the smaller framebuffer.
        for horizontal, texturemid in ((-50, -50), (-20, -90), (30, -70)):
            original_x = 160 + horizontal
            original_y = 100 - texturemid
            self.assertEqual(80 + horizontal * scale, original_x / 2)
            self.assertEqual(50 - texturemid * scale, original_y / 2)


def c_function(source, name):
    match = re.search(r"\b" + name + r"\s*\([^;]*?\)\s*\{", source)
    if not match:
        raise AssertionError(f"Missing C function {name}")
    depth = 1
    end = match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


class LowresColumnTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cmake = shutil.which("cmake")
        if not cmake:
            raise unittest.SkipTest("CMake and a host C compiler are required")
        cls.temporary = tempfile.TemporaryDirectory()
        root = Path(cls.temporary.name)
        upstream = (Path(__file__).resolve().parents[1]
                    / "vendor/doomgeneric/doomgeneric/r_data.c").read_text(encoding="utf-8")
        generated = root / "r_data.c"
        generated.write_text(upstream, encoding="utf-8")
        bound_texture_columns(generated)
        modified = generated.read_text(encoding="utf-8")
        cls.modified = modified
        harness = r'''
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
#define SHORT(x) (x)
#define LONG(x) (x)
#define PU_STATIC 1
#define PU_CACHE 8
#define Z_ChangeTag(p,t) ((void)0)
typedef unsigned char byte;
typedef struct { byte topdelta, length, unused; } column_t;
typedef struct { short width,height,leftoffset,topoffset; int columnofs[1]; } patch_t;
typedef struct { short originx,originy; int patch; } texpatch_t;
typedef struct texture_s { char name[8]; short width,height; int index;
    struct texture_s *next; short patchcount; texpatch_t patches[1]; } texture_t;
static texture_t *textures[1024];
static short *texturecolumnlump[1024];
static unsigned short *texturecolumnofs[1024];
static byte *texturecomposite[1024], *patchdata[1024];
static int texturecompositesize[1024], texturewidthmask[1024];
static int current, error, reads;
static void I_Error(const char *message, ...) { (void)message; error = 1; }
static void *Z_Malloc(int size,int tag,void *user) {
    void *p=calloc(1, size ? size : 1); (void)tag;
    if(user) *(void **)user=p; return p;
}
static void Z_Free(void *p) { free(p); }
static patch_t *W_CacheLumpNum(int lump,int tag) {
    (void)tag; ++reads; return (patch_t *)patchdata[lump];
}
'''
        harness += "\nvoid\n" + c_function(upstream, "R_DrawColumnInCache")
        harness += "\nvoid\n" + c_function(upstream, "R_GenerateLookup")
        harness += "\nvoid\n" + c_function(upstream, "R_GenerateComposite")
        harness += "\nstatic byte *\n" + c_function(modified, "FM1_CompositeColumn")
        harness += "\nbyte *\n" + c_function(modified, "R_GetColumn")
        harness += r'''
API void configure(int width,int height,int count,int *xs,int *ys,byte **data) {
    int i, power=1; texture_t *t;
    if (++current>=1024 || count>1023) abort();
    t=calloc(1,sizeof(*t)+(count-1)*sizeof(texpatch_t));
    t->width=width; t->height=height; t->patchcount=count; textures[current]=t;
    for(i=0;i<count;++i) {
        t->patches[i].originx=xs[i]; t->patches[i].originy=ys[i];
        t->patches[i].patch=i+1; patchdata[i+1]=data[i];
    }
    while(power*2<=width) power*=2;
    texturewidthmask[current]=power-1;
    texturecolumnlump[current]=calloc(width,sizeof(short));
    texturecolumnofs[current]=calloc(width,sizeof(unsigned short));
    error=0; R_GenerateLookup(current);
}
API byte *actual(int column) { return R_GetColumn(current,column); }
API byte *reference(int column) {
    int lump=texturecolumnlump[current][column];
    int ofs=texturecolumnofs[current][column];
    if(lump>0) return patchdata[lump]+ofs;
    if(!texturecomposite[current]) R_GenerateComposite(current);
    return texturecomposite[current]+ofs;
}
API int composite(int column) { return texturecolumnlump[current][column]<0; }
API int failed(void) { return error; }
API int read_count(void) { return reads; }
'''
        (root / "columns.c").write_text(harness, encoding="utf-8")
        (root / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\nproject(columns C)\n"
            "add_library(columns SHARED columns.c)\n", encoding="utf-8")
        configure = [cmake, "-S", str(root), "-B", str(root / "build")]
        if os.name == "nt":
            configure.extend(["-G", "Visual Studio 17 2022", "-A", "x64"])
        for command in (configure, [cmake, "--build", str(root / "build"), "--config", "Release"]):
            result = subprocess.run(command, capture_output=True, text=True,
                                    encoding="utf-8", errors="replace")
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
        suffix = "columns.dll" if os.name == "nt" else "libcolumns.so"
        cls.library = ctypes.CDLL(str(next((root / "build").rglob(suffix))))
        cls.library.configure.argtypes = [ctypes.c_int] * 3 + [ctypes.POINTER(ctypes.c_int)] * 2 + [ctypes.POINTER(ctypes.c_void_p)]
        for name in ("actual", "reference"):
            getattr(cls.library, name).argtypes = [ctypes.c_int]
            getattr(cls.library, name).restype = ctypes.c_void_p

    @classmethod
    def tearDownClass(cls):
        if os.name == "nt":
            ctypes.windll.kernel32.FreeLibrary(ctypes.c_void_p(cls.library._handle))
        cls.temporary.cleanup()

    def configure(self, width, height, patches):
        self.buffers = [ctypes.create_string_buffer(data) for _, _, data in patches]
        count = len(patches)
        xs = (ctypes.c_int * count)(*[x for x, _, _ in patches])
        ys = (ctypes.c_int * count)(*[y for _, y, _ in patches])
        data = (ctypes.c_void_p * count)(*[ctypes.addressof(b) for b in self.buffers])
        self.library.configure(width, height, count, xs, ys, data)

    @staticmethod
    def patch(columns):
        header = bytearray(struct.pack("<hhhh", len(columns), 128, 0, 0) + bytes(4 * len(columns)))
        for index, posts in enumerate(columns):
            struct.pack_into("<I", header, 8 + index * 4, len(header))
            for top, pixels in posts:
                header.extend(bytes((top, len(pixels), 0)) + bytes(pixels) + b"\0")
            header.append(255)
        return bytes(header)

    def assert_column(self, column, height):
        actual = self.library.actual(column)
        self.assertFalse(self.library.failed())
        reference = self.library.reference(column)
        self.assertEqual(ctypes.string_at(actual, height), ctypes.string_at(reference, height))
        if self.library.composite(column):
            self.assertEqual(ctypes.string_at(actual - 3, 3), bytes((0, height, 0)))
            self.assertEqual(ctypes.string_at(actual + height, 2), b"\0\xff")

    def test_overlapping_posts_and_both_vertical_clip_edges(self):
        base = self.patch([[(0, range(1, 13))], [(0, range(21, 33))]])
        overlay = self.patch([[(0, (91, 92, 93, 94)), (7, (95, 96, 97, 98))]])
        for origin_y in (-2, 0, 5, 20):
            self.configure(2, 12, [(0, 0, base), (1, origin_y, overlay)])
            self.assert_column(0, 12)
            self.assert_column(1, 12)

    def test_height_guard_and_memoized_immutable_column(self):
        patch = self.patch([[(0, (11, 12, 13, 14))]])
        self.configure(1, 128, [(0, 0, patch), (0, 0, patch)])
        self.assert_column(0, 128)
        before = self.library.read_count()
        self.library.actual(0)
        self.assertEqual(self.library.read_count(), before)
        for height in (0, 129):
            self.configure(1, height, [(0, 0, patch), (0, 0, patch)])
            self.library.actual(0)
            self.assertTrue(self.library.failed())

    def test_every_staged_composite_column_matches_stock_c(self):
        root = Path(__file__).resolve().parents[1]
        stages = [stage for stage in (root / "build/stage-menu-ui.wad",
                  root / "build/fine-assets/stage-menu-p4.wad") if stage.exists()]
        if not stages:
            self.skipTest("Local user-supplied staged IWAD is absent")
        for stage in stages:
            with self.subTest(stage=stage.name):
                self.assert_wad_columns(stage)

    def assert_wad_columns(self, stage):
        from stage_wad import read_wad
        lumps = {lump.name.upper(): lump.data for lump in read_wad(stage)}
        names = lumps[b"PNAMES"]
        patchnames = [names[4+i*8:12+i*8].split(b"\0", 1)[0]
                      for i in range(struct.unpack_from("<I", names)[0])]
        textures = lumps[b"TEXTURE1"]
        compared = 0
        for index in range(struct.unpack_from("<I", textures)[0]):
            offset = struct.unpack_from("<I", textures, 4+index*4)[0]
            width, height, count = struct.unpack_from("<HHxxxxH", textures, offset+12)
            self.assertLessEqual(height, 128)
            patches = []
            for p in range(count):
                x, y, name = struct.unpack_from("<hhH", textures, offset+22+p*10)
                patches.append((x, y, lumps[patchnames[name].upper()]))
            self.configure(width, height, patches)
            for column in range(width):
                if self.library.composite(column):
                    self.assert_column(column, height)
                    compared += 1
        self.assertGreater(compared, 1000)


if __name__ == "__main__":
    unittest.main()
