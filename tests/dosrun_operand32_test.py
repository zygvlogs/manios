#!/usr/bin/env python3
"""Real-mode 66h regressions, with a native x86 ALU oracle."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct cpu c;
static void code(const unsigned char *p, size_t n) { c.ip=0; memcpy(c.mem+0x1000,p,n); }
#define CODE(...) do { const unsigned char p[]={__VA_ARGS__}; code(p,sizeof(p)); } while(0)
static uint32_t oracle(unsigned op,uint32_t a,uint32_t b,unsigned cf,unsigned *flags) {
 unsigned long f;
 #define NATIVE(insn) __asm__ volatile("bt $0,%3; " insn " %2,%0; pushfq; popq %1" : "+r"(a), "=r"(f) : "r"(b), "r"(cf) : "cc")
 switch(op) {case 0:NATIVE("addl");break;case 1:NATIVE("orl");break;case 2:NATIVE("adcl");break;case 3:NATIVE("sbbl");break;case 4:NATIVE("andl");break;case 5:case 7:NATIVE("subl");break;default:NATIVE("xorl");break;}
 *flags=f;return a;
}
static unsigned seed=0x12345678;
static unsigned random32(void) {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void) {
 cpu_init(&c); CHECK(c.mem); c.sreg[1]=0x100;c.sreg[3]=0x200;c.sreg[2]=0x300;c.sreg[0]=0x400;
 /* Native host oracle, all 8 ALU operations, boundary and random inputs,
  * both directions, register/memory and all immediate encodings. */
 unsigned edge[]={0,1,0x7fffffff,0x80000000,0xffffffff,0xfffffffe,0xf,0x10};
 for(unsigned op=0;op<8;op++) for(unsigned i=0;i<512;i++) for(unsigned form=0;form<8;form++) {
  uint32_t a=i<64?edge[i/8]:random32(),b=i<64?edge[i%8]:random32(); unsigned cf=i&1,nf;
  if(form==5)b=(uint32_t)(int32_t)(int8_t)b;
  uint32_t expect=oracle(op,a,b,cf,&nf);c.flags=2|FLAG_IF|(cf?FLAG_CF:0);
  cpu_put_reg32(&c,AX,a);cpu_put_reg32(&c,CX,b);
  if(form==0) {CODE(0x66,(op<<3)|1,0xC8);} /* EAX,ECX */
  if(form==1) {CODE(0x66,(op<<3)|3,0xC1);} /* EAX,ECX, reverse direction */
  if(form==2) {CODE(0x66,(op<<3)|3,0x06,0x00,0x08);mem_w16(&c,0x2800,b);mem_w16(&c,0x2802,b>>16);}
  if(form==3) {CODE(0x66,(op<<3)|5,b,b>>8,b>>16,b>>24);}
  if(form==4) {CODE(0x66,0x81,0xC0|(op<<3),b,b>>8,b>>16,b>>24);}
  if(form==5) {CODE(0x66,0x83,0xC0|(op<<3),b);}
  if(form==6) {CODE(0x66,(op<<3)|1,0x0E,0,8);mem_w16(&c,0x2800,a);mem_w16(&c,0x2802,a>>16);}
  if(form==7) {CODE(0x66,0x81,0x06|(op<<3),0,8,b,b>>8,b>>16,b>>24);mem_w16(&c,0x2800,a);mem_w16(&c,0x2802,a>>16);}
  CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_reg32(&c,AX)==(form>=6||op==7?a:expect));
  if(form>=6)CHECK(((uint32_t)mem_r16(&c,0x2800)|((uint32_t)mem_r16(&c,0x2802)<<16))==(op==7?a:expect));
  unsigned mask=FLAG_CF|FLAG_OF|FLAG_SF|FLAG_ZF|FLAG_PF;
  if(op==0||op==2||op==3||op==5||op==7)mask|=FLAG_AF;
  CHECK((c.flags&mask)==(nf&mask));CHECK(c.flags&FLAG_IF);
  CHECK(c.ip==(form==7?9:form==6?5:form==2?5:form==3?6:form==4?7:form==5?4:3));
 }
 /* Mixing widths and repeated prefixes preserves upper halves. */
 CODE(0x66,0x66,0xB8,0x78,0x56,0x34,0x12,0xB8,0xCD,0xAB,0xB4,0xEE);
 CHECK(cpu_step(&c)==CPU_OK&&c.ip==7&&cpu_reg32(&c,AX)==0x12345678);
 CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0x1234ABCD);
 CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0x1234EECD);
 /* Every ModR/M address mode, displacement size, register and SS default. */
 for(unsigned mod=0;mod<4;mod++) for(unsigned r=0;r<8;r++) for(unsigned m=0;m<8;m++) {
  for(int j=0;j<8;j++)cpu_put_reg32(&c,j,0xAAAA0000|0x100);
  unsigned base[]={0x200,0x200,0x200,0x200,0x100,0x100,0x100,0x100};
  unsigned off=base[m],disp=mod==1?0xF8:0x1234;
  if(mod==0&&m==6)off=0x1234;
  else if(mod==1)off=(off-8)&65535;else if(mod==2)off=(off+disp)&65535;
  CODE(0x66,0x8B,(mod<<6)|(r<<3)|m,disp,disp>>8);
  uint16_t seg=(m==2||m==3||(m==6&&mod!=0))?c.sreg[2]:c.sreg[3];
  mem_w16(&c,lin(seg,off),0x5678);mem_w16(&c,lin(seg,off)+2,0x1234);
  CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_reg32(&c,r)==(mod==3?0xAAAA0100:0x12345678));
  CHECK(c.ip==(mod==1?4:mod==2||(mod==0&&m==6)?5:3));
 }
 CODE(0x26,0x66,0xC7,0x06,0x00,0x08,0xEF,0xCD,0xAB,0x89);
 CHECK(cpu_step(&c)==CPU_OK&&mem_r16(&c,0x4802)==0x89AB&&c.ip==10);
 CODE(0x66,0xA1,0,8);CHECK(cpu_step(&c)==CPU_OK);
 CODE(0x66,0xA3,4,8);CHECK(cpu_step(&c)==CPU_OK&&mem_r16(&c,0x2804)==c.r[AX]);
 CODE(0x66,0x8D,0x46,0xFC);c.r[BP]=0x20;CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0x1c);
 c.r[SP]=0x8000;c.r_hi[SP]=0xABCD;cpu_put_reg32(&c,AX,0x87654321);
 CODE(0x66,0x50,0x66,0x5B);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,SP)==0xABCD7FFC);
 CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,BX)==0x87654321&&c.r[SP]==0x8000);
 CODE(0x66,0x54,0x66,0x5C);CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,SP)==0xABCD8000);
 CODE(0x66,0x6A,0xFF,0x66,0x59);CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,CX)==0xffffffff);
 cpu_put_reg32(&c,AX,0x7fffffff);c.flags=FLAG_CF|2;CODE(0x66,0x40);CHECK(cpu_step(&c)==CPU_OK&&(c.flags&(FLAG_CF|FLAG_OF|FLAG_SF))==(FLAG_CF|FLAG_OF|FLAG_SF));
 CODE(0x66,0x48);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0x7fffffff&&(c.flags&FLAG_CF));
 CODE(0x66,0xF7,0xD0);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0x80000000);
 CODE(0x66,0xF7,0xD8);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0x80000000&&(c.flags&FLAG_OF));
 CODE(0x66,0x85,0xC0);CHECK(cpu_step(&c)==CPU_OK&&(c.flags&FLAG_SF)&&!(c.flags&(FLAG_CF|FLAG_OF)));
 c.r[AX]=0x8000;CODE(0x66,0x98,0x66,0x99);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0xFFFF8000);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,DX)==0xffffffff);
 CODE(0x66,0x0F,0xB6,0xC4);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0x80);
 c.r[CX]=0x8001;CODE(0x66,0x0F,0xBF,0xC1);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0xffff8001);
 CODE(0x66,0x0F,0xC8);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0x0180ffff);
 /* Dword strings: address-size remains 16; upper index/count halves survive. */
 for(unsigned op=0;op<3;op++)for(unsigned rep=0;rep<3;rep++)for(unsigned df=0;df<2;df++) {
  unsigned opcode=op==0?0xA5:op==1?0xAB:0xAD;
  c.flags=2|FLAG_CF|FLAG_OF|(df?FLAG_DF:0);unsigned flags=c.flags;
  cpu_put_reg32(&c,SI,0xAAAA0810);cpu_put_reg32(&c,DI,0xBBBB0910);cpu_put_reg32(&c,CX,0xCCCC0003);
  cpu_put_reg32(&c,AX,0x87654321);
  for(int j=0;j<3;j++){int off=0x810+(df?-4:4)*j;mem_w16(&c,lin(c.sreg[2],off),0x4567+j);mem_w16(&c,lin(c.sreg[2],off)+2,0x1234);}
  if(rep){CODE(rep==1?0xF3:0xF2,0x36,0x66,opcode);}else{CODE(0x66,0x36,opcode);}
  CHECK(cpu_step(&c)==CPU_OK&&c.flags==flags);
  int n=rep?3:1,step=(df?-4:4)*n;
  CHECK(cpu_reg32(&c,SI)==(0xAAAA0000u|(unsigned)(0x810+(op==1?0:step))));
  CHECK(cpu_reg32(&c,DI)==(0xBBBB0000u|(unsigned)(0x910+(op==2?0:step))));
  CHECK(cpu_reg32(&c,CX)==(0xCCCC0000u|(rep?0:3)));
  if(op==2)CHECK(cpu_reg32(&c,AX)==(0x12344567u+n-1));
  else for(int j=0;j<n;j++){int off=0x910+(df?-4:4)*j;CHECK(mem_r16(&c,lin(c.sreg[0],off))==(op==1?0x4321:0x4567+j));CHECK(mem_r16(&c,lin(c.sreg[0],off)+2)==(op==1?0x8765:0x1234));}
 }
 c.r[SI]=c.r[DI]=0xFFFF;c.r[CX]=0;unsigned flags=c.flags;
 CODE(0x66,0xF3,0xA5);CHECK(cpu_step(&c)==CPU_OK&&c.r[SI]==0xFFFF&&c.flags==flags);
 c.r[SI]=0xFFFC;c.r[DI]=0x100;c.r[CX]=2;CODE(0x66,0xF3,0xA5);c.flags&=~FLAG_DF;
 CHECK(cpu_step(&c)==CPU_OK&&c.r[SI]==4&&c.r[DI]==0x108&&c.r[CX]==0);
 c.r[SI]=0;c.r[DI]=0xFFFD;c.r[CX]=1;CODE(0x66,0xF3,0xA5);CHECK(cpu_step(&c)==CPU_FAULT&&c.ip==0&&c.r[SI]==0&&c.r[DI]==0xFFFD&&c.r[CX]==1);
 /* CPUID defines complete registers, regardless of operand-size prefix. */
 cpu_put_reg32(&c,AX,0);CODE(0x0F,0xA2);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==1&&cpu_reg32(&c,BX)==0x756e6547&&cpu_reg32(&c,DX)==0x49656e69&&cpu_reg32(&c,CX)==0x6c65746e);
 cpu_put_reg32(&c,AX,1);CODE(0x66,0x0F,0xA2);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0x400&&cpu_reg32(&c,DX)==0);
 cpu_put_reg32(&c,AX,0x10000);CODE(0x0F,0xA2);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0);
 /* IP wraps only at the 16-bit boundary after a complete instruction. */
 CODE(0x66,0xB8,1,2,3,4);memcpy(c.mem+0x10ffa,c.mem+0x1000,6);c.ip=0xfffa;CHECK(cpu_step(&c)==CPU_OK&&c.ip==0&&cpu_reg32(&c,AX)==0x04030201);
 /* Memory stack forms, exchanges and TEST preserve complete operands. */
 c.r[SP]=0x8000;cpu_put_reg32(&c,AX,0x11223344);cpu_put_reg32(&c,BX,0xaabbccdd);
 CODE(0x66,0x87,0xD8,0x66,0x93);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,BX)==0x11223344);CHECK(cpu_step(&c)==CPU_OK&&cpu_reg32(&c,AX)==0x11223344);
 CODE(0x66,0xFF,0x36,0,8,0x66,0x8F,0x06,4,8);CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_step(&c)==CPU_OK&&c.r[SP]==0x8000&&mem_r16(&c,0x2804)==mem_r16(&c,0x2800)&&mem_r16(&c,0x2806)==mem_r16(&c,0x2802));
 CODE(0x66,0xFF,0x06,4,8,0x66,0xFF,0x0E,4,8);unsigned old=mem_r16(&c,0x2804);CHECK(cpu_step(&c)==CPU_OK&&mem_r16(&c,0x2804)==((old+1)&65535));CHECK(cpu_step(&c)==CPU_OK&&mem_r16(&c,0x2804)==old);
 CODE(0x66,0xA9,0,0,0,0);CHECK(cpu_step(&c)==CPU_OK&&(c.flags&FLAG_ZF));
 CODE(0x66,0xF7,0xC0,0,0,0,0);CHECK(cpu_step(&c)==CPU_OK&&(c.flags&FLAG_ZF));
 /* Full 15-byte encoding accepted, 16-byte encoding rejected atomically. */
 unsigned char longcode[16];memset(longcode,0x66,sizeof(longcode));longcode[10]=0xB8;memset(longcode+11,0,4);code(longcode,15);CHECK(cpu_step(&c)==CPU_OK&&c.ip==15);
 memset(longcode,0x66,11);longcode[11]=0xB8;memset(longcode+12,0,4);code(longcode,16);CHECK(cpu_step(&c)==CPU_FAULT&&c.ip==0);
 /* Unsupported forms must not execute a 16-bit approximation or mutate data. */
 unsigned char invalid[][8]={{0x66,0xD1,0xF0},{0x66,0x67,0x89,0xC0},{0x66,0xE8,0,0,0,0},{0x66,0xF3,0xA7},{0x66,0xC7,0xC8,1,2,3,4},{0x66,0xF7,0xC8},{0x66,0x0F,0x01,0xC0},{0x66,0xF0,0x01,0xC0},{0x66,0x8D,0xC0},{0x66,0xA3,0xFD,0xFF},{0x66,0x89,0x06,0xFD,0xFF}};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
  code(invalid[i],8);struct cpu before=c;unsigned char mem[MEM_SIZE];memcpy(mem,c.mem,MEM_SIZE);
  CHECK(cpu_step(&c)==CPU_FAULT&&c.ip==0&&c.fault_opcode==0x66);
  CHECK(!memcmp(c.r,before.r,sizeof(c.r))&&!memcmp(c.r_hi,before.r_hi,sizeof(c.r_hi))&&c.flags==before.flags&&!memcmp(mem,c.mem,MEM_SIZE));
 }
 cpu_free(&c);puts("PASS: 32768 native-oracle ALU cases; widths, ModR/M, segments, stack, extensions, length and explicit faults");return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="dosrun-operand32-") as tmp:
    src = Path(tmp) / "test.c"
    src.write_text(HARNESS)
    exe = Path(tmp) / "test"
    subprocess.run(["gcc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", "-no-pie",
                    "-I", str(ROOT / "userland/dosrun"), str(src),
                    str(ROOT / "userland/dosrun/cpu.c"), "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
