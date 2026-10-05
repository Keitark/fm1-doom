/* GPL-2.0-or-later: FM-1 video replacement for Doomgeneric's desktop i_video.
   Uses the engine's indexed render buffer (320x200 by default) and converts
   eight LCD rows at a time without allocating a 32-bit output frame. */
#include "i_video.h"
#if FM1_DOOM_SOURCE_WIDTH == 160 && FM1_DOOM_SOURCE_HEIGHT == 100
#include "doomstat.h"
#include "d_items.h"
#endif
#include "i_system.h"
#include "v_video.h"
#include "tables.h"
#include "z_zone.h"
#include "fm1_doom_runtime.h"
#include <limits.h>
#include <string.h>

byte *I_VideoBuffer;
boolean screenvisible;
boolean screensaver_mode;
float mouse_acceleration = 2.0f;
int mouse_threshold = 10;
int usegamma;
int screen_width = 240, screen_height = 240;
char *video_driver;
static uint8_t palette[768];

#if FM1_DOOM_SOURCE_WIDTH == 160 && FM1_DOOM_SOURCE_HEIGHT == 100
/* Five-pixel digits keep basic health/ammo visible when the original 320-wide
 * status bar is suppressed in the low-resolution experiment. */
static const uint8_t hud_glyphs[12][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7},
    {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1},
    {7,5,7,5,7}, {7,5,7,1,7}, {5,5,7,5,5}, {2,5,7,5,5}
};
static void hud_glyph(unsigned x, unsigned y, unsigned glyph)
{
    unsigned row, col;
    for (row = 0; row < 5; ++row)
        for (col = 0; col < 3; ++col)
            if (hud_glyphs[glyph][row] & (4u >> col))
                I_VideoBuffer[(y + row) * SCREENWIDTH + x + col] = 4;
}
static void hud_number(unsigned x, unsigned y, int number)
{
    unsigned value = number < 0 ? 0u : number > 999 ? 999u : (unsigned)number;
    hud_glyph(x, y, value / 100u);
    hud_glyph(x + 4u, y, (value / 10u) % 10u);
    hud_glyph(x + 8u, y, value % 10u);
}
static void hud_draw(void)
{
    const player_t *player = &players[consoleplayer];
    int ammo = 0;
    if (player->readyweapon >= 0 && player->readyweapon < NUMWEAPONS) {
        ammotype_t type = weaponinfo[player->readyweapon].ammo;
        if (type >= 0 && type < NUMAMMO) ammo = player->ammo[type];
    }
    memset(I_VideoBuffer + (SCREENHEIGHT - 7) * SCREENWIDTH, 0, 7 * SCREENWIDTH);
    hud_glyph(2, SCREENHEIGHT - 6, 10);  /* H */
    hud_number(7, SCREENHEIGHT - 6, player->health);
    hud_glyph(80, SCREENHEIGHT - 6, 11); /* A */
    hud_number(85, SCREENHEIGHT - 6, ammo);
}
#endif

void I_GetEvent(void);

void I_InitGraphics(void)
{
    if (!fm1_doom_active_port()) I_Error("FM-1 IO is not bound");
    I_VideoBuffer = Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);
    screenvisible = true;
    { extern void I_InitInput(void); I_InitInput(); }
}

void I_ShutdownGraphics(void)
{
    if (I_VideoBuffer) Z_Free(I_VideoBuffer);
    I_VideoBuffer = NULL;
}

void I_StartFrame(void) { }
void I_StartTic(void) { I_GetEvent(); }
void I_UpdateNoBlit(void) { }
void I_FinishUpdate(void)
{
#if FM1_DOOM_SOURCE_WIDTH == 160 && FM1_DOOM_SOURCE_HEIGHT == 100
    if (!menuactive) hud_draw();
    fm1_doom_active_port()->menu_visible = menuactive;
#endif
    if (fm1_doom_present(fm1_doom_active_port(), I_VideoBuffer))
        I_Error("FM-1 LCD transfer failed");
}
void I_ReadScreen(byte *out)
{
    if (out && I_VideoBuffer) memcpy(out, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}
void I_SetPalette(byte *rgb)
{
    unsigned i;
    if (!rgb) return;
    for (i = 0; i < 768; ++i) palette[i] = gammatable[usegamma][rgb[i]];
    fm1_doom_palette(fm1_doom_active_port(), palette);
}
int I_GetPaletteIndex(int r, int g, int b)
{
    unsigned i;
    int best = 0, best_distance = INT_MAX;
    for (i = 0; i < 256; ++i) {
        int dr = r - palette[i * 3u];
        int dg = g - palette[i * 3u + 1u];
        int db = b - palette[i * 3u + 2u];
        int distance = dr * dr + dg * dg + db * db;
        if (distance < best_distance) { best = (int)i; best_distance = distance; }
    }
    return best;
}
void I_BeginRead(void) { }
void I_EndRead(void) { }
void I_SetWindowTitle(char *title) { (void)title; }
void I_GraphicsCheckCommandLine(void) { }
void I_SetGrabMouseCallback(grabmouse_callback_t callback) { (void)callback; }
void I_EnableLoadingDisk(void) { }
void I_BindVideoVariables(void) { }
void I_DisplayFPSDots(boolean enabled) { (void)enabled; }
void I_CheckIsScreensaver(void) { }
void I_InitWindowTitle(void) { }
void I_InitWindowIcon(void) { }
