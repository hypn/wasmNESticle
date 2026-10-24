// Platform layer for the WebAssembly build: replaces the DirectDraw/DirectInput/WinMM code of the
// Win95 build (and the DOS drivers). There is no main loop here: JavaScript calls the nes_*
// exports below from requestAnimationFrame and from DOM input events.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <emscripten.h>

#include "types.h"
#include "config.h"
#include "dd.h"
#include "mouse.h"
#include "keyb.h"
#include "message.h"
#include "timing.h"
#include "guivol.h"
#include "r2img.h"
#include "nesvideo.h"
#include "guirect.h"
#include "m6502.h"

//-------------------------------------------------------------------
// video: one 8-bit paletted framebuffer, converted to RGBA once per frame
//-------------------------------------------------------------------
#define MAXW 1600
#define MAXH 1200

extern "C" { int PITCH = 640, BANKED = 0; }
char *video = 0;
char *screen = 0;

static uint8_t  pal[256][3];            // 8-bit components
static uint32_t *rgba = 0;              // output for JavaScript
static int dirtypal = 1;

void setpalette(PALETTE *p)
{
 for (int i = 0; i < 256; i++) {pal[i][0] = p->c[i].r; pal[i][1] = p->c[i].g; pal[i][2] = p->c[i].b;}
}

void setpalettenum(int index, COLOR *c)
{
 // 8-bit components (the GUI palette is scaled up in initgame; NESticle.pal is already 8-bit)
 pal[index][0] = c->r; pal[index][1] = c->g; pal[index][2] = c->b;
}

void changeresolution(int xw, int yw)
{
 if (xw > MAXW || yw > MAXH) {msg.error("%dx%d mode not supported", xw, yw); return;}
 SCREENX = xw; SCREENY = yw; PITCH = xw;
 memset(screen, 0, MAXW * MAXH);
 msg.printf(2, "Video mode set: %dx%dx%d", SCREENX, SCREENY, 8);
}

//-------------------------------------------------------------------
// off-screen surface (used by the nametable caches)
//-------------------------------------------------------------------
surface::surface(int txw, int tyw)
{
 xw = txw; yw = tyw;
 dds = (char *)calloc(xw * yw, 1);
 oldscrx = oldscry = oldpitch = 0;
}
surface::~surface() {free(dds);}
// Like the DirectDraw version, locking re-points the drawing globals at the surface so the
// blitters (draw_tile_asm etc.) render into it; unlock() restores the screen.
char *surface::lock()
{
 oldscrx = SCREENX; oldscry = SCREENY; oldpitch = PITCH;
 SCREENX = xw; SCREENY = yw; PITCH = xw;
 return dds;
}
void surface::unlock()
{
 SCREENX = oldscrx; SCREENY = oldscry; PITCH = oldpitch;
}
int surface::blt(char *dest, int x, int y) {return blt(0, 0, xw, yw, dest, x, y);}
// source rectangle is (sx,sy)-(sxw,syw) as lower-right coordinates; clipped to the screen
int surface::blt(int sx, int sy, int sxw, int syw, char *dest, int x, int y)
{
 if (!dds) return 0;
 if (sxw > xw) sxw = xw;
 if (syw > yw) syw = yw;
 int w = sxw - sx, h = syw - sy;
 if (x < 0) {sx -= x; w += x; x = 0;}
 if (y < 0) {sy -= y; h += y; y = 0;}
 if (x + w > SCREENX) w = SCREENX - x;
 if (y + h > SCREENY) h = SCREENY - y;
 for (int j = 0; j < h; j++)
  memcpy(dest + (y + j) * PITCH + x, dds + (sy + j) * xw + sx, w);
 return 1;
}

//-------------------------------------------------------------------
// timer / misc services declared in dd.h
//-------------------------------------------------------------------
volatile int timerbusy = 0;
char errstr[80];

// Wall clock, or a manually advanced virtual clock so headless tests are deterministic.
static int virtualclock = 0;
static double virtualnow = 0;
static double now_ms() {return virtualclock ? virtualnow : emscripten_get_now();}

extern "C" unsigned timeGetTime(void)
{
 return (unsigned)now_ms();
}
void settimerspeed(int x) {if (x > 0) TIMERSPEED = x;}
void disable() {}
void enable() {}
void *loadresource(char *) {return 0;}
void cleanexit(int x)
{
 fprintf(stderr, "NESticle: fatal error %d: %s\n", x, errstr);
 emscripten_force_exit(x);
}
static int wantquit = 0;
void quitgame() {wantquit = 1;}

class mouse m;

//-------------------------------------------------------------------
// keyboard: PC set-1 scancodes (bit 7 = release), as the DOS keyboard handler delivered them
//-------------------------------------------------------------------
volatile char kbscan = 0;
volatile char kbstat = 0;
volatile char keydown[128];
static int (*kbfunc)() = 0;

#define KQ 64
static char kq[KQ]; static int kqhead = 0, kqtail = 0;

void set_keyboard_func(int (*f)()) {kbfunc = f;}
void pushkey(char k) {int n = (kqhead + 1) % KQ; if (n != kqtail) {kq[kqhead] = k; kqhead = n;}}
int keyhit() {return kqhead != kqtail;}
char getkey() {if (kqhead == kqtail) return 0; char k = kq[kqtail]; kqtail = (kqtail + 1) % KQ; return k;}
char waitkey() {return getkey();}   // cannot block in a browser

static const char lower[128] = {
 0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,9,
 'q','w','e','r','t','y','u','i','o','p','[',']',13,0,'a','s',
 'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
 'b','n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,
};
static const char upper[128] = {
 0,27,'!','@','#','$','%','^','&','*','(',')','_','+',8,9,
 'Q','W','E','R','T','Y','U','I','O','P','{','}',13,0,'A','S',
 'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
 'B','N','M','<','>','?',0,'*',0,' ',0,0,0,0,0,0,
};
char scan2ascii(char s)
{
 unsigned char c = s & 0x7F;
 if (c >= 64) return 0;
 return (kbstat & KB_SHIFT) ? upper[c] : lower[c];
}

// Callers (the key-redefinition dialog) keep these pointers, so every scancode needs its own
// permanent string - a shared buffer made all the button fields show the same name.
char *getkeyname(char key)
{
 static char names[128][8];
 unsigned char c = key & 0x7F;
 switch (c)
  {
   case KB_UP: return (char *)"Up";       case KB_DOWN: return (char *)"Down";
   case KB_LEFT: return (char *)"Left";   case KB_RIGHT: return (char *)"Right";
   case KB_HOME: return (char *)"Home";   case KB_END: return (char *)"End";
   case KB_PGUP: return (char *)"PgUp";   case KB_PGDN: return (char *)"PgDn";
   case KB_INS: return (char *)"Ins";     case KB_DEL: return (char *)"Del";
   case KB_ESC: return (char *)"Esc";     case KB_TAB: return (char *)"Tab";
   case KB_BKSP: return (char *)"Bksp";   case KB_ENTER: return (char *)"Enter";
   case KB_SPACE: return (char *)"Space";
  }
 char *buf = names[c];
 if (c >= KB_F1 && c <= KB_F10) snprintf(buf, 8, "F%d", c - KB_F1 + 1);
 else if (c < 64 && lower[c] > 32) snprintf(buf, 8, "%c", lower[c] >= 'a' && lower[c] <= 'z' ? lower[c] - 32 : lower[c]);
 else snprintf(buf, 8, "0x%02X", c);
 return buf;
}

//-------------------------------------------------------------------
// the game side (main.cpp / cpu.cpp)
//-------------------------------------------------------------------
extern char configfile[];
extern int  initgame();
extern void updatescreen();
extern void terminategame();
extern void gametimer();
extern void enablegui();
static int started = 0;
static int viewx = 0, viewy = 0, vieww = 640, viewh = 480;   // part of the framebuffer worth showing
static double lasttick = 0;

//-------------------------------------------------------------------
// audio: the mixer (nessound.cpp) hands over one frame of 16-bit mono samples per emulated frame;
// they wait in a ring buffer until the web page pulls them into WebAudio. If nobody pulls (or the
// page is slow) the oldest samples are dropped, which bounds latency.
//-------------------------------------------------------------------
#define AUDIORING 32768   // power of two: ~0.74 s at 44.1 kHz
static short audioring[AUDIORING];
static unsigned audiohead = 0, audiotail = 0;     // free-running counters

void primaryoutput(short *buf, int nums)
{
 for (int i = 0; i < nums; i++) audioring[audiohead++ & (AUDIORING - 1)] = buf[i];
 if (audiohead - audiotail > AUDIORING) audiotail = audiohead - AUDIORING;
}
void primaryskip(int) {}
void clearprimary() {audiotail = audiohead;}

// File -> Load ROM: if the page supplied Module.pickRom (it opens the browser's file chooser),
// call it and report success; otherwise the caller falls back to the in-app file dialog.
EM_JS(int, js_pick_rom, (), {
  if (typeof Module.pickRom !== 'function') return 0;
  Module.pickRom();
  return 1;
});
int wasm_pick_rom() {return js_pick_rom();}

extern "C" {

EMSCRIPTEN_KEEPALIVE int nes_init(void)
{
 if (started) return 0;
 screen = video = (char *)calloc(MAXW * MAXH, 1);
 rgba = (uint32_t *)calloc(MAXW * MAXH, 4);
 PITCH = SCREENX;

 cfg = new config();
 cfg->load(configfile);
 enablegui();
 if (initgame() != 0) {fprintf(stderr, "NESticle: unable to initialize (missing gui.vol?)\n"); return -1;}
 resetGUIdef();   // cascade origin for new dialogs; the recovered toolkit never initialised it
 started = 1;
 lasttick = now_ms();
 return 0;
}

// runs the game timer at TIMERSPEED Hz, draws one frame, converts it to RGBA
EMSCRIPTEN_KEEPALIVE int nes_frame(void)
{
 if (!started) return -1;
 double now = now_ms(), step = 1000.0 / TIMERSPEED;
 if (now - lasttick > step * 6) lasttick = now - step * 6;   // don't spiral after a stalled tab
 while (now >= lasttick + step) {gametimer(); lasttick += step;}

 if (nv) nv->refreshpalette();
 m.reset();
 // the recovered GUI toolkit's root does not paint the desktop; the original did when this option is off
 if (!cfg->get(CFG_NOFILLEDDESKTOP))
  for (int y = 0; y < SCREENY; y++) memset(screen + y * PITCH, 9 * 16 + 14, SCREENX);
 updatescreen();

 // With the menu hidden and the game maximised, only the game picture is shown (the page scales it
 // to the window); otherwise the whole desktop.
 extern int guienabled;
 if (!guienabled && nv && nv->maximized && nv->xw > 0 && nv->yw > 0 && nv->xw <= SCREENX && nv->yw <= SCREENY)
  {vieww = nv->xw; viewh = nv->yw; viewx = (SCREENX - vieww) / 2; viewy = (SCREENY - viewh) / 2;}
 else {viewx = viewy = 0; vieww = SCREENX; viewh = SCREENY;}

 for (int y = 0; y < SCREENY; y++)
  {
   const uint8_t *s = (const uint8_t *)screen + y * PITCH;
   uint32_t *d = rgba + y * SCREENX;
   for (int x = 0; x < SCREENX; x++)
    {
     const uint8_t *c = pal[s[x]];
     d[x] = 0xFF000000u | (c[2] << 16) | (c[1] << 8) | c[0];
    }
  }
 return wantquit;
}

// loads a .nes file from the virtual filesystem, hides the GUI and starts it (same as `-runrom`)
extern "C++" int cmd_runrom(char *p);
EMSCRIPTEN_KEEPALIVE int nes_runrom(const char *path)
{
 if (!started) return -1;
 char buf[256];
 strncpy(buf, path, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
 return cmd_runrom(buf);
}

// tests: byte i (0-13) of keyboard device dev's keymap (8 directions, then 6 buttons)
EMSCRIPTEN_KEEPALIVE int nes_keymap(int dev, int i) {return cfg ? ((unsigned char *)&cfg->ids.km[dev & 1])[i % 14] : -1;}

// tests: read a byte of the emulated CPU address space (RAM backing store)
extern byte *ram;
EMSCRIPTEN_KEEPALIVE int nes_peek(int addr) {return ram ? ram[addr & 0xFFFF] : -1;}

// debugging aid: 0=pc 1=running 2=paused 3=clockticks 4=gui enabled 5=video present
EMSCRIPTEN_KEEPALIVE int nes_state(int what)
{
 extern byte CPURunning, CPUpaused; extern int guienabled;
 switch (what)
  {
   case 0: return m6502pc;  case 1: return CPURunning; case 2: return CPUpaused;
   case 3: return (int)m6502clockticks; case 4: return guienabled; case 5: return nv != 0;
  }
 return -1;
}

// tests: switch to a virtual clock and advance it by hand
EMSCRIPTEN_KEEPALIVE void nes_virtual_time(double ms) {virtualclock = 1; virtualnow = ms;}

// copies up to max queued audio samples (signed 16-bit mono, nes_audio_rate() Hz) to out
EMSCRIPTEN_KEEPALIVE int nes_audio_pull(short *out, int max)
{
 int n = (int)(audiohead - audiotail);
 if (n > max) n = max;
 for (int i = 0; i < n; i++) out[i] = audioring[audiotail++ & (AUDIORING - 1)];
 return n;
}
// discards everything queued (e.g. when audio starts late, so old sound is not played)
EMSCRIPTEN_KEEPALIVE void nes_audio_clear(void) {clearprimary();}
EMSCRIPTEN_KEEPALIVE int nes_audio_rate(void) {extern int SOUNDRATE; return SOUNDRATE;}

EMSCRIPTEN_KEEPALIVE uint32_t *nes_framebuffer(void) {return rgba;}
// rectangle of the framebuffer to display: 0=x 1=y 2=width 3=height
EMSCRIPTEN_KEEPALIVE int nes_view(int what) {return what == 0 ? viewx : what == 1 ? viewy : what == 2 ? vieww : viewh;}
EMSCRIPTEN_KEEPALIVE int nes_width(void)  {return SCREENX;}
EMSCRIPTEN_KEEPALIVE int nes_height(void) {return SCREENY;}

// buttons: bit0 left, bit1 right, bit2 middle
EMSCRIPTEN_KEEPALIVE void nes_mouse(int x, int y, int buttons)
{
 if (!started) return;
 m.updatexy(x, y);
 if (buttons != m.b) m.updatebut(buttons);
}

// scancode: PC set-1 make code; down: 1/0
EMSCRIPTEN_KEEPALIVE void nes_key(int scancode, int down)
{
 if (!started || scancode <= 0 || scancode >= 128) return;
 int bit = scancode == 0x2A || scancode == 0x36 ? KB_SHIFT : scancode == 0x1D ? KB_CTRL : scancode == 0x38 ? KB_ALT : 0;
 if (bit) {if (down) kbstat |= bit; else kbstat &= ~bit;}
 keydown[scancode] = down ? 1 : 0;
 kbscan = down ? scancode : (scancode | 0x80);
 if (down) pushkey(kbscan);
 if (kbfunc) kbfunc();
}

} // extern "C"
