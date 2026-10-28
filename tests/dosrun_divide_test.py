#!/usr/bin/env python3
"""DIV/IDIV32 and 8/16/32-bit divide errors, including signed overflow."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned seed=0x12345678;static unsigned rng(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void){struct cpu c;cpu_init(&c);c.sreg[1]=0x100;c.sreg[2]=0x300;c.sreg[3]=0x200;
 for(unsigned signedop=0;signedop<2;signedop++)for(unsigned i=0;i<4096;i++){
  uint32_t den=rng()|1,lo=rng(),hi=rng()%den;uint32_t q,r;
  if(signedop){int32_t b=(int32_t)den,expected_q=(int32_t)lo;int64_t a=(int64_t)expected_q*b;int32_t rem=(b<0?-(int64_t)b:b)/2;if(a<0)rem=-rem;a+=rem;den=b;hi=(uint64_t)a>>32;lo=a;q=a/b;r=a%b;}
  else{uint64_t dividend=((uint64_t)hi<<32)|lo;q=dividend/den;r=dividend%den;}
  cpu_put_reg32(&c,AX,lo);cpu_put_reg32(&c,DX,hi);cpu_put_reg32(&c,BX,den);c.ip=0;c.flags=0x202;
  unsigned char p[]={0x66,0xf7,(signedop?0xf8:0xf0)|(i&1?6:3),0,8};
  if(i&1){p[2]&=0x3f;mem_w16(&c,0x2800,den);mem_w16(&c,0x2802,den>>16);}
  memcpy(c.mem+0x1000,p,sizeof(p));CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_reg32(&c,AX)==q&&cpu_reg32(&c,DX)==r);CHECK(c.ip==(i&1?5:3));
 }
 /* Zero divisor and positive/negative quotient overflow trap before writes. */
 for(unsigned bits=8;bits<=32;bits*=2)for(unsigned mode=0;mode<3;mode++){
  c.sreg[1]=0x100;c.ip=0;c.r[SP]=0x8000;c.flags=0x302;
  cpu_put_reg32(&c,AX,mode==2?0:123);cpu_put_reg32(&c,DX,mode==2?0x80000000:1);cpu_put_reg32(&c,BX,mode==0?0:mode==1?1:0xffffffff);
  if(bits==16&&mode==2)c.r[DX]=0x8000;
  if(bits==8)c.r[AX]=mode==2?0x8000:0x100;
  unsigned char p[3];unsigned n=0;if(bits==32)p[n++]=0x66;p[n++]=bits==8?0xf6:0xf7;p[n++]=mode==2?0xfb:0xf3;
  memcpy(c.mem+0x1000,p,n);unsigned oldax=cpu_reg32(&c,AX),olddx=cpu_reg32(&c,DX);
  wr16(&c,0,0,0x100);wr16(&c,0,2,0x500);
  CHECK(cpu_step(&c)==CPU_INT&&c.int_no==0&&c.sreg[1]==0x500&&c.ip==0x100);
  CHECK(cpu_reg32(&c,AX)==oldax&&cpu_reg32(&c,DX)==olddx);CHECK(c.r[SP]==0x7ffa&&rd16(&c,0x300,0x7ffa)==0&&rd16(&c,0x300,0x7ffe)==0x302);
 }
 cpu_free(&c);puts("PASS: 8192 division results and 8/16/32-bit divide exception frames");}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-divide-') as tmp:
 src=Path(tmp)/'test.c';src.write_text(HARNESS);exe=Path(tmp)/'test'
 subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie','-I',str(ROOT/'userland/dosrun'),str(src),str(ROOT/'userland/dosrun/cpu.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
