/* Force-included into every NESticle translation unit (-include compat.h).
   Papers over Watcom/Borland/MSVC-isms in the 1997 sources so Source/ stays pristine. */
#ifndef WASM_COMPAT_H
#define WASM_COMPAT_H

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <unistd.h>

#define __cdecl
#define _fastcall
#define __fastcall
#define USERENTRY
#define O_BINARY 0
#define stricmp  strcasecmp
#define strnicmp strncasecmp

/* provided by port/platform.cpp (replaces the Win32 multimedia timer) */
#ifdef __cplusplus
extern "C"
#endif
unsigned timeGetTime(void);

#endif
