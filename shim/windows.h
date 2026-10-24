/* shim: the Win32 API is not available under Emscripten.
   Only the analog-joystick structures used by input.cpp are stubbed. */
#include "compat.h"
#ifndef WASM_WINDOWS_SHIM
#define WASM_WINDOWS_SHIM
#define JOYERR_NOERROR 0
struct JOYINFO { unsigned wXpos, wYpos, wZpos, wButtons; };
static inline int joyGetPos(int, JOYINFO *) { return 165; /* JOYERR_UNPLUGGED: no analog joysticks */ }
#endif
