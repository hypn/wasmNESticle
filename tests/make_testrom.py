#!/usr/bin/env python3 -I
"""Writes a small original NROM test ROM (no third-party code/art) that exercises what homebrew
uses first: background tiles + palette, NMI, OAM DMA + a sprite, and the controller (A button).
  * fills the screen with tile 1 (colour 1 = left half, colour 2 = right half, palette 0)
  * sprites 1-4: tile 3 (one marker pixel at top-left) with no/H/V/H+V flip, palette 1
  * sprite 0 = solid 8x8 tile 2 (colour 3 of sprite palette 0) at x=120,y=100 via $4014 DMA each NMI
  * NMI handler: increments zero-page $00, DMAs OAM, reads $4016; while A is held, background
    colour 1 ($3F01) becomes white ($30), otherwise red ($16)
usage: make_testrom.py out.nes"""
import sys

code = bytearray(); labels = {}; fix = []
def emit(*b): code.extend(b)
def label(n): labels[n] = len(code)
def rel(op, n): emit(op, 0); fix.append(('rel', len(code) - 1, n))
def absl(op, n, off=0): emit(op, 0, 0); fix.append(('abs', len(code) - 2, n))
LDAi=lambda v: emit(0xA9, v); STAa=lambda a: emit(0x8D, a & 255, a >> 8)
LDXi=lambda v: emit(0xA2, v); LDYi=lambda v: emit(0xA0, v); STXa=lambda a: emit(0x8E, a & 255, a >> 8)

emit(0x78, 0xD8, 0xA2, 0xFF, 0x9A)                    # SEI CLD LDX #$FF TXS
LDXi(0); STXa(0x2000); STXa(0x2001); emit(0x86, 0x00) # PPUCTRL=PPUMASK=0; STX $00
label('vb1'); emit(0xAD, 0x02, 0x20); rel(0x10, 'vb1')  # wait for vblank
LDAi(0x3F); STAa(0x2006); LDAi(0x00); STAa(0x2006)    # PPUADDR=$3F00
LDXi(0)
label('pal'); absl(0xBD, 'paldata'); STAa(0x2007); emit(0xE8, 0xE0, 24); rel(0xD0, 'pal')   # 24 palette bytes
LDAi(0x20); STAa(0x2006); LDAi(0x00); STAa(0x2006)    # PPUADDR=$2000
LDAi(1); LDYi(3); LDXi(0)
label('f1'); STAa(0x2007); emit(0xE8); rel(0xD0, 'f1'); emit(0x88); rel(0xD0, 'f1')   # 768 tiles
LDXi(192)
label('f2'); STAa(0x2007); emit(0xCA); rel(0xD0, 'f2')                                  # +192 = 960
LDAi(0); LDXi(64)
label('f3'); STAa(0x2007); emit(0xCA); rel(0xD0, 'f3')                                  # attribute table = 0
LDAi(100); STAa(0x0200); LDAi(2); STAa(0x0201); LDAi(0); STAa(0x0202); LDAi(120); STAa(0x0203)  # sprite 0
for i, (attr, x) in enumerate([(0x01, 160), (0x41, 176), (0x81, 192), (0xC1, 208)]):   # flip markers
    base = 0x0204 + i * 4
    LDAi(100); STAa(base); LDAi(3); STAa(base + 1); LDAi(attr); STAa(base + 2); LDAi(x); STAa(base + 3)
LDAi(0); STAa(0x2005); STAa(0x2005)
LDAi(0x80); STAa(0x2000)                              # enable NMI
LDAi(0x1E); STAa(0x2001)                              # show background + sprites
label('hang'); absl(0x4C, 'hang')

label('nmi')
emit(0xE6, 0x00)                                      # INC $00 (frame counter)
LDAi(2); STAa(0x4014)                                 # OAM DMA from $0200
LDAi(1); STAa(0x4016); LDAi(0); STAa(0x4016)          # strobe controller
emit(0xAD, 0x16, 0x40, 0x29, 0x01); rel(0xF0, 'notA') # LDA $4016; AND #1; BEQ notA
LDAi(0x30); absl(0x4C, 'setcol')
label('notA'); LDAi(0x16)
label('setcol'); emit(0xAA)                           # TAX
LDAi(0x3F); STAa(0x2006); LDAi(0x01); STAa(0x2006); STXa(0x2007)
LDAi(0); STAa(0x2005); STAa(0x2005)
LDAi(0x80); STAa(0x2000)                              # restore PPUCTRL (nametable 0): the $2006 writes above changed t's nametable bits
emit(0x40)                                            # RTI
label('paldata')
emit(0x0F, 0x16, 0x2A, 0x12) ; emit(*([0x0F] * 12)) ; emit(0x0F, 0x30, 0x21, 0x27) ; emit(0x0F, 0x30, 0x21, 0x11)

BASE = 0x8000
for kind, pos, name in fix:
    if kind == 'rel': code[pos] = (labels[name] - (pos + 1)) & 0xFF
    else: a = BASE + labels[name]; code[pos], code[pos + 1] = a & 255, a >> 8

assert len(code) < 0x3FFA
prg = bytearray(code) + bytearray(0x4000 - len(code))
nmi = BASE + labels['nmi']
prg[0x3FFA:0x3FFC] = bytes([nmi & 255, nmi >> 8])
prg[0x3FFC:0x3FFE] = bytes([BASE & 255, BASE >> 8])
prg[0x3FFE:0x4000] = bytes([nmi & 255, nmi >> 8])
chr_ = bytearray(0x2000)
chr_[16:24] = bytes([0xF0] * 8); chr_[24:32] = bytes([0x0F] * 8)     # tile 1
chr_[32:40] = bytes([0xFF] * 8); chr_[40:48] = bytes([0xFF] * 8)     # tile 2: solid colour 3
chr_[48] = 0x80; chr_[56] = 0x80                                       # tile 3: one pixel (colour 3) at its top-left
open(sys.argv[1], 'wb').write(b'NES\x1a' + bytes([1, 1, 0, 0]) + bytes(8) + prg + chr_)
