#!/usr/bin/env python3 -I
"""Writes a tiny original NROM ROM (32K PRG) that programs the sound registers once and idles.
usage: make_audio_rom.py out.nes variant
variants: pulse, pulse_off, pulse_len, pulse_env, triangle, noise, dmc"""
import sys

# (register, value) writes after $4015 is set; every variant sets the $4015 enable first.
V = {
 # 50% duty, loop/halt, constant volume 15; timer 253 -> 1789773/(16*254) = 440.4 Hz
 'pulse':     (0x01, [(0x4000, 0xBF), (0x4001, 0x00), (0x4002, 253), (0x4003, 0x08)]),
 # same registers but the channel is not enabled in $4015: must stay silent
 'pulse_off': (0x00, [(0x4000, 0xBF), (0x4001, 0x00), (0x4002, 253), (0x4003, 0x08)]),
 # no halt, length index 3 (2 half-frame ticks = one frame): ends almost immediately
 'pulse_len': (0x01, [(0x4000, 0x9F), (0x4001, 0x00), (0x4002, 253), (0x4003, 0x18)]),
 # decaying envelope (period 15, no loop), long length: fades over ~64 frames
 'pulse_env': (0x01, [(0x4000, 0x8F), (0x4001, 0x00), (0x4002, 253), (0x4003, 0x08)]),
 # triangle, linear counter 127, timer 126 -> 1789773/(32*127) = 440.4 Hz
 'triangle':  (0x04, [(0x4008, 0xFF), (0x400A, 126), (0x400B, 0x08)]),
 # noise, constant volume 15, period index 4
 'noise':     (0x08, [(0x400C, 0x3F), (0x400E, 0x04), (0x400F, 0x08)]),
 # sample channel: loop, rate index 15 (54 cycles/bit), level 0x40, sample at $C000, 17 bytes
 'dmc':       (0x10, [(0x4010, 0x4F), (0x4011, 0x40), (0x4012, 0x00), (0x4013, 0x01)]),
}
enable, writes = V[sys.argv[2]]

code = bytearray([0x78, 0xD8, 0xA2, 0xFF, 0x9A])               # SEI CLD LDX #$FF TXS
def sta(reg, val): code.extend([0xA9, val, 0x8D, reg & 255, reg >> 8])
sta(0x4015, enable)
for reg, val in writes: sta(reg, val)
if sys.argv[2] == 'dmc': sta(0x4015, enable)                    # (re)start the sample after its registers are set
hang = 0x8000 + len(code)
code.extend([0x4C, hang & 255, hang >> 8])                      # JMP self

bank0 = bytearray(code) + bytearray(0x4000 - len(code))
bank1 = bytearray([0xFF, 0x00] * 8 + [0xFF]) + bytearray(0x4000 - 17)   # $C000: sample bytes (up/down ramp)
for off in (0x3FFA, 0x3FFC, 0x3FFE): bank1[off:off + 2] = bytes([0x00, 0x80])   # all vectors -> $8000
open(sys.argv[1], 'wb').write(b'NES\x1a' + bytes([2, 0, 0, 0]) + bytes(8) + bank0 + bank1)
