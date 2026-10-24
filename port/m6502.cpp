// Portable NMOS 6502 core (documented opcodes) with the interface of the original m6502.asm
// (see shim/m6502.h). Written for this port; no third-party CPU code.
#include <stdint.h>
#include "m6502.h"

DWORD m6502clockticks = 0;
BYTE *m6502rom = 0;
BYTE *m6502ram = 0;
WORD m6502lastjsr = 0;
WORD m6502pc = 0;
WORD m6502af = 0;      // A in the high byte, P (status) in the low byte
BYTE m6502x = 0, m6502y = 0, m6502s = 0xFD;
DWORD cyclesRemaining = 0;
int  (*m6502MemoryWrite)(WORD, BYTE) = 0;
WORD (*m6502MemoryRead)(WORD) = 0;

enum { C = 0x01, Z = 0x02, I = 0x04, D = 0x08, B = 0x10, U = 0x20, V = 0x40, N = 0x80 };

static inline BYTE rd(WORD a)
{
 if (a < 0x2000) return m6502ram[a & 0x7FF];
 if (a >= 0x8000) return m6502rom[a];
 return m6502MemoryRead ? (BYTE)m6502MemoryRead(a) : 0;
}
static inline void wr(WORD a, BYTE d)
{
 if (a < 0x2000) {m6502ram[a & 0x7FF] = d; return;}
 if (m6502MemoryWrite) m6502MemoryWrite(a, d);
}
static inline WORD rd16(WORD a) {return rd(a) | (rd(a + 1) << 8);}
// the 6502 JMP (ind) bug: the high byte is fetched from the same page
static inline WORD rd16bug(WORD a) {return rd(a) | (rd((a & 0xFF00) | ((a + 1) & 0xFF)) << 8);}

// registers live in locals during execution
namespace {
struct CPU {
 WORD pc; BYTE a, x, y, s, p;
 void push(BYTE d) {wr(0x100 + s, d); s--;}
 BYTE pull() {s++; return rd(0x100 + s);}
 void nz(BYTE v) {p = (p & ~(N | Z)) | (v & N) | (v ? 0 : Z);}
};
}

static const BYTE cycletab[256] = {
// 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
   7, 6, 0, 0, 0, 3, 5, 0, 3, 2, 2, 0, 0, 4, 6, 0, // 0
   2, 5, 0, 0, 0, 4, 6, 0, 2, 4, 0, 0, 0, 4, 7, 0, // 1
   6, 6, 0, 0, 3, 3, 5, 0, 4, 2, 2, 0, 4, 4, 6, 0, // 2
   2, 5, 0, 0, 0, 4, 6, 0, 2, 4, 0, 0, 0, 4, 7, 0, // 3
   6, 6, 0, 0, 0, 3, 5, 0, 3, 2, 2, 0, 3, 4, 6, 0, // 4
   2, 5, 0, 0, 0, 4, 6, 0, 2, 4, 0, 0, 0, 4, 7, 0, // 5
   6, 6, 0, 0, 0, 3, 5, 0, 4, 2, 2, 0, 5, 4, 6, 0, // 6
   2, 5, 0, 0, 0, 4, 6, 0, 2, 4, 0, 0, 0, 4, 7, 0, // 7
   0, 6, 0, 0, 3, 3, 3, 0, 2, 0, 2, 0, 4, 4, 4, 0, // 8
   2, 6, 0, 0, 4, 4, 4, 0, 2, 5, 2, 0, 0, 5, 0, 0, // 9
   2, 6, 2, 0, 3, 3, 3, 0, 2, 2, 2, 0, 4, 4, 4, 0, // A
   2, 5, 0, 0, 4, 4, 4, 0, 2, 4, 2, 0, 4, 4, 4, 0, // B
   2, 6, 0, 0, 3, 3, 5, 0, 2, 2, 2, 0, 4, 4, 6, 0, // C
   2, 5, 0, 0, 0, 4, 6, 0, 2, 4, 0, 0, 0, 4, 7, 0, // D
   2, 6, 0, 0, 3, 3, 5, 0, 2, 2, 2, 0, 4, 4, 6, 0, // E
   2, 5, 0, 0, 0, 4, 6, 0, 2, 4, 0, 0, 0, 4, 7, 0, // F
};

static void adc(CPU &c, BYTE m)
{
 if (c.p & D) { // decimal mode (the NES 2A03 has none, but keep the NMOS behaviour)
  int lo = (c.a & 0xF) + (m & 0xF) + (c.p & C);
  int hi = (c.a >> 4) + (m >> 4);
  if (lo > 9) {lo += 6; hi++;}
  if (hi > 9) hi += 6;
  int bin = c.a + m + (c.p & C);
  c.p &= ~(C | Z | N | V);
  if (!(bin & 0xFF)) c.p |= Z;
  c.p |= (hi << 4) & N;
  if (~(c.a ^ m) & (c.a ^ (hi << 4)) & 0x80) c.p |= V;
  if (hi > 15) c.p |= C;
  c.a = ((hi << 4) | (lo & 0xF)) & 0xFF;
  return;
 }
 int r = c.a + m + (c.p & C);
 c.p &= ~(C | V);
 if (r > 0xFF) c.p |= C;
 if (~(c.a ^ m) & (c.a ^ r) & 0x80) c.p |= V;
 c.a = (BYTE)r; c.nz(c.a);
}
static void sbc(CPU &c, BYTE m)
{
 if (c.p & D) {
  int lo = (c.a & 0xF) - (m & 0xF) - (1 - (c.p & C));
  int hi = (c.a >> 4) - (m >> 4);
  int bin = c.a - m - (1 - (c.p & C));
  if (lo < 0) {lo -= 6; hi--;}
  if (hi < 0) hi -= 6;
  c.p &= ~(C | Z | N | V);
  if (bin >= 0) c.p |= C;
  if (!(bin & 0xFF)) c.p |= Z;
  c.p |= bin & N;
  if ((c.a ^ m) & (c.a ^ bin) & 0x80) c.p |= V;
  c.a = ((hi << 4) | (lo & 0xF)) & 0xFF;
  return;
 }
 adc(c, m ^ 0xFF);
}
static void cmp(CPU &c, BYTE r, BYTE m)
{
 c.p &= ~C; if (r >= m) c.p |= C;
 c.nz((BYTE)(r - m));
}
static BYTE asl(CPU &c, BYTE v) {c.p = (c.p & ~C) | (v >> 7); v <<= 1; c.nz(v); return v;}
static BYTE lsr(CPU &c, BYTE v) {c.p = (c.p & ~C) | (v & 1); v >>= 1; c.nz(v); return v;}
static BYTE rol(CPU &c, BYTE v) {BYTE o = c.p & C; c.p = (c.p & ~C) | (v >> 7); v = (v << 1) | o; c.nz(v); return v;}
static BYTE ror(CPU &c, BYTE v) {BYTE o = c.p & C; c.p = (c.p & ~C) | (v & 1); v = (v >> 1) | (o << 7); c.nz(v); return v;}

static void interrupt(CPU &c, WORD vector, bool brk)
{
 c.push(c.pc >> 8); c.push(c.pc & 0xFF);
 c.push(((c.p | U) & ~B) | (brk ? B : 0));
 c.p |= I;
 c.pc = rd16(vector);
}  // callers account for the 7 cycles

#define LOAD()  CPU c; c.pc = m6502pc; c.a = m6502af >> 8; c.p = m6502af & 0xFF; c.x = m6502x; c.y = m6502y; c.s = m6502s
#define STORE() m6502pc = c.pc; m6502af = (c.a << 8) | (c.p & 0xFF); m6502x = c.x; m6502y = c.y; m6502s = c.s

void m6502reset(void)
{
 m6502af = U | I;
 m6502x = m6502y = 0;
 m6502s = 0xFD;
 m6502pc = rd16(0xFFFC);
}

DWORD m6502nmi(void)
{
 LOAD(); interrupt(c, 0xFFFA, false); STORE();
 m6502clockticks += 7;
 return 0x10000;
}

DWORD m6502int(void)
{
 LOAD();
 if (!(c.p & I)) {interrupt(c, 0xFFFE, false); STORE(); m6502clockticks += 7;}
 return 0x10000;
}

DWORD m6502exec(DWORD cycles)
{
 LOAD();
 cyclesRemaining = cycles;
 while ((int)cyclesRemaining > 0)
  {
   WORD opaddr = c.pc;
   BYTE op = rd(c.pc++);
   int cy = cycletab[op];
   if (!cy) {m6502pc = opaddr; STORE(); m6502pc = opaddr; return opaddr;} // unsupported opcode

   // operand fetch helpers
   #define IMM()   (rd(c.pc++))
   #define ZP()    ((WORD)rd(c.pc++))
   #define ZPX()   ((WORD)(BYTE)(rd(c.pc++) + c.x))
   #define ZPY()   ((WORD)(BYTE)(rd(c.pc++) + c.y))
   #define ABS()   (c.pc += 2, rd16(c.pc - 2))
   #define ABSX(pen) ({WORD b = ABS(); WORD e = b + c.x; if (pen && ((b ^ e) & 0xFF00)) cy++; e;})
   #define ABSY(pen) ({WORD b = ABS(); WORD e = b + c.y; if (pen && ((b ^ e) & 0xFF00)) cy++; e;})
   #define INDX()  ({BYTE z = rd(c.pc++) + c.x; (WORD)(rd(z) | (rd((BYTE)(z + 1)) << 8));})
   #define INDY(pen) ({BYTE z = rd(c.pc++); WORD b = rd(z) | (rd((BYTE)(z + 1)) << 8); WORD e = b + c.y; if (pen && ((b ^ e) & 0xFF00)) cy++; e;})
   #define BRANCH(cond) {int8_t o = (int8_t)rd(c.pc++); if (cond) {WORD t = c.pc + o; cy += 1 + (((t ^ c.pc) & 0xFF00) ? 1 : 0); c.pc = t;}}
   // read-modify-write
   #define RMW(addr, fn) {WORD ea = (addr); BYTE v = rd(ea); wr(ea, v); wr(ea, fn(c, v));}

   switch (op)
    {
     // ---- ORA / AND / EOR / ADC / SBC / CMP / LDA / STA
     case 0x09: c.a |= IMM(); c.nz(c.a); break;
     case 0x05: c.a |= rd(ZP()); c.nz(c.a); break;
     case 0x15: c.a |= rd(ZPX()); c.nz(c.a); break;
     case 0x0D: c.a |= rd(ABS()); c.nz(c.a); break;
     case 0x1D: c.a |= rd(ABSX(1)); c.nz(c.a); break;
     case 0x19: c.a |= rd(ABSY(1)); c.nz(c.a); break;
     case 0x01: c.a |= rd(INDX()); c.nz(c.a); break;
     case 0x11: c.a |= rd(INDY(1)); c.nz(c.a); break;

     case 0x29: c.a &= IMM(); c.nz(c.a); break;
     case 0x25: c.a &= rd(ZP()); c.nz(c.a); break;
     case 0x35: c.a &= rd(ZPX()); c.nz(c.a); break;
     case 0x2D: c.a &= rd(ABS()); c.nz(c.a); break;
     case 0x3D: c.a &= rd(ABSX(1)); c.nz(c.a); break;
     case 0x39: c.a &= rd(ABSY(1)); c.nz(c.a); break;
     case 0x21: c.a &= rd(INDX()); c.nz(c.a); break;
     case 0x31: c.a &= rd(INDY(1)); c.nz(c.a); break;

     case 0x49: c.a ^= IMM(); c.nz(c.a); break;
     case 0x45: c.a ^= rd(ZP()); c.nz(c.a); break;
     case 0x55: c.a ^= rd(ZPX()); c.nz(c.a); break;
     case 0x4D: c.a ^= rd(ABS()); c.nz(c.a); break;
     case 0x5D: c.a ^= rd(ABSX(1)); c.nz(c.a); break;
     case 0x59: c.a ^= rd(ABSY(1)); c.nz(c.a); break;
     case 0x41: c.a ^= rd(INDX()); c.nz(c.a); break;
     case 0x51: c.a ^= rd(INDY(1)); c.nz(c.a); break;

     case 0x69: adc(c, IMM()); break;
     case 0x65: adc(c, rd(ZP())); break;
     case 0x75: adc(c, rd(ZPX())); break;
     case 0x6D: adc(c, rd(ABS())); break;
     case 0x7D: adc(c, rd(ABSX(1))); break;
     case 0x79: adc(c, rd(ABSY(1))); break;
     case 0x61: adc(c, rd(INDX())); break;
     case 0x71: adc(c, rd(INDY(1))); break;

     case 0xE9: sbc(c, IMM()); break;
     case 0xE5: sbc(c, rd(ZP())); break;
     case 0xF5: sbc(c, rd(ZPX())); break;
     case 0xED: sbc(c, rd(ABS())); break;
     case 0xFD: sbc(c, rd(ABSX(1))); break;
     case 0xF9: sbc(c, rd(ABSY(1))); break;
     case 0xE1: sbc(c, rd(INDX())); break;
     case 0xF1: sbc(c, rd(INDY(1))); break;

     case 0xC9: cmp(c, c.a, IMM()); break;
     case 0xC5: cmp(c, c.a, rd(ZP())); break;
     case 0xD5: cmp(c, c.a, rd(ZPX())); break;
     case 0xCD: cmp(c, c.a, rd(ABS())); break;
     case 0xDD: cmp(c, c.a, rd(ABSX(1))); break;
     case 0xD9: cmp(c, c.a, rd(ABSY(1))); break;
     case 0xC1: cmp(c, c.a, rd(INDX())); break;
     case 0xD1: cmp(c, c.a, rd(INDY(1))); break;

     case 0xE0: cmp(c, c.x, IMM()); break;
     case 0xE4: cmp(c, c.x, rd(ZP())); break;
     case 0xEC: cmp(c, c.x, rd(ABS())); break;
     case 0xC0: cmp(c, c.y, IMM()); break;
     case 0xC4: cmp(c, c.y, rd(ZP())); break;
     case 0xCC: cmp(c, c.y, rd(ABS())); break;

     case 0xA9: c.a = IMM(); c.nz(c.a); break;
     case 0xA5: c.a = rd(ZP()); c.nz(c.a); break;
     case 0xB5: c.a = rd(ZPX()); c.nz(c.a); break;
     case 0xAD: c.a = rd(ABS()); c.nz(c.a); break;
     case 0xBD: c.a = rd(ABSX(1)); c.nz(c.a); break;
     case 0xB9: c.a = rd(ABSY(1)); c.nz(c.a); break;
     case 0xA1: c.a = rd(INDX()); c.nz(c.a); break;
     case 0xB1: c.a = rd(INDY(1)); c.nz(c.a); break;

     case 0xA2: c.x = IMM(); c.nz(c.x); break;
     case 0xA6: c.x = rd(ZP()); c.nz(c.x); break;
     case 0xB6: c.x = rd(ZPY()); c.nz(c.x); break;
     case 0xAE: c.x = rd(ABS()); c.nz(c.x); break;
     case 0xBE: c.x = rd(ABSY(1)); c.nz(c.x); break;
     case 0xA0: c.y = IMM(); c.nz(c.y); break;
     case 0xA4: c.y = rd(ZP()); c.nz(c.y); break;
     case 0xB4: c.y = rd(ZPX()); c.nz(c.y); break;
     case 0xAC: c.y = rd(ABS()); c.nz(c.y); break;
     case 0xBC: c.y = rd(ABSX(1)); c.nz(c.y); break;

     case 0x85: wr(ZP(), c.a); break;
     case 0x95: wr(ZPX(), c.a); break;
     case 0x8D: wr(ABS(), c.a); break;
     case 0x9D: wr(ABSX(0), c.a); break;
     case 0x99: wr(ABSY(0), c.a); break;
     case 0x81: wr(INDX(), c.a); break;
     case 0x91: wr(INDY(0), c.a); break;
     case 0x86: wr(ZP(), c.x); break;
     case 0x96: wr(ZPY(), c.x); break;
     case 0x8E: wr(ABS(), c.x); break;
     case 0x84: wr(ZP(), c.y); break;
     case 0x94: wr(ZPX(), c.y); break;
     case 0x8C: wr(ABS(), c.y); break;

     // ---- shifts / rotates / inc / dec
     case 0x0A: c.a = asl(c, c.a); break;
     case 0x06: RMW(ZP(), asl); break;
     case 0x16: RMW(ZPX(), asl); break;
     case 0x0E: RMW(ABS(), asl); break;
     case 0x1E: RMW(ABSX(0), asl); break;
     case 0x4A: c.a = lsr(c, c.a); break;
     case 0x46: RMW(ZP(), lsr); break;
     case 0x56: RMW(ZPX(), lsr); break;
     case 0x4E: RMW(ABS(), lsr); break;
     case 0x5E: RMW(ABSX(0), lsr); break;
     case 0x2A: c.a = rol(c, c.a); break;
     case 0x26: RMW(ZP(), rol); break;
     case 0x36: RMW(ZPX(), rol); break;
     case 0x2E: RMW(ABS(), rol); break;
     case 0x3E: RMW(ABSX(0), rol); break;
     case 0x6A: c.a = ror(c, c.a); break;
     case 0x66: RMW(ZP(), ror); break;
     case 0x76: RMW(ZPX(), ror); break;
     case 0x6E: RMW(ABS(), ror); break;
     case 0x7E: RMW(ABSX(0), ror); break;
     #define INC_(c, v) ({BYTE r = (BYTE)(v + 1); (c).nz(r); r;})
     #define DEC_(c, v) ({BYTE r = (BYTE)(v - 1); (c).nz(r); r;})
     case 0xE6: RMW(ZP(), INC_); break;
     case 0xF6: RMW(ZPX(), INC_); break;
     case 0xEE: RMW(ABS(), INC_); break;
     case 0xFE: RMW(ABSX(0), INC_); break;
     case 0xC6: RMW(ZP(), DEC_); break;
     case 0xD6: RMW(ZPX(), DEC_); break;
     case 0xCE: RMW(ABS(), DEC_); break;
     case 0xDE: RMW(ABSX(0), DEC_); break;
     case 0xE8: c.x++; c.nz(c.x); break;
     case 0xC8: c.y++; c.nz(c.y); break;
     case 0xCA: c.x--; c.nz(c.x); break;
     case 0x88: c.y--; c.nz(c.y); break;

     // ---- bit
     case 0x24: {BYTE m = rd(ZP()); c.p = (c.p & ~(N | V | Z)) | (m & (N | V)) | ((m & c.a) ? 0 : Z); break;}
     case 0x2C: {BYTE m = rd(ABS()); c.p = (c.p & ~(N | V | Z)) | (m & (N | V)) | ((m & c.a) ? 0 : Z); break;}

     // ---- branches
     case 0x10: BRANCH(!(c.p & N)); break;
     case 0x30: BRANCH(c.p & N); break;
     case 0x50: BRANCH(!(c.p & V)); break;
     case 0x70: BRANCH(c.p & V); break;
     case 0x90: BRANCH(!(c.p & C)); break;
     case 0xB0: BRANCH(c.p & C); break;
     case 0xD0: BRANCH(!(c.p & Z)); break;
     case 0xF0: BRANCH(c.p & Z); break;

     // ---- jumps / subroutines / interrupts
     case 0x4C: c.pc = rd16(c.pc); break;
     case 0x6C: c.pc = rd16bug(rd16(c.pc)); break;
     case 0x20: {WORD t = rd16(c.pc); m6502lastjsr = opaddr; c.pc++; c.push(c.pc >> 8); c.push(c.pc & 0xFF); c.pc = t; break;}
     case 0x60: {WORD lo = c.pull(); WORD hi = c.pull(); c.pc = ((hi << 8) | lo) + 1; break;}
     case 0x40: {c.p = (c.pull() & ~B) | U; WORD lo = c.pull(); WORD hi = c.pull(); c.pc = (hi << 8) | lo; break;}
     case 0x00: c.pc++; interrupt(c, 0xFFFE, true); cy = 7; break;

     // ---- stack / transfers / flags
     case 0x48: c.push(c.a); break;
     case 0x08: c.push(c.p | B | U); break;
     case 0x68: c.a = c.pull(); c.nz(c.a); break;
     case 0x28: c.p = (c.pull() & ~B) | U; break;
     case 0xAA: c.x = c.a; c.nz(c.x); break;
     case 0xA8: c.y = c.a; c.nz(c.y); break;
     case 0x8A: c.a = c.x; c.nz(c.a); break;
     case 0x98: c.a = c.y; c.nz(c.a); break;
     case 0xBA: c.x = c.s; c.nz(c.x); break;
     case 0x9A: c.s = c.x; break;
     case 0x18: c.p &= ~C; break;
     case 0x38: c.p |= C; break;
     case 0x58: c.p &= ~I; break;
     case 0x78: c.p |= I; break;
     case 0xB8: c.p &= ~V; break;
     case 0xD8: c.p &= ~D; break;
     case 0xF8: c.p |= D; break;
     case 0xEA: break;

     default: m6502pc = opaddr; STORE(); m6502pc = opaddr; return opaddr;
    }
   cyclesRemaining -= cy;
   // the host may abort the slice (e.g. an IRQ scanline was programmed) by zeroing cyclesRemaining
   // from inside a memory callback; sync registers so it can inspect them
   m6502clockticks += cy;
   m6502pc = c.pc;
  }
 STORE();
 return 0x10000;
}
