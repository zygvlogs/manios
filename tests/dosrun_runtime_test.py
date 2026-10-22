#!/usr/bin/env python3
"""CPU startup, DOS allocation/files and BIOS text state regressions."""
from pathlib import Path
import subprocess
import tempfile
import dosrun_host_test as host

host.HARNESS = r'''
#include "cpu.h"
#include "dosapi.h"
#include "drives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct cpu c;
static struct dos_state *d;
static void init(void) {
 cpu_init(&c); c.sreg[1]=0x1000; c.sreg[2]=0x2000; c.sreg[3]=0x3000;
 c.sreg[0]=0x4000; c.r[SP]=0xFF00; c.flags=0x202;
}
static void instruction(const unsigned char *code,size_t n) { c.ip=0; memcpy(c.mem+0x10000,code,n); }
#define CODE(...) do { unsigned char code[]={__VA_ARGS__}; instruction(code,sizeof(code)); } while(0)
static void call(int vec,int ax) {
 c.r[AX]=ax; push16(&c,c.flags); push16(&c,c.sreg[1]); push16(&c,c.ip);
 CHECK(dos_int(d,&c,vec)==CPU_OK);
 c.ip=pop16(&c); c.sreg[1]=pop16(&c); c.flags=pop16(&c);
}
static void text(uint16_t seg,uint16_t off,const char *s) {
 do { wr8(&c,seg,off++,*s); } while(*s++);
}
int main(void) {
 /* Every addressing form must choose DS or SS from its BP dependency. */
 for(int rm=0;rm<8;rm++) {
  init(); c.r[BX]=0x10;c.r[BP]=0x20;c.r[SI]=3;c.r[DI]=5;
  int offs[]={0x13,0x15,0x23,0x25,3,5,0x20,0x10};
  wr16(&c,0x2000,offs[rm],0x2222);wr16(&c,0x3000,offs[rm],0x3333);
  CODE(0x8B,0x40+rm,0); CHECK(cpu_step(&c)==CPU_OK);
  CHECK(c.r[AX]==((rm==2||rm==3||rm==6)?0x2222:0x3333));cpu_free(&c);
 }
 init(); CODE(0x60,0x61);for(int i=0;i<8;i++)if(i!=SP)c.r[i]=0x100+i;
 CHECK(cpu_step(&c)==CPU_OK);for(int i=0;i<8;i++)if(i!=SP)c.r[i]=0;
 CHECK(cpu_step(&c)==CPU_OK);CHECK(c.r[SP]==0xFF00);
 for(int i=0;i<8;i++)if(i!=SP)CHECK(c.r[i]==0x100+i);cpu_free(&c);
 init();CODE(0x9F,0x9E);c.flags=0xAD7;c.r[AX]=0x1234;
 CHECK(cpu_step(&c)==CPU_OK);CHECK(c.r[AX]==0xD734&&c.flags==0xAD7);
 c.r[AX]=0x0234;CHECK(cpu_step(&c)==CPU_OK);CHECK(c.flags==0xA02&&c.r[AX]==0x0234);cpu_free(&c);
 init();CODE(0x9A,0x10,0,0,0x50);wr8(&c,0x5000,0x10,0xCB);
 CHECK(cpu_step(&c)==CPU_OK);CHECK(c.sreg[1]==0x5000&&c.ip==0x10&&c.r[SP]==0xFEFC);
 CHECK(cpu_step(&c)==CPU_OK);CHECK(c.sreg[1]==0x1000&&c.ip==5&&c.r[SP]==0xFF00);cpu_free(&c);
 init();CODE(0xCF);push16(&c,0xA43);push16(&c,0x1234);push16(&c,0x5678);
 CHECK(cpu_step(&c)==CPU_IRET);CHECK(c.sreg[1]==0x1234&&c.ip==0x5678&&c.flags==0xA43&&c.r[SP]==0xFF00);cpu_free(&c);
 init();CODE(0xD1,0xC0);c.r[AX]=0x8001;CHECK(cpu_step(&c)==CPU_OK);CHECK(c.r[AX]==3&&(c.flags&FLAG_CF));cpu_free(&c);
 init();CODE(0x40,0x48);c.r[AX]=0xF;c.flags|=FLAG_CF;
 CHECK(cpu_step(&c)==CPU_OK);CHECK((c.flags&(FLAG_AF|FLAG_CF))==(FLAG_AF|FLAG_CF));
 CHECK(cpu_step(&c)==CPU_OK);CHECK((c.flags&(FLAG_AF|FLAG_CF))==(FLAG_AF|FLAG_CF));cpu_free(&c);
 for(int a=0;a<100;a++) for(int b=0;b<100;b++) {
  init();CODE(0x04,((b/10)<<4)|(b%10),0x27);c.r[AX]=((a/10)<<4)|(a%10);
  CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_step(&c)==CPU_OK);
  int sum=(a+b)%100;CHECK(c.r[AX]==((sum/10)<<4)|(sum%10));CHECK(!!(c.flags&FLAG_CF)==(a+b>=100));cpu_free(&c);
 }
 puts("PASS: DAA packed-decimal addition for all 10,000 pairs from 00 to 99");
 puts("PASS: addressing segments, POPA, LAHF/SAHF, far CALL, IRET, ROL and INC/DEC flags");
 init();d=dos_new(&c);CHECK(d);dos_set_program(d,0x1000,0x1010);dos_build_psp(d);dos_mount('C',".");
 /* Real elapsed time must advance both direct BDA polls and INT 1Ah.
  * Sub-tick updates retain their fraction, and midnight is read/cleared. */
 dos_clock_advance(d,54);CHECK(mem_r16(&c,0x46C)==0);
 dos_clock_advance(d,1);CHECK(mem_r16(&c,0x46C)==1);
 for(int ms=0;ms<945;ms++)dos_clock_advance(d,1);
 call(0x1A,0);CHECK(c.r[CX]==0&&c.r[DX]==18&&c.r[AX]==0);
 mem_w16(&c,0x46C,0xFFFF);mem_w16(&c,0x46E,0);
 dos_clock_advance(d,1000);call(0x1A,0);
 CHECK(c.r[CX]==1&&c.r[DX]==17&&c.r[AX]==0);
 mem_w16(&c,0x46C,0x00AF);mem_w16(&c,0x46E,0x18);
 dos_clock_advance(d,55);call(0x1A,0);
 CHECK(c.r[CX]==0&&c.r[DX]==0&&c.r[AX]==1&&mem_r8(&c,0x470)==0);
 call(0x1A,0);CHECK(c.r[AX]==0);
 puts("PASS: BIOS ticks, fractional updates, word carry and midnight rollover");
 CHECK(rd8(&c,0x1000,0x50)==0xCD&&rd8(&c,0x1000,0x51)==0x21&&rd8(&c,0x1000,0x52)==0xCB);
 CHECK(rd16(&c,0x1000,0x16)==0xFFFF&&rd8(&c,0x1000,0x18)==0);
 uint16_t env=rd16(&c,0x1000,0x2C);CHECK(env!=0&&rd8(&c,env,0)=='P');
 int end=0;while(end<4094&&(rd8(&c,env,end)||rd8(&c,env,end+1)))end++;
 CHECK(end<4094&&rd16(&c,env,end+2)==1&&rd8(&c,env,end+4)=='C');
 c.sreg[0]=0x1000;c.r[BX]=0x200;call(0x21,0x4A00);CHECK(!(c.flags&FLAG_CF)&&rd16(&c,0x1000,2)==0x1200);
 c.r[BX]=0xFFFF;call(0x21,0x4800);CHECK((c.flags&FLAG_CF)&&c.r[AX]==8&&c.r[BX]==0x8DFF);
 c.r[BX]=0x100;call(0x21,0x4800);CHECK(!(c.flags&FLAG_CF));uint16_t one=c.r[AX];CHECK(one==0x1201);
 c.r[BX]=0x100;call(0x21,0x4800);CHECK(!(c.flags&FLAG_CF));uint16_t two=c.r[AX];CHECK(two==0x1302);
 c.sreg[0]=one;c.r[BX]=0x200;call(0x21,0x4A00);CHECK((c.flags&FLAG_CF)&&c.r[AX]==8&&c.r[BX]==0x100);
 c.sreg[0]=two;call(0x21,0x4900);CHECK(!(c.flags&FLAG_CF));
 c.sreg[0]=one;c.r[BX]=0x200;call(0x21,0x4A00);CHECK(!(c.flags&FLAG_CF)&&rd16(&c,one-1,3)==0x200);
 c.sreg[0]=one;call(0x21,0x4900);CHECK(!(c.flags&FLAG_CF));call(0x21,0x4900);CHECK((c.flags&FLAG_CF)&&c.r[AX]==9);
 c.r[BX]=0x8DFF;call(0x21,0x4800);CHECK(!(c.flags&FLAG_CF)&&c.r[AX]==one);
 c.sreg[0]=0xE000;call(0x21,0x4900);CHECK((c.flags&FLAG_CF)&&c.r[AX]==9);
 puts("PASS: PSP/environment, memory shrink, allocate, exhaustion, growth, coalescing and invalid free");
 c.sreg[3]=0x1500;c.r[DX]=0x100;call(0x21,0x1A00);
 c.sreg[3]=0x1600;call(0x21,0x2F00);CHECK(c.sreg[0]==0x1500&&c.r[BX]==0x100);
 text(0x1600,0x200,"C:/a.txt");c.r[DX]=0x200;c.r[CX]=0;call(0x21,0x4E00);CHECK(!(c.flags&FLAG_CF));
 CHECK(rd16(&c,0x1500,0x11A)==3&&rd8(&c,0x1500,0x11E)=='A');
 c.sreg[3]=0x1700;c.r[DX]=0x100;call(0x21,0x1A00);c.sreg[3]=0x1600;
 text(0x1600,0x200,"C:/B.TXT");c.r[DX]=0x200;c.r[CX]=0;call(0x21,0x4E00);CHECK(!(c.flags&FLAG_CF)&&rd8(&c,0x1700,0x11E)=='B');
 c.sreg[3]=0x1500;c.r[DX]=0x100;call(0x21,0x1A00);call(0x21,0x4F00);CHECK((c.flags&FLAG_CF)&&c.r[AX]==18);
 c.sreg[3]=0x1600;text(0x1600,0x200,"C:/a.txt");c.r[DX]=0x200;call(0x21,0x3D00);CHECK(!(c.flags&FLAG_CF));uint16_t handle=c.r[AX];
 c.r[BX]=handle;call(0x21,0x4500);CHECK(!(c.flags&FLAG_CF));uint16_t copy=c.r[AX];
 c.r[BX]=handle;c.r[CX]=1;c.r[DX]=0x300;call(0x21,0x3F00);CHECK(c.r[AX]==1&&rd8(&c,0x1600,0x300)=='a');
 c.r[BX]=handle;call(0x21,0x3E00);
 c.r[BX]=copy;c.r[CX]=1;c.r[DX]=0x300;call(0x21,0x3F00);CHECK(c.r[AX]==1&&rd8(&c,0x1600,0x300)=='b');
 c.r[BX]=copy;c.r[CX]=12;call(0x21,0x4600);CHECK(!(c.flags&FLAG_CF));c.r[BX]=copy;call(0x21,0x3E00);
 c.r[BX]=12;c.r[CX]=1;c.r[DX]=0x300;call(0x21,0x3F00);CHECK(c.r[AX]==1&&rd8(&c,0x1600,0x300)=='c');
 c.r[BX]=19;call(0x21,0x4500);CHECK((c.flags&FLAG_CF)&&c.r[AX]==6);
 call(0x21,0x714E);CHECK((c.flags&FLAG_CF)&&c.r[AX]==0x7100);
 puts("PASS: DTA segments, independent searches, file sizes, shared duplicate offsets and LFN fallback");
 /* Legacy overlays use record-oriented FCB I/O rather than file handles. */
 c.sreg[3]=0x1600;
 for(int i=0;i<37;i++)wr8(&c,0x1600,0x400+i,0);
 text(0x1600,0x401,"A       TXT");wr8(&c,0x1600,0x400,3);
 c.r[DX]=0x400;call(0x21,0x0F00);CHECK((c.r[AX]&255)==0&&rd16(&c,0x1600,0x410)==3&&rd16(&c,0x1600,0x40E)==128);
 wr16(&c,0x1600,0x40E,2);c.r[CX]=2;call(0x21,0x2700);
 CHECK((c.r[AX]&255)==3&&c.r[CX]==2&&rd16(&c,0x1600,0x421)==2);
 CHECK(rd8(&c,0x1500,0x100)=='a'&&rd8(&c,0x1500,0x102)=='c'&&rd8(&c,0x1500,0x103)==0);
 c.r[CX]=1;call(0x21,0x2700);CHECK((c.r[AX]&255)==1&&c.r[CX]==0);
 wr16(&c,0x1600,0x40C,0);wr8(&c,0x1600,0x420,0);call(0x21,0x1400);
 CHECK((c.r[AX]&255)==0&&rd8(&c,0x1600,0x420)==1&&rd8(&c,0x1500,0x100)=='a');
 wr16(&c,0x1600,0x421,1);call(0x21,0x2100);
 CHECK((c.r[AX]&255)==3&&rd16(&c,0x1600,0x421)==1&&rd8(&c,0x1500,0x100)=='c');
 c.sreg[3]=0x1500;c.r[DX]=0xFFFF;call(0x21,0x1A00);c.sreg[3]=0x1600;c.r[DX]=0x400;c.r[CX]=1;
 call(0x21,0x2700);CHECK((c.r[AX]&255)==2&&c.r[CX]==0);
 call(0x21,0x1000);CHECK((c.r[AX]&255)==0);
 puts("PASS: FCB open, sequential/random/block reads, partial-record padding, EOF and DTA bounds");
 text(0x1600,0x401,"NEW     DAT");c.r[DX]=0x400;call(0x21,0x1600);CHECK((c.r[AX]&255)==0);
 c.sreg[3]=0x1500;c.r[DX]=0x100;call(0x21,0x1A00);c.sreg[3]=0x1600;c.r[DX]=0x400;
 text(0x1500,0x100,"saved");wr16(&c,0x1600,0x40E,1);wr16(&c,0x1600,0x421,0);c.r[CX]=5;
 call(0x21,0x2800);CHECK((c.r[AX]&255)==0&&c.r[CX]==5&&rd16(&c,0x1600,0x410)==5);
 call(0x21,0x1000);CHECK((c.r[AX]&255)==0);call(0x21,0x0F00);CHECK((c.r[AX]&255)==0&&rd16(&c,0x1600,0x410)==5);
 wr16(&c,0x1600,0x40E,1);wr16(&c,0x1600,0x421,0);c.r[CX]=5;call(0x21,0x2700);
 CHECK((c.r[AX]&255)==0&&rd8(&c,0x1500,0x100)=='s'&&rd8(&c,0x1500,0x104)=='d');
 wr16(&c,0x1600,0x421,1);wr8(&c,0x1500,0x100,'A');call(0x21,0x2200);CHECK((c.r[AX]&255)==0);
 c.r[CX]=0;call(0x21,0x2800);CHECK((c.r[AX]&255)==1);
 call(0x21,0x1000);CHECK((c.r[AX]&255)==0);
 puts("PASS: FCB create, block write, save/reopen, random overwrite and explicit truncation failure");
 /* Exhausted FCB slots must not truncate an existing file on create. */
 for(int f=0;f<16;f++) {
  uint16_t off=0x500+f*40;
  for(int i=0;i<37;i++)wr8(&c,0x1600,off+i,0);
  text(0x1600,off+1,"A       TXT");wr8(&c,0x1600,off,3);c.r[DX]=off;
  call(0x21,0x0F00);CHECK((c.r[AX]&255)==0);
 }
 for(int i=0;i<37;i++)wr8(&c,0x1600,0x800+i,0);
 text(0x1600,0x801,"NEW     DAT");wr8(&c,0x1600,0x800,3);c.r[DX]=0x800;
 call(0x21,0x1600);CHECK((c.r[AX]&255)==255);
 for(int f=0;f<16;f++) {c.r[DX]=0x500+f*40;call(0x21,0x1000);CHECK((c.r[AX]&255)==0);}
 puts("PASS: exhausted FCB slots preserve an existing file on failed create");
 int saved_stdin=dup(0), input[2];CHECK(pipe(input)==0);CHECK(dup2(input[0],0)>=0);close(input[0]);
 CHECK(write(input[1],"q",1)==1);
 call(0x16,0x0100);CHECK(!(c.flags&FLAG_ZF)&&c.r[AX]==0x1071);
 call(0x16,0x0100);CHECK(!(c.flags&FLAG_ZF)&&c.r[AX]==0x1071);
 call(0x16,0x0000);CHECK(c.r[AX]==0x1071);
 call(0x16,0x0100);CHECK(c.flags&FLAG_ZF);
 CHECK(write(input[1],"x",1)==1);call(0x21,0x0B00);CHECK((c.r[AX]&255)==255);
 c.r[DX]=255;call(0x21,0x0600);CHECK(!(c.flags&FLAG_ZF)&&(c.r[AX]&255)=='x');
 call(0x21,0x0B00);CHECK((c.r[AX]&255)==0);
 CHECK(write(input[1],"old",3)==3);c.r[DX]=255;call(0x21,0x0C06);CHECK(c.flags&FLAG_ZF);
 CHECK(write(input[1],"\n",1)==1);c.r[DX]=255;call(0x21,0x0600);CHECK((c.r[AX]&255)==13);
 CHECK(dup2(saved_stdin,0)>=0);close(saved_stdin);close(input[1]);
 puts("PASS: nonblocking keyboard readiness, non-consuming BIOS peek, scan code, DOS status and flush");
 c.r[BX]=0x0100;c.r[DX]=0x0203;call(0x10,0x0200);
 c.r[BX]=0x011E;c.r[CX]=3;call(0x10,0x0958);
 CHECK(mem_r16(&c,0xB8000+4096+(2*80+3)*2)==0x1E58);
 call(0x10,0x0300);CHECK(c.r[DX]==0x0203&&c.r[CX]==0x0607);
 call(0x10,0x0800);CHECK(c.r[AX]==0x1E58);
 call(0x10,0x0F00);CHECK(c.r[AX]==0x5003&&(c.r[BX]>>8)==0);
 puts("PASS: BIOS text attributes, repeated output, cursor position, readback and mode queries");
 fflush(stdout);int saved_out=dup(1), capture[2];CHECK(pipe(capture)==0);CHECK(dup2(capture[1],1)>=0);close(capture[1]);
 mem_w16(&c,0xB8000,0x1E58);dos_video_present(d);dos_video_present(d);
 CHECK(dup2(saved_out,1)>=0);close(saved_out);char rendered[256];long size_out=read(capture[0],rendered,sizeof(rendered));close(capture[0]);
 CHECK(size_out>0&&size_out<(long)sizeof(rendered));int xs=0;for(long i=0;i<size_out;i++)if(rendered[i]=='X')xs++;CHECK(xs==1);
 puts("PASS: direct VGA text writes render once; unchanged cells are not redrawn");
 dos_free(d);cpu_free(&c);return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="dosrun-runtime-") as tmp:
    root=Path(tmp); (root/"a.txt").write_bytes(b"abc"); (root/"b.txt").write_bytes(b"xyz")
    exe=host.build(tmp)
    subprocess.run([exe],cwd=tmp,check=True)
    assert (root/"new.dat").read_bytes() == b"sAved"
