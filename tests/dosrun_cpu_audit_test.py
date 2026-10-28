#!/usr/bin/env python3
"""CMPXCHG width/flags, unsupported system state, and timestamp registers."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){struct cpu c;cpu_init(&c);c.sreg[1]=0x100;c.sreg[2]=0x300;c.sreg[3]=0x200;c.r[SP]=0x8000;
 unsigned edge[]={0,1,0x7f,0x80,0xff,0x1201,0x3401,0x8000,0x7fff,0xffff};
 for(unsigned word=0;word<2;word++)for(unsigned i=0;i<100;i++)for(unsigned memory=0;memory<2;memory++){
  uint32_t a=edge[i/10],b=edge[i%10],result=a;unsigned long flags;
  if(word)__asm__ volatile("subw %w2,%w0; pushfq; popq %1":"+&r"(result),"=r"(flags):"r"(b):"cc");
  else __asm__ volatile("subb %b2,%b0; pushfq; popq %1":"+&q"(result),"=r"(flags):"q"(b):"cc");
  unsigned char p[]={0x0f,0xb0+word,memory?0x16:0xd3,0,8};memcpy(c.mem+0x1000,p,sizeof(p));c.ip=0;c.flags=0x202;
  cpu_put_reg32(&c,AX,0xabcd0000|a);cpu_put_reg32(&c,BX,0x12340000|b);cpu_put_reg32(&c,DX,0x11225678);mem_w16(&c,0x2800,b);
  CHECK(cpu_step(&c)==CPU_OK);unsigned mask=word?0xffff:0xff;int equal=(a&mask)==(b&mask);
  CHECK(cpu_reg32(&c,AX)==(0xabcd0000|(equal?a:(a&~mask)|(b&mask))));
  CHECK((memory?mem_r16(&c,0x2800):c.r[BX])==(equal?(b&~mask)|(0x5678&mask):b));
  CHECK((c.flags&0x8d5)==(flags&0x8d5));CHECK(c.flags&FLAG_IF);
 }
 /* Unmodeled FS/GS and control/MSR state must never overwrite SS/flags. */
 unsigned unsupported[]={0xa0,0xa1,0xa8,0xa9,0x06,0x30};
 for(unsigned i=0;i<sizeof(unsupported)/sizeof(unsupported[0]);i++){
  c.ip=0;c.flags=0x203;c.r[SP]=0x8000;c.sreg[2]=0x300;wr16(&c,0x300,0x8000,0xdead);c.mem[0x1000]=0x0f;c.mem[0x1001]=unsupported[i];
  CHECK(cpu_step(&c)==CPU_FAULT);CHECK(c.flags==0x203&&c.r[SP]==0x8000&&c.sreg[2]==0x300);
 }
 c.ip=0;c.insns=0x12345677;c.mem[0x1000]=0x0f;c.mem[0x1001]=0x31;
 cpu_put_reg32(&c,AX,0xffffffff);cpu_put_reg32(&c,DX,0xffffffff);cpu_put_reg32(&c,CX,0x11223344);cpu_put_reg32(&c,BX,0xaabbccdd);
 CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_reg32(&c,AX)==0x12345678&&cpu_reg32(&c,DX)==0);CHECK(cpu_reg32(&c,CX)==0x11223344&&cpu_reg32(&c,BX)==0xaabbccdd);
 cpu_free(&c);puts("PASS: 400 CMPXCHG width/flag cases; unsupported system operations; full timestamp registers");}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-audit-') as tmp:
 src=Path(tmp)/'test.c';src.write_text(HARNESS);exe=Path(tmp)/'test'
 subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-fsanitize=address,undefined','-no-pie','-I',str(ROOT/'userland/dosrun'),str(src),str(ROOT/'userland/dosrun/cpu.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
