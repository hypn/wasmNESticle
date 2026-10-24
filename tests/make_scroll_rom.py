#!/usr/bin/env python3 -I
"""Writes a small original NROM ROM that scrolls forever: every NMI it advances the X scroll by 1 and
the Y scroll by 3 (wrapping at 240), stepping through all four nametables as X wraps. Nametable 0
is filled with tile 1 (stripes), the second row of nametables with tile 2 (solid), the rest is blank,
so the picture is regularly cut across nametable edges. The left 8 pixels are masked, as most
games do. usage: make_scroll_rom.py out.nes"""
import sys
code = bytearray(); labels = {}; fix = []
def emit(*b): code.extend(b)
def label(n): labels[n] = len(code)
def rel(op, n): emit(op, 0); fix.append(('rel', len(code) - 1, n))
def absl(op, n): emit(op, 0, 0); fix.append(('abs', len(code) - 2, n))
LDAi = lambda v: emit(0xA9, v); STAa = lambda a: emit(0x8D, a & 255, a >> 8)
LDXi = lambda v: emit(0xA2, v); LDYi = lambda v: emit(0xA0, v)

emit(0x78, 0xD8, 0xA2, 0xFF, 0x9A)                         # SEI CLD LDX #$FF TXS
LDXi(0); STAa(0x2000); STAa(0x2001); emit(0x86, 0x00, 0x86, 0x01, 0x86, 0x02)   # zero x/y/nametable counters
label('vb'); emit(0xAD, 0x02, 0x20); rel(0x10, 'vb')
LDAi(0x3F); STAa(0x2006); LDAi(0); STAa(0x2006); LDXi(0)
label('pal'); absl(0xBD, 'paldata'); STAa(0x2007); emit(0xE8, 0xE0, 4); rel(0xD0, 'pal')
def fill(hi, tile):
    LDAi(hi); STAa(0x2006); LDAi(0); STAa(0x2006)
    LDAi(tile); LDYi(4); LDXi(0)
    lab = 'f%x_%d' % (hi, tile)
    label(lab); STAa(0x2007); emit(0xE8); rel(0xD0, lab); emit(0x88); rel(0xD0, lab)
fill(0x20, 1); fill(0x28, 2)                                # (horizontal mirroring: $2400 = $2000, $2C00 = $2800)
LDAi(0x80); STAa(0x2000); LDAi(0x08); STAa(0x2001)          # NMI on, show background but hide the leftmost 8 pixels
                                                             # (NESticle then shrinks its clip box by 8 px per side, which
                                                             # gives the neighbouring nametable a negative-width blit)
label('hang'); absl(0x4C, 'hang')
label('nmi')
emit(0xE6, 0x00)                                            # INC $00            (x scroll)
rel(0xD0, 'noflip')                                         # BNE
emit(0xE6, 0x02, 0xA5, 0x02, 0x29, 0x03, 0x85, 0x02)        # x wrapped: INC $02; LDA $02; AND #3; STA $02
label('noflip')
emit(0xA5, 0x01, 0x18, 0x69, 0x03, 0xC9, 0xF0, 0x90, 0x02, 0xE9, 0xF0, 0x85, 0x01)   # y = (y+3) mod 240
emit(0xA5, 0x00, 0x8D, 0x05, 0x20, 0xA5, 0x01, 0x8D, 0x05, 0x20)                  # PPUSCROLL = x, y
emit(0xA5, 0x02, 0x09, 0x80, 0x8D, 0x00, 0x20)              # PPUCTRL = NMI | nametable select
emit(0x40)                                                  # RTI
label('paldata'); emit(0x0F, 0x16, 0x2A, 0x12)
for kind, pos, name in fix:
    if kind == 'rel': code[pos] = (labels[name] - (pos + 1)) & 0xFF
    else: a = 0x8000 + labels[name]; code[pos], code[pos + 1] = a & 255, a >> 8
prg = bytearray(code) + bytearray(0x4000 - len(code))
nmi = 0x8000 + labels['nmi']
prg[0x3FFA:0x3FFC] = bytes([nmi & 255, nmi >> 8]); prg[0x3FFC:0x3FFE] = bytes([0, 0x80]); prg[0x3FFE:0x4000] = bytes([nmi & 255, nmi >> 8])
chr_ = bytearray(0x2000); chr_[16:24] = bytes([0xF0] * 8); chr_[24:32] = bytes([0x0F] * 8)   # tile 1
chr_[32:40] = bytes([0xFF] * 8); chr_[40:48] = bytes([0xFF] * 8)                           # tile 2
open(sys.argv[1], 'wb').write(b'NES\x1a' + bytes([1, 1, 0, 0]) + bytes(8) + prg + chr_)
