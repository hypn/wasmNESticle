#ifndef _NESSOUND_
#define _NESSOUND_

#include "types.h"

#include "uutimer.h"

// Model of the NES sound chip (APU): two pulse channels, triangle, noise and delta-modulation
// (sample) channel. The 1997 mixer in this file was a rough approximation (wrong pitch, no
// envelope/sweep/length model, silent noise and sample channels); this replaces it, keeping the
// interface the rest of NESticle uses.
//
// Register writes take effect immediately. update() then renders one emulated frame's worth of
// samples, clocking the frame sequencer (envelope/linear counter x4, length/sweep x2) in quarters.

struct apupulse
{
 byte duty, halt, constvol, vol;               // $4000
 byte sweepen, sweepperiod, sweepneg, sweepshift, sweepreload, sweepdiv;   // $4001
 int  timer;                                   // 11-bit period
 int  len;                                     // length counter
 byte envstart, envdiv, envdecay;
 byte seq;                                     // position in the 8-step duty sequence
 double ctr;                                   // CPU cycles until the next sequencer step
 byte ones;                                    // pulse 1 negates with ones' complement
 int  target() const;
 int  out() const;
};

struct apuTriangle
{
 byte control, linearload, linear, reloadflag;
 int  timer, len;
 byte seq;
 double ctr;
};

struct apunoise
{
 byte halt, constvol, vol, mode;
 int  period, len;
 byte envstart, envdiv, envdecay;
 unsigned short shift;
 double ctr;
};

struct apudmc
{
 byte irqen, loop, level;
 int  period;
 unsigned short addr, startaddr;
 int  bytesleft, startlen;
 byte buf, bufempty, shiftreg, bitsleft, silence, irq;
 double ctr;
};

class nessound
{
 apupulse  sq[2];
 apuTriangle tri;
 apunoise  noi;
 apudmc    dmc;
 double hpx, hpy;                              // DC-blocking filter state
 byte   hpprimed;
 byte   seqstep;                               // frame sequencer position (half-frame clocks on odd steps)

 void quarterframe();
 void halfframe();
 void dmcrestart();
 void dmcfetch();
 double dmcstep(double cycles);                // advance the sample channel, returns its mean level

 public:
 byte enablebits;                              // last value written to $4015
 void write(byte a,byte d);                    // register $4000+a
 void setenable(byte d);                       // write to $4015
 byte readstatus();                            // read of $4015
 void reset();
 void print();
 nessound();
 ~nessound();

 //most recently rendered block (also shown by the wave output window)
 static short mixingbuf[4096];

 //called once per emulated frame (timer tick): renders and outputs that frame's samples
 void update(int nums);
 void mix16(short *buf, int nums);             //render nums samples (mono, signed 16 bit)
};

extern nessound *ns;
extern byte soundenabled; //is sound enabled ?
void disablesound();
void enablesound();
void togglesound();

#endif
