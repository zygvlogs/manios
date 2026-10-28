#!/usr/bin/env python3
"""Compare defined shift/rotate results and flags with native x86-64 hardware."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct cpu c;
static unsigned failures, cases;
#define CHECK(x) do { cases++; if (!(x)) { if(failures++<12) fprintf(stderr,"line %d: %s\n",__LINE__,#x); } } while(0)
static uint32_t native(unsigned bits,unsigned op,uint32_t a,unsigned count,unsigned flags,unsigned *out) {
 unsigned long f,initial=flags|2;
 #define ASM(insn,sz,reg) __asm__ volatile("pushq %3; popfq; " insn sz " %%cl," reg "; pushfq; popq %1" : "+&a"(a),"=&r"(f) : "c"(count),"r"(initial) : "cc")
 #define WIDTH(insn) do {if(bits==8){ASM(insn,"b","%%al");}else if(bits==16){ASM(insn,"w","%%ax");}else{ASM(insn,"l","%%eax");}}while(0)
 switch(op){case 0:WIDTH("rol");break;case 1:WIDTH("ror");break;case 2:WIDTH("rcl");break;case 3:WIDTH("rcr");break;case 4:WIDTH("shl");break;case 5:WIDTH("shr");break;default:WIDTH("sar");break;}
 *out=f;return a;
}
int main(void){
 cpu_init(&c);c.sreg[1]=0x100;c.sreg[3]=0x200;c.sreg[2]=0x300;
 unsigned edges[]={0,1,0x80000000,0xffffffff,0x7fffffff,0x8000,0x80,0x55aa55aa};
 for(unsigned bits=8;bits<=32;bits*=2)for(unsigned op=0;op<8;op++)if(op!=6)
 for(unsigned count=0;count<256;count++)for(unsigned e=0;e<8;e++)for(unsigned form=0;form<6;form++){
  if(form%3==0 && count!=1)continue;
  unsigned input=(e&1)?0xad7:0x202,nf;
  uint32_t a=edges[e],expect=native(bits,op,a,count,input,&nf);
  c.flags=input;c.ip=0;cpu_put_reg32(&c,AX,a);c.r[CX]=count;
  unsigned char code[8];unsigned n=0;
  if(bits==32)code[n++]=0x66;
  if(form>=3)code[n++]=0x36;
  code[n++]=(form%3==0?0xD0:form%3==1?0xD2:0xC0)+(bits!=8);
  code[n++]=(form>=3?0x06:0xC0)|(op<<3);
  if(form>=3){code[n++]=0;code[n++]=8;mem_w16(&c,0x3800,a);mem_w16(&c,0x3802,a>>16);}
  if(form%3==2)code[n++]=count;
  memcpy(c.mem+0x1000,code,n);
  enum cpu_stop stop=cpu_step(&c);
  if(stop!=CPU_OK){CHECK(stop==CPU_OK);continue;}
  uint32_t got=form>=3?(uint32_t)mem_r16(&c,0x3800)|((uint32_t)mem_r16(&c,0x3802)<<16):cpu_reg32(&c,AX);
  CHECK(got==expect);CHECK(c.ip==n);
  unsigned k=count&31,mask=FLAG_CF|FLAG_OF|FLAG_SF|FLAG_ZF|FLAG_PF|FLAG_AF|FLAG_IF|FLAG_DF;
  if(k){
   if(op>=4){mask&=~FLAG_AF;if(k>=bits && op!=7)mask&=~FLAG_CF;}
   if(k!=1)mask&=~FLAG_OF;
  }
  if((c.flags&mask)!=(nf&mask)){
   if(failures<12)fprintf(stderr,"width=%u op=%u count=%u input=%x flags=%x expected=%x mask=%x\n",bits,op,count,a,c.flags,nf,mask);
   CHECK((c.flags&mask)==(nf&mask));
  }
 }
 /* Undefined /6 encoding must fault, rather than return success without shifting. */
 for(int word=0;word<2;word++){
  c.ip=0;c.flags=0x202;cpu_put_reg32(&c,AX,0x12345678);
  c.mem[0x1000]=0xD0+word;c.mem[0x1001]=0xF0;
  CHECK(cpu_step(&c)==CPU_FAULT);CHECK(cpu_reg32(&c,AX)==0x12345678&&c.flags==0x202);
 }
 /* Dword segment/stack boundary and long encoding failures are atomic. */
 unsigned char bad[][17]={{0x66,0xC1,0x26,0xfd,0xff,1},{0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0xC1,0xe0,1}};
 for(unsigned i=0;i<2;i++){c.ip=0;c.flags=0xad7;cpu_put_reg32(&c,AX,0x12345678);memcpy(c.mem+0x1000,bad[i],17);CHECK(cpu_step(&c)==CPU_FAULT&&c.ip==0&&c.flags==0xad7&&cpu_reg32(&c,AX)==0x12345678);}
 cpu_free(&c);
 if(failures){fprintf(stderr,"FAIL: %u of %u checks\n",failures,cases);return 1;}
 printf("PASS: %u native shift/rotate, encoding and atomic-fault checks\n",cases);return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-shifts-') as tmp:
    src=Path(tmp)/'test.c';src.write_text(HARNESS);exe=Path(tmp)/'test'
    subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I',str(ROOT/'userland/dosrun'),str(src),str(ROOT/'userland/dosrun/cpu.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
