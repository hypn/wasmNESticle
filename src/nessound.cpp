#include <stdlib.h>
#include <string.h>

#include "nessound.h"
#include "message.h"

#include "r2img.h"
#include "font.h"
#include "dd.h"

#include "nes.h"

#include "sound.h"

#include "timing.h"

#include "prof.h"

#include "m6502.h"

//primary output functions (platform layer)
void clearprimary();
void primaryoutput(short *buf,int nums);
void primaryskip(int nums);

extern byte soundinstalled;

//global nessound device
nessound *ns;

//---------------------------------
//sound enable stuff
byte soundenabled=1;
void disablesound()
{
 if (!soundenabled) return;
 clearprimary(); //clear the output buffer
 msg.printf(1,"Sound disabled");
 soundenabled=0;
}

void enablesound()
{
 if (soundenabled) return;
 if (!soundinstalled) {msg.error("Sound not initialized"); return;}
 soundenabled=1;
 msg.printf(1,"Sound enabled");
}

void togglesound()
{
 if (soundenabled) disablesound(); else enablesound();
}

//============================================
// tables

#define CPUHZ 1789773.0 //NTSC 2A03 clock

static const byte lentab[32]=
 {10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,
  12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30};

static const byte dutyseq[4][8]=
 {{0,1,0,0,0,0,0,0},{0,1,1,0,0,0,0,0},{0,1,1,1,1,0,0,0},{1,0,0,1,1,1,1,1}};

static const int noiseperiod[16]=
 {4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068};

static const int dmcperiod[16]=
 {428,380,340,320,286,254,226,214,190,160,142,128,106,84,72,54};

//============================================
// envelope shared by the pulse and noise channels
template<class T> static void clockenvelope(T &c)
{
 if (c.envstart) {c.envstart=0; c.envdecay=15; c.envdiv=c.vol; return;}
 if (c.envdiv==0)
  {
   c.envdiv=c.vol;
   if (c.envdecay>0) c.envdecay--; else if (c.halt) c.envdecay=15;
  } else c.envdiv--;
}

//============================================
// pulse

int apupulse::target() const
{
 int change=timer>>sweepshift;
 if (sweepneg) return timer-change-(ones ? 1 : 0);
 return timer+change;
}

int apupulse::out() const
{
 if (len==0 || timer<8 || target()>0x7FF || !dutyseq[duty][seq]) return 0;
 return constvol ? vol : envdecay;
}

//============================================
// chip

short nessound::mixingbuf[4096];

nessound::nessound()
{
 reset();
 clearprimary();
}

nessound::~nessound()
{
 clearprimary();
}

void nessound::reset()
{
 memset(sq,0,sizeof(sq));
 memset(&tri,0,sizeof(tri));
 memset(&noi,0,sizeof(noi));
 memset(&dmc,0,sizeof(dmc));
 sq[0].ones=1;
 noi.shift=1; noi.period=noiseperiod[0];
 dmc.period=dmcperiod[0]; dmc.bufempty=1; dmc.silence=1; dmc.bitsleft=8;
 enablebits=0;
 seqstep=0;
 hpx=hpy=0; hpprimed=0;
 memset(mixingbuf,0,sizeof(mixingbuf));
}

//decode enable bit for each channel; a disabled channel's length counter is cleared
void nessound::setenable(byte d)
{
 enablebits=d;
 if (!(d&1)) sq[0].len=0;
 if (!(d&2)) sq[1].len=0;
 if (!(d&4)) tri.len=0;
 if (!(d&8)) noi.len=0;
 dmc.irq=0;
 if (!(d&16)) dmc.bytesleft=0;
 else if (dmc.bytesleft==0) dmcrestart();
}

byte nessound::readstatus()
{
 return (sq[0].len>0 ? 1 : 0) | (sq[1].len>0 ? 2 : 0) | (tri.len>0 ? 4 : 0) | (noi.len>0 ? 8 : 0)
      | (dmc.bytesleft>0 ? 16 : 0) | (dmc.irq ? 0x80 : 0);
}

//a = register number, 0-0x13 ($4000-$4013)
void nessound::write(byte a,byte d)
{
 if (a<8)
  {
   apupulse &p=sq[a>>2];
   switch (a&3)
    {
     case 0: p.duty=d>>6; p.halt=(d>>5)&1; p.constvol=(d>>4)&1; p.vol=d&15; break;
     case 1: p.sweepen=d>>7; p.sweepperiod=(d>>4)&7; p.sweepneg=(d>>3)&1; p.sweepshift=d&7; p.sweepreload=1; break;
     case 2: p.timer=(p.timer&0x700)|d; break;
     case 3:
      p.timer=(p.timer&0xFF)|((d&7)<<8);
      if (enablebits&(1<<(a>>2))) p.len=lentab[d>>3];
      p.seq=0; p.envstart=1;
      break;
    }
   return;
  }
 switch (a)
  {
   case 8:  tri.control=d>>7; tri.linearload=d&0x7F; break;
   case 10: tri.timer=(tri.timer&0x700)|d; break;
   case 11:
    tri.timer=(tri.timer&0xFF)|((d&7)<<8);
    if (enablebits&4) tri.len=lentab[d>>3];
    tri.reloadflag=1;
    break;

   case 12: noi.halt=(d>>5)&1; noi.constvol=(d>>4)&1; noi.vol=d&15; break;
   case 14: noi.mode=d>>7; noi.period=noiseperiod[d&15]; break;
   case 15:
    if (enablebits&8) noi.len=lentab[d>>3];
    noi.envstart=1;
    break;

   case 16: dmc.irqen=d>>7; dmc.loop=(d>>6)&1; dmc.period=dmcperiod[d&15]; if (!dmc.irqen) dmc.irq=0; break;
   case 17: dmc.level=d&0x7F; break;
   case 18: dmc.startaddr=0xC000+d*64; break;
   case 19: dmc.startlen=d*16+1; break;
  }
}

//print all channels
void nessound::print()
{
}

//------
//frame sequencer

void nessound::quarterframe()
{
 clockenvelope(sq[0]); clockenvelope(sq[1]); clockenvelope(noi);
 if (tri.reloadflag) tri.linear=tri.linearload; else if (tri.linear>0) tri.linear--;
 if (!tri.control) tri.reloadflag=0;
}

void nessound::halfframe()
{
 for (int i=0; i<2; i++)
  {
   apupulse &p=sq[i];
   if (!p.halt && p.len>0) p.len--;
   if (p.sweepdiv==0 && p.sweepen && p.sweepshift && p.timer>=8 && p.target()<=0x7FF) p.timer=p.target();
   if (p.sweepdiv==0 || p.sweepreload) {p.sweepdiv=p.sweepperiod; p.sweepreload=0;} else p.sweepdiv--;
  }
 if (!tri.control && tri.len>0) tri.len--;
 if (!noi.halt && noi.len>0) noi.len--;
}

//------
//delta modulation channel

static inline byte cpuread(word a) {return a>=0x8000 ? m6502rom[a] : ram[a];}

void nessound::dmcrestart()
{
 dmc.addr=dmc.startaddr; dmc.bytesleft=dmc.startlen;
 dmcfetch();
}

void nessound::dmcfetch()
{
 if (!dmc.bufempty || dmc.bytesleft<=0) return;
 dmc.buf=cpuread(dmc.addr); dmc.bufempty=0;
 dmc.addr=(dmc.addr==0xFFFF) ? 0x8000 : dmc.addr+1;
 if (--dmc.bytesleft==0)
  {
   if (dmc.loop) {dmc.addr=dmc.startaddr; dmc.bytesleft=dmc.startlen;}
   else if (dmc.irqen) dmc.irq=1;
  }
}

//advances the dmc by cycles CPU cycles, returning the mean output level
double nessound::dmcstep(double cycles)
{
 apudmc &d=dmc;
 double acc=0, left=cycles;
 while (left>0)
  {
   if (d.ctr<=0) d.ctr=d.period;
   double run=left<d.ctr ? left : d.ctr;
   acc+=d.level*run; d.ctr-=run; left-=run;
   if (d.ctr<=0)
    {
     if (!d.silence)
      {
       if (d.shiftreg&1) {if (d.level<=125) d.level+=2;} else {if (d.level>=2) d.level-=2;}
       d.shiftreg>>=1;
      }
     if (--d.bitsleft==0)
      {
       d.bitsleft=8;
       if (d.bufempty) d.silence=1;
       else {d.silence=0; d.shiftreg=d.buf; d.bufempty=1; dmcfetch();}
      }
    }
  }
 return acc/cycles;
}

//------
//mixing

//renders nums mono samples. Timers run in CPU cycles; each channel's output is averaged over the
//CPU cycles that make up one sample (a box filter, which keeps the square edges from aliasing).
void nessound::mix16(short *b,int nums)
{
 if (!CPURunning) {memset(b,0,nums*2); return;}

 const double cps=CPUHZ/SOUNDRATE; //CPU cycles per output sample
 int step=1; //next frame-sequencer boundary: sample nums*step/4

 for (int i=0; i<nums; i++)
  {
   //frame sequencer: 4 steps per emulated frame (envelopes and the triangle's linear counter every
   //step; length counters and sweeps on every second one)
   if (step<4 && i==nums*step/4) {quarterframe(); if (seqstep&1) halfframe(); seqstep++; step++;}

   double s[5]={0,0,0,0,0};
   for (int c=0; c<2; c++) //pulse
    {
     apupulse &p=sq[c];
     double left=cps, acc=0;
     const int period=(p.timer+1)*2;
     while (left>0)
      {
       if (p.ctr<=0) p.ctr=period;
       double run=left<p.ctr ? left : p.ctr;
       acc+=p.out()*run; p.ctr-=run; left-=run;
       if (p.ctr<=0) p.seq=(p.seq+1)&7;
      }
     s[c]=acc/cps;
    }
   { //triangle
    double left=cps, acc=0;
    const int period=tri.timer+1;
    while (left>0)
     {
      if (tri.ctr<=0) tri.ctr=period;
      double run=left<tri.ctr ? left : tri.ctr;
      int v=tri.seq<16 ? 15-tri.seq : tri.seq-16;
      //(a halted triangle holds its current step; ultrasonic periods average to mid-scale)
      acc+=((tri.timer<2 && tri.len>0 && tri.linear>0) ? 7.5 : v)*run; tri.ctr-=run; left-=run;
      if (tri.ctr<=0 && tri.len>0 && tri.linear>0) tri.seq=(tri.seq+1)&31;
     }
    s[2]=acc/cps;
   }
   { //noise
    double left=cps, acc=0;
    while (left>0)
     {
      if (noi.ctr<=0) noi.ctr=noi.period;
      double run=left<noi.ctr ? left : noi.ctr;
      int v=(noi.len>0 && !(noi.shift&1)) ? (noi.constvol ? noi.vol : noi.envdecay) : 0;
      acc+=v*run; noi.ctr-=run; left-=run;
      if (noi.ctr<=0)
       {
        int fb=(noi.shift&1)^((noi.shift>>(noi.mode ? 6 : 1))&1);
        noi.shift=(noi.shift>>1)|(fb<<14);
       }
     }
    s[3]=acc/cps;
   }
   s[4]=dmcstep(cps);

   //the chip's non-linear mixer
   double p=s[0]+s[1];
   double pulse=p>0 ? 95.88/(8128.0/p+100.0) : 0;
   double tnd=s[2]/8227.0+s[3]/12241.0+s[4]/22638.0;
   double mixed=pulse+(tnd>0 ? 159.79/(1.0/tnd+100.0) : 0);

   //DC blocker (about 35Hz high-pass), then to 16 bit
   if (!hpprimed) {hpx=mixed; hpy=0; hpprimed=1;} //start at the idle level: no step at power-on
   hpy=mixed-hpx+0.995*hpy; hpx=mixed;
   double o=hpy*44000.0; //the chip's full-scale mix is +-0.5 after the DC blocker: no clipping even at maximum
   b[i]=(short)(o>32767 ? 32767 : o<-32768 ? -32768 : o);
  }
 //the last sequencer step of the frame lands at its end
 quarterframe(); if (seqstep&1) halfframe(); seqstep++;
}

//renders one frame worth of samples and sends them to the platform layer
void nessound::update(int nums)
{
 if (!soundinstalled) return;
 if (nums>4096) nums=4096;
 if (!soundenabled) {primaryskip(nums); return;}
 pf.sound_timer.enter();

 mix16(mixingbuf,nums);
 primaryoutput(mixingbuf,nums); //output it to primary buffer
 pf.sound_timer.leave();
}
