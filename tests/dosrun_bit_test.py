#!/usr/bin/env python3
"""BT/BTS/BTR/BTC register and signed memory bit-string addressing."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d bits=%u op=%u index=%d form=%u: %s\n",__LINE__,bits,op,index,form,#x);exit(1);}}while(0)
int main(void){
 struct cpu c;cpu_init(&c);c.sreg[1]=0x100;c.sreg[3]=0x200;c.sreg[2]=0x300;
 unsigned ops[]={0xa3,0xab,0xb3,0xbb},cases=0;
 for(unsigned bits=16;bits<=32;bits*=2)for(unsigned op=0;op<4;op++)for(int index=-65;index<=65;index++)for(unsigned form=0;form<4;form++){
  unsigned char code[9];unsigned n=0;if(bits==32)code[n++]=0x66;
  if(form&1)code[n++]=0x36;
  code[n++]=0x0f;code[n++]=form>=2?0xba:ops[op];code[n++]=(form&1?6:0xc0)|((form>=2?4+op:3)<<3);
  if(form&1){code[n++]=0;code[n++]=8;}
  if(form>=2)code[n++]=index;
  memcpy(c.mem+0x1000,code,n);c.ip=0;c.flags=0xad7;
  cpu_put_reg32(&c,AX,0x91abcdef);cpu_put_reg32(&c,BX,(uint32_t)index);
  for(unsigned i=0;i<64;i++)mem_w8(&c,0x37e0+i,(unsigned char)(i*17));
  unsigned char before[64];memcpy(before,c.mem+0x37e0,64);
  int word=(index>=0?index/(int)bits:-((-(int)index+(int)bits-1)/(int)bits));
  unsigned bit=(unsigned)index&(bits-1),off=0x800+((form<2&&form&1)?word*(bits/8):0);
  uint32_t original=form&1?(uint32_t)mem_r16(&c,0x3000+off)|((uint32_t)mem_r16(&c,0x3002+off)<<16):cpu_reg32(&c,AX);
  uint32_t mask=1u<<bit,expected=op==1?original|mask:op==2?original&~mask:op==3?original^mask:original;
  CHECK(cpu_step(&c)==CPU_OK);CHECK(c.ip==n);CHECK(!!(c.flags&FLAG_CF)==!!(original&mask));CHECK(c.flags&FLAG_ZF);
  uint32_t got=form&1?(uint32_t)mem_r16(&c,0x3000+off)|((uint32_t)mem_r16(&c,0x3002+off)<<16):cpu_reg32(&c,AX);
  CHECK(got==expected);
  if(form&1)for(unsigned i=0;i<64;i++)if(i+0x7e0<off||i+0x7e0>=off+bits/8)CHECK(c.mem[0x37e0+i]==before[i]);
  cases++;
 }
 /* Invalid group extension cannot set CF or modify the operand. */
 unsigned bits=32,op=0,form=0;int index=0;
 for(unsigned width=0;width<2;width++){
  unsigned char bad[]={0x66,0x0f,0xba,0xc0,1};c.ip=0;c.flags=0xad7;cpu_put_reg32(&c,AX,7);
  memcpy(c.mem+0x1000,bad+!width,sizeof(bad)-!width);CHECK(cpu_step(&c)==CPU_FAULT);CHECK(c.flags==0xad7&&cpu_reg32(&c,AX)==7);
 }
 cpu_free(&c);printf("PASS: %u bit-string register/memory cases and invalid encodings\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-bits-') as tmp:
 src=Path(tmp)/'test.c';src.write_text(HARNESS);exe=Path(tmp)/'test'
 subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-fsanitize=address,undefined','-no-pie','-I',str(ROOT/'userland/dosrun'),str(src),str(ROOT/'userland/dosrun/cpu.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
