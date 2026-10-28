#!/usr/bin/env python3
"""Real-mode PUSHFD/POPFD CPU identification, flags and stack regressions."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static struct cpu c;
static void code(const unsigned char *p,size_t n){c.ip=0;memcpy(c.mem+0x1000,p,n);}
#define CODE(...) do{const unsigned char p[]={__VA_ARGS__};code(p,sizeof(p));}while(0)
static void setflags(uint32_t f){cpu_put_reg32(&c,AX,f);CODE(0x66,0x50,0x66,0x9d);CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_step(&c)==CPU_OK);}
static uint32_t getflags(void){CODE(0x66,0x9c,0x66,0x58);CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_step(&c)==CPU_OK);return cpu_reg32(&c,AX);}
int main(void){
 cpu_init(&c);c.sreg[1]=0x100;c.sreg[2]=0x300;c.r[SP]=0x8000;c.r_hi[SP]=0x1234;
 setflags(0xffffffff);CHECK(getflags()==0x247fd7);CHECK(cpu_reg32(&c,SP)==0x12348000);
 setflags(0);CHECK(getflags()==2);
 /* AC/ID toggling used by DOS extenders' CPU detection. */
 for(unsigned f=0;f<4;f++){unsigned flags=(f&1?0x40000:0)|(f&2?0x200000:0)|0x202;setflags(flags);CHECK(getflags()==flags);
  CODE(0x04,1);CHECK(cpu_step(&c)==CPU_OK);CHECK((getflags()&0x240000)==(flags&0x240000));
  /* POPF and an interrupt/IRET roundtrip preserve the high flags. */
  push16(&c,0xffff);CODE(0x9d);CHECK(cpu_step(&c)==CPU_OK);CHECK(getflags()==((flags&0x240000)|0x7fd7));
  uint16_t cs,ip;cpu_enter_interrupt(&c,0x30,&cs,&ip);CODE(0xcf);CHECK(cpu_step(&c)==CPU_IRET);CHECK((getflags()&0x240000)==(flags&0x240000));
 }
 /* No partial stack writes or flags changes across a segment limit. */

 c.r[SP]=0x8000;setflags(0x240202);
 for(unsigned sp=1;sp<4;sp++){
  c.r[SP]=sp;CODE(0x66,0x9c);CHECK(cpu_step(&c)==CPU_FAULT&&c.ip==0&&c.r[SP]==sp&&c.flags==0x202);
  c.r[SP]=0xfffc+sp;CODE(0x66,0x9d);CHECK(cpu_step(&c)==CPU_FAULT&&c.ip==0&&c.r[SP]==0xfffc+sp&&c.flags==0x202);
 }
 c.r[SP]=0;CODE(0x66,0x9c,0x66,0x9d);CHECK(cpu_step(&c)==CPU_OK&&c.r[SP]==0xfffc);CHECK(cpu_step(&c)==CPU_OK&&c.r[SP]==0);
 c.r[SP]=0x8000;CHECK(getflags()==0x240202);

 cpu_free(&c);puts("PASS: flags masks, AC/ID probes, mixed-width flags and interrupt lifetime");
}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-flags32-') as tmp:
 src=Path(tmp)/'test.c';src.write_text(HARNESS);exe=Path(tmp)/'test'
 subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I',str(ROOT/'userland/dosrun'),str(src),str(ROOT/'userland/dosrun/cpu.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
