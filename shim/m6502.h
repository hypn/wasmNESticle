/* Interface to the 6502 core. The original was x86 assembly (m6502.asm); this header
   keeps the same names so cpu.cpp/nes.cpp/rom.cpp/mmc.cpp build unchanged. */
#ifndef WASM_M6502_H
#define WASM_M6502_H
#include <stdint.h>
#ifndef DWORD
#define DWORD uint32_t
#endif
#ifndef WORD
#define WORD uint16_t
#endif
#ifndef BYTE
#define BYTE uint8_t
#endif

extern DWORD m6502clockticks;
extern BYTE *m6502rom;   /* byte at address a>=0x8000 is m6502rom[a] */
extern BYTE *m6502ram;   /* 2KB mirrored through 0x0000-0x1FFF */
extern WORD m6502lastjsr;
extern WORD m6502pc;
extern WORD m6502af;     /* A in the high byte, flags in the low byte (x86 AH:AL style) */
extern BYTE m6502x, m6502y, m6502s;
extern DWORD cyclesRemaining; /* set to 0 by the host to abort the current m6502exec() */

DWORD m6502nmi(void);
DWORD m6502int(void);
DWORD m6502exec(DWORD cycles);   /* returns 0x10000 on normal completion */
void  m6502reset(void);

extern int  (*m6502MemoryWrite)(WORD, BYTE);
extern WORD (*m6502MemoryRead)(WORD);
#endif
