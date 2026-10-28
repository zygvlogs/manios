#!/usr/bin/env python3
"""32-bit signed multiply encodings and overflow compared with native x86."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d form %u: %s\n",__LINE__,form,#x);exit(1);}}while(0)
int main(void){struct cpu c;cpu_init(&c);c.sreg[1]=0x100;c.sreg[3]=0x200;
 unsigned edges[]={0,1,0x7fffffff,0x80000000,0xffffffff,0x10000,0xffff8000,0x80},cases=0;
 for(unsigned i=0;i<64;i++)for(unsigned form=0;form<6;form++){
  uint32_t a=edges[i/8],b=edges[i%8];if(form>=4)b=(uint32_t)(int32_t)(int8_t)b;
  uint32_t result=a;unsigned long flags;
  __asm__ volatile("imull %2,%0; pushfq; popq %1" : "+&r"(result),"=r"(flags):"r"(b):"cc");
  unsigned char code[12];unsigned n=0;code[n++]=0x66;
  if(form<2){code[n++]=0x0f;code[n++]=0xaf;}else code[n++]=form<4?0x69:0x6b;
  code[n++]=form&1?0x06:0xc2;
  if(form&1){code[n++]=0;code[n++]=8;uint32_t v=form<2?b:a;mem_w16(&c,0x2800,v);mem_w16(&c,0x2802,v>>16);}
  if(form>=2){code[n++]=b;if(form<4){code[n++]=b>>8;code[n++]=b>>16;code[n++]=b>>24;}}
  cpu_put_reg32(&c,AX,a);cpu_put_reg32(&c,DX,form<2?b:a);c.flags=0x202;c.ip=0;memcpy(c.mem+0x1000,code,n);
  CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_reg32(&c,AX)==result&&c.ip==n);CHECK((c.flags&0x801)==(flags&0x801));CHECK(c.flags&FLAG_IF);cases++;
 }
 /* One-operand forms define EDX:EAX and only CF/OF. */
 for(unsigned i=0;i<64;i++)for(unsigned form=0;form<4;form++){
  uint32_t a=edges[i/8],b=edges[i%8],lo=a,hi;unsigned long flags;
  if(form&1)__asm__ volatile("imull %3; pushfq; popq %2":"+&a"(lo),"=&d"(hi),"=r"(flags):"r"(b):"cc");
  else __asm__ volatile("mull %3; pushfq; popq %2":"+&a"(lo),"=&d"(hi),"=r"(flags):"r"(b):"cc");
  unsigned char p[]={0x66,0xf7,(form>=2?0x26:0xe3)|((form&1)<<3),0,8};
  memcpy(c.mem+0x1000,p,sizeof(p));c.ip=0;c.flags=0x202;cpu_put_reg32(&c,AX,a);cpu_put_reg32(&c,BX,b);mem_w16(&c,0x2800,b);mem_w16(&c,0x2802,b>>16);
  CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_reg32(&c,AX)==lo&&cpu_reg32(&c,DX)==hi);CHECK((c.flags&0x801)==(flags&0x801));cases++;
 }
 cpu_free(&c);printf("PASS: %u native IMUL32 register/memory and sign-extended immediate cases\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-imul32-') as tmp:
 src=Path(tmp)/'test.c';src.write_text(HARNESS);exe=Path(tmp)/'test'
 subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-fsanitize=address,undefined','-no-pie','-I',str(ROOT/'userland/dosrun'),str(src),str(ROOT/'userland/dosrun/cpu.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
