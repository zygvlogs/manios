#!/usr/bin/env python3
"""SHLD/SHRD defined flags and counts, with a native x86 oracle and UBSan."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: bits=%u right=%u count=%u form=%u %s\n",__LINE__,bits,right,count,form,#x);exit(1);}}while(0)
static uint32_t native(unsigned bits,unsigned right,uint32_t a,uint32_t b,unsigned count,unsigned *out){
 unsigned long f,initial=0xad7;
 #define ASM(insn,sz,reg,src) __asm__ volatile("pushq %4; popfq; " insn sz " %%cl," src "," reg "; pushfq; popq %1" : "+&a"(a),"=&r"(f) : "d"(b),"c"(count),"r"(initial) : "cc")
 if(bits==16){if(right){ASM("shrd","w","%%ax","%%dx");}else{ASM("shld","w","%%ax","%%dx");}}
 else{if(right){ASM("shrd","l","%%eax","%%edx");}else{ASM("shld","l","%%eax","%%edx");}}
 *out=f;return a;
}
int main(void){
 struct cpu c;cpu_init(&c);c.sreg[1]=0x100;c.sreg[3]=0x200;
 unsigned cases=0;
 for(unsigned bits=16;bits<=32;bits*=2)for(unsigned right=0;right<2;right++)for(unsigned count=0;count<256;count++)for(unsigned form=0;form<4;form++)for(unsigned e=0;e<16;e++){
  uint32_t a=0x91234567u*e,b=0x89abcdefu^(e*0x1234567u);unsigned nf;
  uint32_t expect=native(bits,right,a,b,count,&nf);
  unsigned char code[9];unsigned n=0;if(bits==32)code[n++]=0x66;
  code[n++]=0x0f;code[n++]=(right?0xac:0xa4)+(form&1);code[n++]=form>=2?0x16:0xd0;
  if(form>=2){code[n++]=0;code[n++]=8;mem_w16(&c,0x2800,a);mem_w16(&c,0x2802,a>>16);}
  if(!(form&1))code[n++]=count;
  memcpy(c.mem+0x1000,code,n);c.ip=0;c.flags=0xad7;c.r[CX]=count;cpu_put_reg32(&c,AX,a);cpu_put_reg32(&c,DX,b);
  CHECK(cpu_step(&c)==CPU_OK);CHECK(c.ip==n);
  /* Counts above 16 for word operands have architecturally undefined results;
   * still execute them under UBSan to catch negative C shifts. */
  if(bits==16&&(count&31)>16)continue;
  uint32_t got=form>=2?(uint32_t)mem_r16(&c,0x2800)|((uint32_t)mem_r16(&c,0x2802)<<16):cpu_reg32(&c,AX);
  CHECK(got==expect);
  unsigned mask=0xed5;if(count&31){mask&=~FLAG_AF;if((count&31)!=1)mask&=~FLAG_OF;}
  CHECK((c.flags&mask)==(nf&mask));cases++;
 }
 cpu_free(&c);printf("PASS: %u defined native double-shift cases; undefined counts free of C undefined behavior\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-double-shift-') as tmp:
 src=Path(tmp)/'test.c';src.write_text(HARNESS);exe=Path(tmp)/'test'
 subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie','-I',str(ROOT/'userland/dosrun'),str(src),str(ROOT/'userland/dosrun/cpu.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
