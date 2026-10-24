// C versions of TILE.ASM / SPRITE.ASM / SPRITEBG.ASM: draw an 8x8 pattern (values 0-3) into the
// 8-bit screen with the NES palette base folded into the pixel value.
#include <string.h>
#include "types.h"
#include "nesvideo.h"

extern "C" int PITCH, SCREENX, SCREENY;

extern "C" {

// background tile: every pixel is written (0 = backdrop colour), palette base 224
void draw_tile_asm(bitmap8x8 *b, char *dest, int x, int y, byte pal)
{
 const byte base = (byte)((pal << 2) | 224);
 for (int j = 0; j < 8; j++)
  {
   int dy = y + j;
   if (dy < 0 || dy >= SCREENY) continue;
   for (int i = 0; i < 8; i++)
    {
     int dx = x + i;
     if (dx < 0 || dx >= SCREENX) continue;
     dest[dy * PITCH + dx] = b->s[j][i] | base;
    }
  }
}

// o: bit0 = flip X, bit1 = flip Y. Pixel value 0 is transparent. Palette base 240.
static void sprite(bitmap8x8 *b, char *dest, int x, int y, int o, byte pal, bool behind)
{
 const byte base = (byte)((pal << 2) | (224 + 16));
 for (int j = 0; j < 8; j++)
  {
   int dy = y + j;
   if (dy < 0 || dy >= SCREENY) continue;
   const byte *row = b->s[(o & 2) ? 7 - j : j];
   for (int i = 0; i < 8; i++)
    {
     byte v = row[(o & 1) ? 7 - i : i];
     if (!v) continue;
     int dx = x + i;
     if (dx < 0 || dx >= SCREENX) continue;
     byte *p = (byte *)dest + dy * PITCH + dx;
     if (behind && (*p & 0x13)) continue;   // only show through where the background is transparent
     *p = v | base;
    }
  }
}

void draw_sprite_asm(bitmap8x8 *b, char *dest, int x, int y, int o, byte pal)        {sprite(b, dest, x, y, o, pal, false);}
void draw_sprite_behind_asm(bitmap8x8 *b, char *dest, int x, int y, int o, byte pal) {sprite(b, dest, x, y, o, pal, true);}

}
