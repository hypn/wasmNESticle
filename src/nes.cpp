//emulation of NES registers....
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <process.h>

#include "types.h"

#include "message.h"
#include "file.h"

#include "keyb.h"

#define FREE(x) if (x) {free(x); x=0;}
#define DELETE(x) if (x) {delete x; x=0;}

#include "m6502/m6502.h"
#include "timing.h"

#include "nesvideo.h"
#include "nes.h"
#include "slist.h"
#include "mmc.h"

#include "input.h"
#include "dd.h"

#include "r2img.h"
#include "font.h"

#include "nessound.h"
#include "timing.h"

extern int blah,blah2;

WORD NESread(word a);
int NESwrite(word a,byte d);


//---------------------------------
//hardware internal registers
//The PPU's scroll/address registers, as on the real chip ("loopy" registers). One 15-bit address
//format serves both VRAM access and scrolling:  0yyy NN YYYYY XXXXX = fine Y, nametable, coarse Y, coarse X.
word ppumemaddr; //"v": current address - the VRAM address for $2007, and the scroll position while drawing
word ppu_t;      //"t": temporary address; $2000/$2005/$2006 writes go here and are copied to v
byte ppu_x;      //fine X scroll (0-7)
char ppuw;       //write toggle shared by $2005 and $2006 (reset by reading $2002)
byte ppu_nty;    //nametable-Y bit of the address the picture is currently drawn from
char ppureadfuckup;  //first mem fuck up
byte spritememaddr; //next write in sprite memory

volatile byte vblank; //are we in a vblank ?
volatile byte hitflag; //are we in hitflag?
volatile byte frame; //are we scanning frame?

memorymapper *mmc; //current memory mapper

//-------------------------
//reset nes

//create data structures for nes hardware
void initNEShardware()
{
 if (NESinitialized) return;
 ppu=new NES_ppumemory;

 ram=(byte *)malloc(0x10000);
 memset(ram,0,0x10000);

 spritemem=(NES_sprite *)malloc(256);

 //set default timing
 HBLANKCYCLES=115;
 VBLANKLINES=33;

 mmc=newmemorymapper(romhdr.banktype); //create memorymapper

 NESinitialized=1;
}

//free data structures for nes hardware
void terminateNEShardware()
{
 if (!NESinitialized) return;
 m_stop();
 DELETE(ppu);
 DELETE(mmc);
 FREE(ram);
 FREE(spritemem);
 NESinitialized=0;
}

int resetNEShardware()
{
 if (!NESinitialized) return -1;
 if (!numrom) {msg.error("No rom data!"); return -1;}

 ppuw=0; ppu_t=0; ppu_x=0; ppu_nty=0;
 ppumemaddr=0; ppureadfuckup=0;
 spritememaddr=0;

 //reset nes sound
 ns->reset();

 //erase memory
 ppu->clear();
 memset(ram,0,0x10000);
 memset(spritemem,0,256);

 memset(ram+0x8000,0x1,0x8000); //invalid instruction
 memset(ram+0x800,0x1,0x2000-0x800); //invalid instruction

// if (expansion) memcpy(ram+0x5000,expansion,0x1000);

 //setup trainer (if it exists)
 if (trainer)
   memcpy(ram+0x7000,trainer,512); //copy trainer

 //copy battery backed ram (if it exists)
 if (batterymem) batterymem->copytoRAM();

 //set base of ram
 m6502ram=(uchar *)ram; //set base of emu

 //reset memory mapper (should set up m6502rom
 mmc->reset();

 //reset 6502 CPU emulator
 m6502reset();   //reset cpu

 //set r/w trap handlers
 m6502MemoryRead=NESread;
 m6502MemoryWrite=NESwrite;

 //reset nes hardware to initial settings
 nv->clear();
 
 CPUpaused=0;
 msg.printf(1,"NES hardware reset");
 return 0;
}


//--------------------------------
//PPU emulation

NES_ppumemory::NES_ppumemory()
{
 clear();
// msg.printf(2,"%dK PPU address space created",sizeof(*this)/1024);
}

void NES_ppumemory::clear() {memset(this,0,sizeof(*this));}

void NES_ppumemory::write(word a,byte d)
{
 //pallete write ?
 if (a>=0x3F00 && a<0x3F20)
  {
//   msg.printf(1,"pal[%X]=%X",a-0x3f00,d);
   if (((byte *)this)[a]==d) return; //no change in palette
   if (!(a&0xF)) //it's a background palette...
    {
     for (int i=0; i<8; i++)
      {
       ((byte *)this)[0x3F00+i*4]=d;
        nv->palupdateidx[i*4]=1;
      }
     nv->paletteupdated=1;
    } else       //normal palette
    if (a&3)
    {
     ((byte *)this)[a]=d; //write it then...
     nv->palupdateidx[a-0x3F00]=1;
     nv->paletteupdated=1;
    }
   return;
  }

 //pattern table was written to ?
 if (a<0x2000)
 {
  if (numvrom)
   {
//    msg.printf(1,"PPU[%X]=%X",a,d);
    return; //it's rom dude
   }

  if (((byte *)this)[a]==d) return; //no change in
  ((byte *)this)[a]=d; //write byte to pattern table
  nv->pt1kupdated[a/0x400]=1;
//  nv->ptn[a/0x1000]->update((a&0xFFF)/sizeof(NES_pattern));
//  msg.printf(1,"pattern %d.%d updated",a/0x1000,(a&0xFFF)/sizeof(NES_pattern));
  return;
 }

 //name/attribute table was written to..
 if (a<0x3000)
  {
   nv->ntc[(a-0x2000)/0x400]->write(a&0x3FF,d); //write to nametable cache

//   if (kbstat&KB_CTRL)
//    if (a<0x2000+15*32) msg.printf(1,"ppu data write %X %X",ppumemaddr,d);
   return;
  }
}




//-----------------------------------
//byte acceptna=0;


//Tells the video layer where the picture is scrolled to from now on, given a PPU address v. The picture
//uses v's coarse X, fine Y, coarse Y and nametable, plus the fine X register. firstrow is the first frame
//row that is drawn with that address (-STARTFRAME for the top of the frame); delay is passed on to the event.
static void emitscroll(word v,int firstrow,int delay)
{
 nv->sl.addevent(SE_NATABLE,(v>>10)&3,delay);
 nv->sl.addevent(SE_SCROLLX,((v&31)<<3)|ppu_x,delay);
 int coarsey=(v>>5)&31;
 if (coarsey<30)   //rows 30/31 (the attribute bytes) cannot be shown as a scroll position
   nv->sl.addevent(SE_SCROLLY,coarsey*8+((v>>12)&7)-firstrow-STARTFRAME,delay);
}

#define PPURENDERING (ram[0x2001]&0x18) //background or sprites on: the PPU is using its scroll address

void startframe()
{
 frame=1;     //during rendering of frame
 vblank=0;    //not in vblank
 nv->sl.addevent(SE_FRAMESTART,0);

 //at the end of vblank a drawing PPU loads its address from t: this is where the frame's scroll comes from
 if (PPURENDERING)
  {
   ppumemaddr=ppu_t;
   ppu_nty=(ppu_t>>11)&1;
   emitscroll(ppu_t,-STARTFRAME,0);
  }
}

void startvblank()
{
 hitflag=0;  //reset hitflag
 frame=0;  //not rendering frame
 vblank=1;   //we are in vblank
// acceptna=1;
}

//-------------------------------------------------------
//memory write trap handlers
extern int CPUtickframe;


//write to lower ports
int neswrite2000(WORD a,BYTE d)
{
//   msg.printf(1,"write[%X]=%X",a,d);
  switch (a)
  {
   case 0x2000: //PPU control register 1
     ppu_t=(ppu_t&~0x0C00)|((d&3)<<10);
     //during the frame the picture follows t's nametable-X bit from the next line, but its nametable-Y
     //bit only changes when v is next loaded from t
     if (frame && PPURENDERING)  nv->sl.addevent(SE_NATABLE,(ppu_nty<<1)|((ppu_t>>10)&1),1);

     //set pattern tables
     if ((d&0x8)!=(ram[a]&0x8))  nv->sl.addevent(SE_SPRITEPT,d&0x8);
     if ((d&0x10)!=(ram[a]&0x10))  nv->sl.addevent(SE_BGPT,d&0x10);
     ram[a]=d;
//     if (CPUtickframe) msg.printf(1,"write[%X]=%X",a,d);
     return 0;
   case 0x2001: //PPU control register 2
     //turning the PPU on part way through the frame: it starts drawing from its current address
     if (frame && !PPURENDERING && (d&0x18))
      {
       word v=(ppumemaddr&~0x041F)|(ppu_t&0x041F); //(the horizontal bits are reloaded from t each line)
       ppu_nty=(v>>11)&1;
       emitscroll(v&0x7FFF,getscanline(),1);
      }

     //turn things on/off
     if ((d&0x8)!=(ram[a]&0x8))  nv->sl.addevent(SE_BG,d&0x8);
     if ((d&0x10)!=(ram[a]&0x10))  nv->sl.addevent(SE_SPRITE,d&0x10);
     ram[a]=d;
//    msg.printf(1,"write[%X]=%X line=%d",a,d,getscanline());
     return 0;
   case 0x2002:  return 0;
   case 0x2003: //sprite memory address
      spritememaddr=d;
//      msg.printf(1,"sprite mem addr set to %X",spritememaddr);
      return 0;
   case 0x2004: //sprite mem data write
//      msg.printf(1,"spritewrite %d",spritememaddr);
      ((byte *)spritemem)[spritememaddr++]=d;
      return 0;
   case 0x2005: //Background scroll
      if (!ppuw)
       { //first write: X
        ppu_t=(ppu_t&~0x001F)|(d>>3);
        ppu_x=d&7;
        if (frame && PPURENDERING)  nv->sl.addevent(SE_SCROLLX,((ppu_t&31)<<3)|ppu_x,1); //(used from the next line)
       }
      else
       { //second write: Y. Only t changes, so mid-frame this affects the next frame, as on the real PPU.
        ppu_t=(ppu_t&~0x73E0)|((d&7)<<12)|((d&0xF8)<<2);
       }
      ppuw^=1;
     return 0;

   case 0x2006: //PPU memory address
     if (!ppuw)  ppu_t=(ppu_t&0x00FF)|((d&0x3F)<<8);   //first write: high bits (bit 14 is cleared)
      else
       { //second write: low byte, then v=t
        ppu_t=(ppu_t&0x7F00)|d;
        ppumemaddr=ppu_t;
        //While the PPU is drawing, v is the scroll position too: this is how games split the screen
        //(e.g. a status bar). The new position is first used by the next scanline.
        if (frame && PPURENDERING)
         {
          ppu_nty=(ppumemaddr>>11)&1;
          emitscroll(ppumemaddr,getscanline()+1,1);
         }
       }
     ppuw^=1;
     ppureadfuckup=1;
      return 0;
   case 0x2007: //PPU data write
      ppu->write(ppumemaddr&0x3FFF,d);
      ppumemaddr+=(ram[0x2000]&4) ? 32 : 1;
      ppureadfuckup=0;
      return 0;
   default:
//    msg.printf(1,"unsupported write: %X %X PC=%X",a,d,m6502pc);
    return 0;
  }
};


//-------------------------------------------------------
//memory read trap handlers

WORD nesread2000(WORD a)
{
//  msg.printf(3,"%X read",a);
  switch (a)
  {
   case 0x2000: return 0; //PPU control register 1
//    msg.printf(3,"%X read PC=%X bank=%d %X%X",a,m6502pc,mmc->getbank(),ram[0x27],ram[0x26]);
   case 0x2001:
//    msg.printf(3,"%X read",a);
     return 0; //PPU control register 2
   case 0x2002: //PPU status register
    {
//    if (CPUtickframe)      msg.printf(2,"PPU status read %X",s);
      ppuw=0;
      //(the original also forced the nametable to 0 on every status read during the frame - a workaround for
      // the lack of a scroll model: reading $2002 does not touch the PPU's scroll registers on real hardware)

//      acceptna=1;
      if (vblank) return 0x80;
             //{vblank=0; return 0x80;} //{s|=0x80; vblank=0;} //reset vblank after read
  //      if (vblank) return 0x80; //reset vblank after read

        //see if we've scanned past sprite #0's y coordinate
      if (!hitflag) // && frame)
      {
       int hity=spritemem[0].y-1;
//       int hity2=hity+ ((ram[0x2000]&0x20) ? 16 : 8);

       int line=getscanline();//+STARTFRAME;
//       if (ram[0x2000]&0x20) line+=STARTFRAME;
//       if (line>=hity && line<hity2) return 0x40;
       if (hity <= line) return 0x40;
//        {hitflag=1; return 0x40;}

//       msg.printf(2,"hit! line=%d hitline=%d",getscanline(),spritemem[0].y-1);
      }
     return 0;
    }

   case 0x2004: //sprite mem data read
//      msg.printf(1,"spriteread %d",spritememaddr);
      return ((byte *)spritemem)[spritememaddr++];
   case 0x2005: return 0; //msg.error("bgscroll read!");  return 0;
   case 0x2007: //PPU data read
    {
//      msg.printf(2,"PPU data read %X",ppumemaddr);
      byte d=mmc->ppuread(ppumemaddr&0x3FFF); //use memory mapper
      if (ppureadfuckup) {ppureadfuckup=0; return d;}
      ppumemaddr+=(ram[0x2000]&4) ? 32 : 1;
      return d;
     }
   default:
//    msg.printf(3,"%X read",a);
    return ram[a];
  }
}






//write to upper ports
int neswrite4000(WORD a,BYTE d)
{
 switch (a)
 {
  case 0x4014: //DMA sprite copy
//    msg.printf(3,"DMA sprite copy from %X at %d",d*0x100,getscanline());
    {
     unsigned addr=d*0x100;
     if (addr>=0x8000)  memcpy(spritemem,m6502rom+addr,0x100); //copy memory
      else
     if (addr>=0x2000)  memcpy(spritemem,ram+addr,0x100); //copy memory
                  else  memcpy(spritemem,ram+(addr&0x7FF),0x100); //copy memory
     return 0;
    }
  case 0x4015: //sound switch
     ns->setenable(d);
    return 0;
  case 0x4016: //joystick
  case 0x4017: //joystick
     ram[0x4016]=1; //reset joystick
     ram[0x4017]=1; //reset joystick
    return 0;
  default:
    if (a>=0x4000 && a<=0x4013) //is it a sound register?
       ns->write((byte)(a-0x4000),d); //send to sound layer
//    else msg.error("unsupported write: %X %X",a,d); //unknown
   return 0;
 };
}



WORD nesread4000(WORD a)
{
// msg.error("upper port read!!");
 switch (a)
 {
  case 0x4015: return ns ? ns->readstatus() : 0; //sound channel status
  case 0x4016: //joystick
  case 0x4017:
   {
    int joynum=a-0x4016; //get joystick number
    int stat=inputdevice[joynum]->stat; //get current status of dirs
    int but=inputdevice[joynum]->but; //get current status of buts
    if (keydown[KB_ENTER]) but|=ID_BUT3;
    if (keydown[KB_TAB]) but|=ID_BUT2;

    switch (ram[a]++)
    {
     case 1: return (but&ID_BUT1) ? 1 : 0; //A
     case 2: return (but&ID_BUT0) ? 1 : 0; //B
     case 3: return (but&ID_BUT2) ? 1 : 0; //Select
     case 4: return (but&ID_BUT3) ? 1 : 0; //Start
     case 5: return (stat&ID_UP ) ? 1 : 0; //
     case 6: return (stat&ID_DOWN) ? 1 : 0; //
     case 7: return (stat&ID_LEFT) ? 1 : 0; //
     case 8: return (stat&ID_RIGHT) ? 1 : 0; //
    }
   }
 }
 return 0;
}


//-------------------------

//inline int neswrite5000(WORD a,BYTE d){ram[a]=d; return 0;}
inline int neswrite6000(WORD a,BYTE d){ram[a]=d; return 0;}

//inline WORD nesread5000(WORD a) {return ram[a];}
inline WORD nesread6000(WORD a) {return ram[a];}



//------------------------------------
//main read and write handlers


WORD _fastcall NESread(word a)
{
// if (a<0x2000) return ram[a&0x7FF]; //lower ram
 if (a<0x4000) return nesread2000(a); //lower ports
 if (a<0x5000) return nesread4000(a); //higher ports
 if (a>=0x6000) return nesread6000(a); //battery mem
 if (a>=0x5000) return mmc->read5000(a); //expansion
 return 0;
}


int _fastcall NESwrite(word a,byte d)
{
 if (a<0x4000) return neswrite2000(a,d); //lower ports
 if (a<0x5000) return neswrite4000(a,d); //higher ports
 if (a>=0x8000) return mmc->write(a,d); //redirect to memory mapper
 if (a>=0x6000) return neswrite6000(a,d); //battery backed mem
 if (a>=0x5000) return mmc->write5000(a,d); //expansion
 return 0; //unsupported
}














