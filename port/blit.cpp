// Portable C replacements for the x86 assembly in R2IMGASM.ASM (image/rect/line blitters).
// All routines clip to [0,SCREENX) x [0,SCREENY) relative to the destination pointer;
// the CLIP class (dd.h) implements sub-windows by moving the pointer and shrinking SCREENX/Y.
#include <string.h>
#include "types.h"
#include "r2img.h"

extern "C" int PITCH, SCREENX, SCREENY;

extern "C" {

// RLE image: for each scanline, alternating [transparent run][opaque run + pixels] until xw
// pixels are covered. Scanline starts come from ydisp[] (offsets from the start of the IMG).
// o: IMG_FLIPY (1) draws the bottom scanline first, IMG_FLIPX (2) mirrors each scanline.
void drawimager2(IMG *s, char *d, int x, int y, int o)
{
 if (!s || s->xw <= 0 || s->yw <= 0) return;
 const int xw = s->xw, yw = s->yw;
 if (x >= SCREENX || y >= SCREENY || x + xw <= 0 || y + yw <= 0) return;
 const int *yd = s->ydisp();

 for (int sy = 0; sy < yw; sy++)
  {
   int dy = y + ((o & IMG_FLIPY) ? yw - 1 - sy : sy);
   if (dy < 0 || dy >= SCREENY) continue;
   char *row = d + dy * PITCH;
   const unsigned char *p = (const unsigned char *)s + yd[sy];

   int px = 0; // position within the image row
   while (px < xw)
    {
     px += *p++;           // transparent run
     if (px >= xw) break;
     int run = *p++;       // opaque run
     if (run > xw - px) run = xw - px;
     if (o & IMG_FLIPX)
      {
       for (int i = 0; i < run; i++)
        {
         int dx = x + xw - 1 - (px + i);
         if (dx >= 0 && dx < SCREENX) row[dx] = p[i];
        }
      }
     else
      {
       int dx = x + px, from = 0, n = run;
       if (dx < 0) {from = -dx; n += dx; dx = 0;}
       if (dx + n > SCREENX) n = SCREENX - dx;
       if (n > 0) memcpy(row + dx, p + from, n);
      }
     p += run; px += run;
    }
  }
}

// copies an uncompressed rectangle; s has row pitch 'pitch', d uses PITCH. Caller clips.
void drawscr(char *s, char *d, int xw, int yw, int pitch)
{
 if (xw <= 0) return;
 for (; yw > 0; yw--, s += pitch, d += PITCH) memcpy(d, s, xw);
}

// horizontal line from x to x2 (exclusive)
void drawhline(char *d, int color, int x, int y, int x2)
{
 if (y < 0 || y >= SCREENY) return;
 if (x < 0) x = 0;
 if (x2 > SCREENX) x2 = SCREENX;
 if (x2 <= x) return;
 memset(d + y * PITCH + x, color, x2 - x);
}

// vertical line from y to y2 (exclusive)
void drawvline(char *d, int color, int x, int y, int y2)
{
 if (x < 0 || x >= SCREENX) return;
 if (y < 0) y = 0;
 if (y2 > SCREENY) y2 = SCREENY;
 for (d += y * PITCH + x; y < y2; y++, d += PITCH) *d = (char)color;
}

// remaps the pixels under a rectangle through a colormap (used for shadows)
void drawrectmap(char *d, COLORMAP *map, int x, int y, int xw, int yw)
{
 int x2 = x + xw, y2 = y + yw;
 if (x < 0) x = 0;
 if (y < 0) y = 0;
 if (x2 > SCREENX) x2 = SCREENX;
 if (y2 > SCREENY) y2 = SCREENY;
 for (int j = y; j < y2; j++)
  {
   unsigned char *p = (unsigned char *)d + j * PITCH;
   for (int i = x; i < x2; i++) p[i] = map->c[p[i]];
  }
}

// the original paletted-hardware loaders are not needed: the platform layer owns the palette
void loadpalette8(struct VBE_palette *, int, int) {}
void loadpalette6(struct VBE_palette *, int, int) {}

} // extern "C"

// filled rectangle (declared with C++ linkage under WIN95 in r2img.h)
void drawrect(char *d, int color, int x, int y, int xw, int yw)
{
 int x2 = x + xw, y2 = y + yw;
 if (x < 0) x = 0;
 if (y < 0) y = 0;
 if (x2 > SCREENX) x2 = SCREENX;
 if (y2 > SCREENY) y2 = SCREENY;
 if (x2 <= x) return;
 for (int j = y; j < y2; j++) memset(d + j * PITCH + x, color, x2 - x);
}
