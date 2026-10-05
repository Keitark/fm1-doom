/* GPL-2.0-or-later: FM-1 video replacement for Doomgeneric's desktop i_video.
   Retains the original 320x200 indexed render buffer; converts only four LCD
   rows at a time, so no 32-bit full-frame allocation is required. */
#include "i_video.h"
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
