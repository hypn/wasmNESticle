#!/usr/bin/env python3 -I
"""Writes a tiny original mapper-4 ROM whose scanline IRQ handler starts with an invalid opcode ($02).
Reset programs the IRQ line ($C000) and enables interrupts; the first IRQ then runs the bad opcode.
usage: make_irqjam_rom.py out.nes"""
import sys
code = bytes([
 0x78, 0xD8, 0xA2, 0xFF, 0x9A,          # SEI CLD LDX #$FF TXS
 0xAD, 0x02, 0x20, 0x10, 0xFB,          # wait: LDA $2002 ; BPL wait
 0xA9, 0x30, 0x8D, 0x00, 0xC0,          # LDA #$30 ; STA $C000   (IRQ at scanline 48)
 0x58,                                  # CLI
 0x4C, 0x10, 0x80,                      # hang: JMP $8010 (self)
])
prg = bytearray(0x8000)                 # 4 x 8K pages; mapper 4 starts with pages 2,3 at $8000/$A000 and 2,3 again at $C000/$E000
base = 0x4000                           # page 2 = file offset 0x4000 = CPU $8000
prg[base:base + len(code)] = code
prg[base + 0x100] = 0x02                # IRQ handler at $8100: invalid opcode
prg[base + 0x200] = 0x40                # NMI handler at $8200: RTI
vec = 0x7FFA                            # last bytes of the last page = $FFFA..$FFFF
prg[vec:vec + 6] = bytes([0x00, 0x82, 0x00, 0x80, 0x00, 0x81])   # NMI $8200, RESET $8000, IRQ $8100
chr_ = bytearray(0x2000)
open(sys.argv[1], 'wb').write(b'NES\x1a' + bytes([2, 1, 0x40, 0]) + bytes(8) + prg + chr_)   # mapper 4
