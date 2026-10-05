"""Create a disposable 160x100 Doomgeneric source variant under build/.

The pinned upstream submodule is never edited. This direct-E1M1 experiment
uses a compact HUD and original menu patches drawn at half size.
"""
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "vendor/doomgeneric/doomgeneric"
TARGET = ROOT / "build/lowres-source"
PIN = "dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284"

PRINTF_INTEGER = re.compile(
    r"%%|%(?:[1-9][0-9]*\$)?[-+ #0']*"
    r"(?:\*(?:[1-9][0-9]*\$)?|[0-9]+)?"
    r"(?:\.(?:\*(?:[1-9][0-9]*\$)?|[0-9]*))?"
    r"(?:hh|ll|I32|I64|[hljztL])?i")
C_TOKEN = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'
    r"'(?:\\.|[^'\\])*'|[A-Za-z_][A-Za-z_0-9]*|.", re.S)


def normalize_printf_formats(source: str) -> str:
    output = []
    calls = []
    previous = ""
    for match in C_TOKEN.finditer(source):
        token = match.group(0)
        if token.isspace() or token.startswith(("//", "/*")):
            output.append(token)
            continue
        if token == "(":
            calls.append(previous if re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", previous) else "")
        elif token == ")" and calls:
            calls.pop()
        elif token.startswith('"'):
            call = next((name for name in reversed(calls) if name), "")
            if not call.endswith("scanf"):
                token = PRINTF_INTEGER.sub(
                    lambda value: value.group(0) if value.group(0) == "%%"
                    else value.group(0)[:-1] + "d", token)
        output.append(token)
        previous = token
    return "".join(output)


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise ValueError(f"unexpected upstream source: {path.name}")
    path.write_text(text.replace(old, new), encoding="utf-8")


def replace_function(path: Path, signature: str, replacement: str) -> None:
    source = path.read_text(encoding="utf-8")
    if source.count(signature) != 1:
        raise ValueError(f"unexpected function layout: {path.name}: {signature}")
    start = source.index(signature)
    body = source.index("{", start + len(signature) - 1)
    depth = 0
    for end in range(body, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                path.write_text(source[:start] + replacement + source[end + 1:], encoding="utf-8")
                return
    raise ValueError(f"unterminated function: {path.name}: {signature}")


def replace_count(path: Path, old: str, new: str, count: int) -> None:
    source = path.read_text(encoding="utf-8")
    if source.count(old) != count:
        raise ValueError(f"unexpected declaration count: {path.name}: {old}")
    path.write_text(source.replace(old, new), encoding="utf-8")


def scale_weapon_sprites(path: Path) -> None:
    # Psprite coordinates and weapon patch dimensions remain in Doom's
    # original 320x200 space after the framebuffer becomes 160x100.
    replace_once(path, "    pspritescale = FRACUNIT*viewwidth/SCREENWIDTH;",
                 "    pspritescale = FRACUNIT*viewwidth/320;")
    replace_once(path, "    pspriteiscale = FRACUNIT*SCREENWIDTH/viewwidth;",
                 "    pspriteiscale = FRACUNIT*320/viewwidth;")


COMPOSITE_COLUMN_C = '''/* Each renderer caller consumes the returned column before requesting
 * another. Compose one immutable texture column instead of allocating a
 * 16/32 KiB texture cache that can fail in a fragmented Doom zone. */
static byte *FM1_CompositeColumn(int texnum, int col)
{
    static byte column[135];
    static int last_texture = -1, last_column = -1;
    texture_t *texture = textures[texnum];
    texpatch_t *patch;
    patch_t *realpatch;
    column_t *patchcol;
    int i, source_x;

    if (texture->height < 1 || texture->height > 128)
    {
        I_Error("FM-1 composite texture height exceeds 128: %d", texture->height);
        return column + 3;
    }
    if (last_texture == texnum && last_column == col)
        return column + 3;

    memset(column, 0, sizeof(column));
    column[1] = texture->height;
    column[texture->height + 4] = 0xff;
    for (i = 0, patch = texture->patches; i < texture->patchcount; ++i, ++patch)
    {
        realpatch = W_CacheLumpNum(patch->patch, PU_CACHE);
        source_x = col - patch->originx;
        if (source_x < 0 || source_x >= SHORT(realpatch->width))
            continue;
        patchcol = (column_t *)((byte *)realpatch
                               + LONG(realpatch->columnofs[source_x]));
        R_DrawColumnInCache(patchcol, column + 3, patch->originy, texture->height);
    }
    last_texture = texnum;
    last_column = col;
    return column + 3;
}


'''


def bound_texture_columns(path: Path) -> None:
    replace_once(path, "//\n// R_GetColumn\n//", COMPOSITE_COLUMN_C + "//\n// R_GetColumn\n//")
    replace_once(path, "    if (!texturecomposite[tex])\n\tR_GenerateComposite (tex);\n\n"
                 "    return texturecomposite[tex] + ofs;",
                 "    return FM1_CompositeColumn(tex, col);")


def main() -> None:
    head = subprocess.check_output(["git", "-C", str(SOURCE), "rev-parse", "HEAD"], text=True).strip()
    if head != PIN:
        raise ValueError(f"unreviewed Doomgeneric revision: {head}")
    if subprocess.check_output(["git", "-C", str(SOURCE), "status", "--porcelain"], text=True).strip():
        raise ValueError("Doomgeneric submodule has local changes")
    shutil.copytree(SOURCE, TARGET, dirs_exist_ok=True)
    replace_once(TARGET / "i_video.h", "#define SCREENWIDTH  320", "#define SCREENWIDTH  160")
    replace_once(TARGET / "i_video.h", "#define SCREENHEIGHT 200", "#define SCREENHEIGHT 100")
    replace_once(TARGET / "r_draw.c", "#define MAXWIDTH\t\t\t1120",
                 "#define MAXWIDTH\t\t\tSCREENWIDTH")
    replace_once(TARGET / "r_draw.c", "#define MAXHEIGHT\t\t\t832",
                 "#define MAXHEIGHT\t\t\tSCREENHEIGHT")
    wad = TARGET / "w_wad.c"
    replace_once(wad, "#include <ctype.h>", "#include <ctype.h>\n#include <limits.h>")
    replace_once(wad, "    newlumpinfo = calloc(newnumlumps, sizeof(lumpinfo_t));",
                 "    if (newnumlumps < 0\n"
                 "        || (size_t)newnumlumps > INT_MAX / sizeof(lumpinfo_t))\n"
                 "    {\n        I_Error(\"Invalid lumpinfo count\");\n        return;\n    }\n\n"
                 "    newlumpinfo = NULL;\n    if (newnumlumps != 0)\n    {\n"
                 "        newlumpinfo = Z_Malloc(newnumlumps * sizeof(lumpinfo_t),\n"
                 "                               PU_STATIC, NULL);\n    }")
    replace_once(wad, "    if (newlumpinfo == NULL)",
                 "    if (newnumlumps != 0 && newlumpinfo == NULL)")
    replace_once(wad, "\tI_Error (\"Couldn't realloc lumpinfo\");\n    }",
                 "\tI_Error (\"Couldn't realloc lumpinfo\");\n        return;\n    }\n"
                 "    if (newnumlumps != 0)\n"
                 "        memset(newlumpinfo, 0, newnumlumps * sizeof(lumpinfo_t));")
    replace_once(wad, "    free(lumpinfo);", "    if (lumpinfo != NULL) Z_Free(lumpinfo);")
    display = TARGET / "d_main.c"
    replace_once(display, "    int\t\t\t\tnowtime;\n    int\t\t\t\ttics;\n"
                 "    int\t\t\t\twipestart;\n", "")
    replace_once(display, "    boolean\t\t\tdone;\n    boolean\t\t\twipe;\n", "")
    source = display.read_text(encoding="utf-8")
    start = source.index("    // save the current screen if about to wipe")
    end = source.index("    if (gamestate == GS_LEVEL && gametic)", start)
    source = source[:start] + source[end:]
    start = source.index("    // normal update")
    end = source.index("\n}\n", start)
    source = source[:start] + "    I_FinishUpdate ();\n" + source[end:]
    display.write_text(source, encoding="utf-8")
    replace_once(display, "if (wipe || (viewheight != 200 && fullscreen) )",
                 "if (viewheight != SCREENHEIGHT && fullscreen)")
    replace_count(display, "viewheight == 200", "viewheight == SCREENHEIGHT", 2)
    replace_once(display, "scaledviewwidth != 320", "scaledviewwidth != SCREENWIDTH")
    system = TARGET / "i_system.c"
    replace_once(system, "void I_AtExit(atexit_func_t func, boolean run_on_error)\n{\n",
                 "void I_AtExit(atexit_func_t func, boolean run_on_error)\n{\n"
                 "#ifdef FM1_TARGET_PI32V2\n    (void)func;\n    (void)run_on_error;\n#else\n")
    replace_once(system, "    exit_funcs = entry;\n}", "    exit_funcs = entry;\n#endif\n}")
    replace_once(system, "void I_Error (char *error, ...)\n{\n",
                 "#ifdef FM1_TARGET_PI32V2\n"
                 "extern void fm1_doom_target_fatal(const char *format, va_list args);\n\n"
                 "void I_Error (char *error, ...)\n{\n    va_list args;\n"
                 "    va_start(args, error);\n    fm1_doom_target_fatal(error, args);\n"
                 "    va_end(args);\n}\n#else\nvoid I_Error (char *error, ...)\n{\n")
    replace_once(system, "//\n// Read Access Violation emulation.",
                 "#endif\n\n//\n// Read Access Violation emulation.")
    replace_once(TARGET / "r_main.c", "    setblocks = blocks;",
                 "    setblocks = 11; /* low-resolution proof: force full view */")
    scale_weapon_sprites(TARGET / "r_main.c")
    bound_texture_columns(TARGET / "r_data.c")
    status = TARGET / "st_stuff.c"
    replace_function(status, "void ST_Drawer (boolean fullscreen, boolean refresh)",
                     "void ST_Drawer (boolean fullscreen, boolean refresh)\n"
                     "{\n    (void)fullscreen; (void)refresh;\n    ST_doPaletteStuff();\n}")
    replace_function(status, "void ST_Ticker (void)",
                     "void ST_Ticker (void)\n{\n    /* compact FM-1 HUD needs no widget state */\n}")
    replace_function(status, "void ST_Start (void)",
                     "void ST_Start (void)\n{\n    plyr = &players[consoleplayer];\n"
                     "    st_palette = -1;\n    st_stopped = false;\n}")
    replace_function(status, "void ST_Init (void)",
                     "void ST_Init (void)\n{\n"
                     "    lu_palette = W_GetNumForName(DEH_String(\"PLAYPAL\"));\n}")
    headsup = TARGET / "hu_stuff.c"
    replace_function(headsup, "void HU_Init(void)",
                     "void HU_Init(void)\n{\n    /* No 320-pixel font in direct-E1M1 mode. */\n}")
    replace_function(headsup, "void HU_Start(void)",
                     "void HU_Start(void)\n{\n    plr = &players[consoleplayer];\n"
                     "    headsupactive = true;\n}")
    for name in ("HU_Drawer", "HU_Erase", "HU_Ticker"):
        replace_function(headsup, f"void {name}(void)",
                         f"void {name}(void)\n{{\n    /* Text UI is omitted. */\n}}")
    menu = TARGET / "m_menu.c"
    menu_source = menu.read_text(encoding="utf-8")
    marker = '#include "m_menu.h"'
    if menu_source.count(marker) != 1:
        raise ValueError("unexpected m_menu.h inclusion")
    menu_source = menu_source.replace(marker, marker + '''

/* Original menu patches have 320x200 coordinates; sample them into 160x100. */
static void FM1_DrawPatchHalf(int x, int y, patch_t *patch)
{
    int sx, sy, dx, dy;
    column_t *column;
    const byte *pixels;
    x -= SHORT(patch->leftoffset);
    y -= SHORT(patch->topoffset);
    for (sx = 0; sx < SHORT(patch->width); ++sx)
    {
        dx = (x + sx) / 2;
        if ((x + sx) < 0 || ((x + sx) & 1) || dx >= SCREENWIDTH)
            continue;
        column = (column_t *)((byte *)patch + LONG(patch->columnofs[sx]));
        while (column->topdelta != 0xff)
        {
            pixels = (const byte *)column + 3;
            for (sy = 0; sy < column->length; ++sy)
            {
                int py = y + column->topdelta + sy;
                dy = py / 2;
                if (py >= 0 && !(py & 1) && dy < SCREENHEIGHT)
                    I_VideoBuffer[dy * SCREENWIDTH + dx] = pixels[sy];
            }
            column = (column_t *)((byte *)column + column->length + 4);
        }
    }
}
#define V_DrawPatchDirect FM1_DrawPatchHalf
''')
    menu.write_text(menu_source, encoding="utf-8")
    replace_once(menu, '''    {1,"M_OPTION",M_Options,'o'},
    {1,"M_LOADG",M_LoadGame,'l'},
    {1,"M_SAVEG",M_SaveGame,'s'},
    // Another hickup with Special edition.
    {1,"M_RDTHIS",M_ReadThis,'r'},
    {1,"M_QUITG",M_QuitDOOM,'q'}''', '''    {1,"M_OPTION",M_Options,'o'}''')
    replace_once(menu, '''        // Commercial has no "read this" entry.
\tMainMenu[readthis] = MainMenu[quitdoom];
\tMainDef.numitems--;
\tMainDef.y += 8;
\tNewDef.prevMenu = &MainDef;''', '''        /* The FM-1 main menu always has New Game and Options. */''')
    replace_once(menu, "    main_end,\n    NULL,\n    MainMenu,", "    2,\n    NULL,\n    MainMenu,")
    replace_once(menu, "    newg_end,\t\t// # of menu items\n    &EpiDef,",
                 "    4,\t\t// nightmare excluded from low-memory profile\n    &MainDef,")
    replace_function(menu, "void M_NewGame(int choice)\n{",
                     "void M_NewGame(int choice)\n{\n"
                     "    (void)choice;\n    M_SetupNextMenu(&NewDef);\n}")
    source = menu.read_text(encoding="utf-8")
    start = source.index("menuitem_t OptionsMenu[]=")
    end = source.index("menu_t  OptionsDef =", start)
    source = source[:start] + '''menuitem_t OptionsMenu[]=
{
    {1,"M_DETAIL", M_ChangeDetail,'g'}
};

''' + source[end:]
    menu.write_text(source, encoding="utf-8")
    replace_once(menu, "    opt_end,\n    &MainDef,\n    OptionsMenu,",
                 "    1,\n    &MainDef,\n    OptionsMenu,")
    replace_function(menu, "void M_DrawOptions(void)\n{",
                     "void M_DrawOptions(void)\n{\n"
                     "    V_DrawPatchDirect(108, 15,\n"
                     "        W_CacheLumpName(DEH_String(\"M_OPTTTL\"), PU_CACHE));\n"
                     "    V_DrawPatchDirect(260, OptionsDef.y,\n"
                     "        W_CacheLumpName(DEH_String(detailNames[detailLevel]), PU_CACHE));\n"
                     "}")
    replace_once(menu, "    if (key == -1)\n\treturn false;",
                 "    if (key == -1)\n\treturn false;\n"
                 "    if (menuactive && key == KEY_FIRE) key = key_menu_forward;\n"
                 "    if (menuactive && key == KEY_USE) key = key_menu_back;")
    replace_once(menu, "\tcurrentMenu->lastOn = itemOn;\n\tM_ClearMenus ();\n\tS_StartSound(NULL,sfx_swtchx);\n\treturn true;",
                 "\tcurrentMenu->lastOn = itemOn;\n"
                 "\tif (currentMenu->prevMenu)\n"
                 "\t{\n\t    currentMenu = currentMenu->prevMenu;\n"
                 "\t    itemOn = currentMenu->lastOn;\n\t}\n"
                 "\telse M_ClearMenus ();\n"
                 "\tS_StartSound(NULL,sfx_swtchx);\n\treturn true;")
    # This first-stage build fixes skill 1 and has no DeHackEd patches, so the
    # large state/actor definition tables can live in XIP flash instead of RAM.
    game = TARGET / "g_game.c"
    start = game.read_text(encoding="utf-8").index("    if (fastparm || (skill == sk_nightmare")
    end = game.read_text(encoding="utf-8").index("    // force players to be initialized", start)
    source = game.read_text(encoding="utf-8")
    game.write_text(source[:start] +
                    "    if (fastparm || skill == sk_nightmare)\n"
                    "        I_Error(\"fast/nightmare mode is unavailable in FM-1 direct boot\");\n\n"
                    + source[end:], encoding="utf-8")
    # The packaged first stage contains E1M1 only. Complete either exit by
    # starting that map again; never enter the omitted intermission/E1M2 art.
    replace_function(game, "void G_DoCompleted (void)",
                     "void G_DoCompleted (void)\n{\n"
                     "    G_DeferedInitNew(gameskill, 1, 1);\n}")
    replace_once(TARGET / "info.c", "state_t\tstates[NUMSTATES] =",
                 "const state_t states[NUMSTATES] =")
    replace_once(TARGET / "info.c", "mobjinfo_t mobjinfo[NUMMOBJTYPES] =",
                 "const mobjinfo_t mobjinfo[NUMMOBJTYPES] =")
    replace_once(TARGET / "info.h", "extern state_t\tstates[NUMSTATES];",
                 "extern const state_t states[NUMSTATES];")
    replace_once(TARGET / "info.h", "extern mobjinfo_t mobjinfo[NUMMOBJTYPES];",
                 "extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];")
    replace_count(TARGET / "p_mobj.h", "mobjinfo_t*\t\tinfo;", "const mobjinfo_t*\t\tinfo;", 1)
    replace_count(TARGET / "p_mobj.h", "state_t*\t\tstate;", "const state_t*\t\tstate;", 1)
    replace_count(TARGET / "p_pspr.h", "state_t*\tstate;", "const state_t*\tstate;", 1)
    replace_count(TARGET / "p_mobj.c", "state_t*\tst;", "const state_t*\tst;", 2)
    replace_count(TARGET / "p_mobj.c", "mobjinfo_t*\tinfo;", "const mobjinfo_t*\tinfo;", 1)
    replace_count(TARGET / "p_pspr.c", "state_t*\tstate;", "const state_t*\tstate;", 2)
    replace_count(TARGET / "p_enemy.c", "mobjinfo_t*\t\tinfo;", "const mobjinfo_t*\t\tinfo;", 1)
    replace_count(TARGET / "f_finale.c", "state_t*\tcaststate;", "const state_t*\tcaststate;", 1)
    replace_once(TARGET / "p_saveg.c", "static void saveg_writep(void *p)",
                 "static void saveg_writep(const void *p)")
    # These definition tables are immutable. Their legacy name lookups only
    # hash, compare or copy the names; they never write through the pointers.
    replace_once(TARGET / "p_switch.c", "switchlist_t alphSwitchList[] =",
                 "const switchlist_t alphSwitchList[] =")
    for name in ("name1", "name2"):
        replace_once(TARGET / "p_switch.c", f"DEH_String(alphSwitchList[i].{name})",
                     f"DEH_String((char *)alphSwitchList[i].{name})")
    replace_once(TARGET / "p_spec.c", "animdef_t\t\tanimdefs[] =",
                 "const animdef_t animdefs[] =")
    for name in ("startname", "endname"):
        replace_once(TARGET / "p_spec.c", f"DEH_String(animdefs[i].{name})",
                     f"DEH_String((char *)animdefs[i].{name})")
    # Local single-player command buffering does not need 128 network tics.
    replace_once(TARGET / "net_defs.h", "#define BACKUPTICS 128", "#define BACKUPTICS 16")
    # R_FindPlane fails closed at this bound; retain the original openings and
    # drawseg capacities until whole-map visibility testing exists.
    replace_once(TARGET / "r_plane.c", "#define MAXVISPLANES\t128",
                 "#define MAXVISPLANES\t64")
    # This lookup only stores -1..viewwidth+1, so 16 bits cover 160x100.
    replace_once(TARGET / "r_main.c", "int\t\t\tviewangletox[FINEANGLES/2];",
                 "short viewangletox[FINEANGLES/2];")
    replace_once(TARGET / "r_state.h", "extern int\t\tviewangletox[FINEANGLES/2];",
                 "extern short viewangletox[FINEANGLES/2];")
    # The first-stage FM-1 profile has no writable filesystem. Use compiled
    # defaults and avoid creating a save/config directory during boot.
    replace_once(TARGET / "d_main.c", "    I_AtExit(M_SaveDefaults, false);",
                 "    /* No writable config store in the FM-1 E1M1 profile. */")
    replace_once(TARGET / "m_config.c",
                 "    LoadDefaultCollection(&doom_defaults);\n    LoadDefaultCollection(&extra_defaults);",
                 "    /* Keep the bound compiled defaults; no target config files. */")
    replace_function(TARGET / "m_misc.c", "void M_MakeDirectory(char *path)",
                     "void M_MakeDirectory(char *path)\n{\n"
                     "    (void)path; /* No writable save/config store. */\n}")
    replace_once(TARGET / "d_iwad.c", '#include "d_iwad.h"',
                 '#include "d_iwad.h"\n#include "fm1_doom_wad_file.h"')
    replace_once(TARGET / "d_iwad.c", "    if (M_FileExists(name))\n    {\n        return name;\n    }",
                 "    if (fm1_doom_archive_matches(name)) return name;\n"
                 "    if (M_FileExists(name))\n    {\n        return name;\n    }")
    # The SDK formatter supports %d; scanf %i must retain base detection.
    for path in sorted(TARGET.glob("*.[ch]")):
        source = path.read_text(encoding="utf-8")
        path.write_text(normalize_printf_formats(source), encoding="utf-8")
    print(TARGET)
    print("Experimental 160x100 engine generated with compact HUD and half-size Doom menu.")


if __name__ == "__main__":
    main()
