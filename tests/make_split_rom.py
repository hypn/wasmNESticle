#!/usr/bin/env python3 -I
"""Writes an original mapper-4 test ROM for mid-frame scroll changes (a split screen done the way real
games do it). Every nametable row k encodes its own number: tile columns 0-4 are tile 1 or tile 2 for
bits 0-4 of k, and every pixel row r of those tiles encodes r (pixel 0 = r&3, pixel 1 = r>>2), so a screenshot
can be decoded back into (nametable row, fine Y, scroll X) for every line.
  frame start (NMI):   scroll X=11, Y=25      -> top section
  scanline IRQ at line L: $2006 = $20,$A4     -> v = fineY 2, nametable 0, coarse Y 5, coarse X 4 (row 42)
                          $2005 = $26         -> fine X 6, coarse X 4 (used from the next line)
                          $2005 = $C8         -> a mid-frame Y write: only t changes, must NOT move the picture
usage: make_split_rom.py out.nes LINE"""
import sys
L = int(sys.argv[2])

code = bytearray(); labels = {}; fix = []
def emit(*b): code.extend(b)
def label(n): labels[n] = len(code)
def rel(op, n): emit(op, 0); fix.append(('rel', len(code) - 1, n))
def absl(op, n): emit(op, 0, 0); fix.append(('abs', len(code) - 2, n))
def lda(v): emit(0xA9, v)
def sta(a): emit(0x8D, a & 255, a >> 8)

# ---- reset
emit(0x78, 0xD8, 0xA2, 0xFF, 0x9A)                       # SEI CLD LDX #$FF TXS
lda(0); sta(0x2000); sta(0x2001)
label('vb'); emit(0xAD, 0x02, 0x20); rel(0x10, 'vb')     # wait for vblank
lda(0x3F); sta(0x2006); lda(0); sta(0x2006); emit(0xA2, 0)
label('pal'); absl(0xBD, 'paldata'); sta(0x2007); emit(0xE8, 0xE0, 4); rel(0xD0, 'pal')
lda(0x20); sta(0x2006); lda(0); sta(0x2006); absl(0x20, 'copy')     # nametable 0
lda(0x28); sta(0x2006); lda(0); sta(0x2006); absl(0x20, 'copy')     # nametable 2 (same: vertical wrap)
lda(0x80); sta(0x2000); lda(0x0A); sta(0x2001)           # NMI on, background on (left column shown)
emit(0x58)                                               # CLI
label('hang'); absl(0x4C, 'hang')

label('copy')                                            # copies the 1024-byte table to PPUDATA
emit(0xA9, 0); fix.append(('lo', len(code) - 1, 'table')); emit(0x85, 0x00)
emit(0xA9, 0); fix.append(('hi', len(code) - 1, 'table')); emit(0x85, 0x01)
emit(0xA0, 0, 0xA2, 4)
label('cl'); emit(0xB1, 0x00); sta(0x2007); emit(0xC8); rel(0xD0, 'cl'); emit(0xE6, 0x01, 0xCA); rel(0xD0, 'cl'); emit(0x60)

# ---- NMI: scroll for the top of the frame, arm the scanline IRQ
label('nmi')
lda(0x0B); sta(0x2005); lda(25); sta(0x2005); lda(0x80); sta(0x2000)
sta(0xE000); lda(L); sta(0xC000); sta(0xC001); sta(0xE001)
emit(0x40)

# ---- IRQ: the split
label('irq')
sta(0xE000)
lda(0x20); sta(0x2006); lda(0xA4); sta(0x2006)
lda(0x26); sta(0x2005); lda(0xC8); sta(0x2005)
emit(0x40)

label('paldata'); emit(0x0F, 0x16, 0x2A, 0x12)
while len(code) < 0x400: emit(0)
label('table')
for k in range(30):
    emit(*[2 if (k >> j) & 1 else 1 for j in range(5)], *([3] * 27))
emit(*([0] * 64))                                        # attribute table

for kind, pos, name in fix:
    if kind == 'rel': code[pos] = (labels[name] - (pos + 1)) & 0xFF
    elif kind == 'abs': a = 0x8000 + labels[name]; code[pos], code[pos + 1] = a & 255, a >> 8
    elif kind == 'lo': code[pos] = (0x8000 + labels[name]) & 255
    else: code[pos] = (0x8000 + labels[name]) >> 8

prg = bytearray(0x8000)
base = 0x4000                                            # mapper 4 starts with pages 2,3 at $8000/$A000 (and again at $C000/$E000)
prg[base:base + len(code)] = code
vec = 0x7FFA
nmi, rst, irq = (0x8000 + labels[n] for n in ('nmi', 'hang', 'irq'))
prg[vec:vec + 6] = bytes([nmi & 255, nmi >> 8, 0x00, 0x80, irq & 255, irq >> 8])   # RESET = $8000

chr_ = bytearray(0x2000)
def tile(n, bit):
    for r in range(8):
        px = [r & 3, (r >> 2) & 1, bit + 1, 3, 3, 3, 3, 3] if bit is not None else [3] * 8
        chr_[n * 16 + r] = sum((p & 1) << (7 - i) for i, p in enumerate(px))
        chr_[n * 16 + 8 + r] = sum(((p >> 1) & 1) << (7 - i) for i, p in enumerate(px))
tile(1, 0); tile(2, 1); tile(3, None)
open(sys.argv[1], 'wb').write(b'NES\x1a' + bytes([2, 1, 0x40, 0]) + bytes(8) + prg + chr_)
