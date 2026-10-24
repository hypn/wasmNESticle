// Small replacements for platform-specific services referenced by the game code.
#include <stdio.h>
#include <emscripten.h>
#include "types.h"

// build.cpp only defined these for Watcom/Borland/MSVC; shown in the About box
char buildcompiler[] = "Emscripten";
int buildcompilerversionhigh = __EMSCRIPTEN_major__;
int buildcompilerversionlow = __EMSCRIPTEN_minor__;

// the web page provides the audio device (see port/platform.cpp)
byte soundinstalled = 1;

// DirectDraw diagnostics dialog (Win95 only): nothing to report
void ddrawinfo() {}

// On-disk structures must match the 1997 layouts exactly (gui.vol, .nes, .r2 files).
#include "vol.h"
#include "rom.h"
#include "r2img.h"
static_assert(sizeof(header) == 18, "vol header must be 18 bytes");
static_assert(sizeof(NESROMHEADER) == 16, "iNES header must be 16 bytes");
static_assert(sizeof(COLOR) == 3, "palette entries are RGB bytes");
static_assert(sizeof(BMAP) == 16, "image header is 4 ints");
static_assert(sizeof(void *) == 4, "volbatch stores 4-byte pointers (wasm32 only)");
