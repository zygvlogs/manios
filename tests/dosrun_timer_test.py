#!/usr/bin/env python3
"""Deterministic PIT0/PIC/BIOS timer and guest interrupt regressions."""
from pathlib import Path
import subprocess
import tempfile
import dosrun_host_test as host
host.HARNESS = r'''
#include "cpu.h"
#include "dosapi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static struct cpu c;static struct dos_state*d;
static void out(int p,int v){CHECK(c.port_out8(&c,p,v)==0);}
static int in(int p){uint8_t v;CHECK(c.port_in8(&c,p,&v)==0);return v;}
static void finish(void){
 for(int i=0;i<100;i++){
  enum cpu_stop s=cpu_step(&c);
  if(s==CPU_INT){enum cpu_stop r=dos_int(d,&c,c.int_no);CHECK(r==CPU_OK||r==CPU_IRET||r==CPU_GUEST);
   if(r!=CPU_GUEST){c.ip=pop16(&c);c.sreg[1]=pop16(&c);c.flags=pop16(&c);}}
  else CHECK(s==CPU_OK||s==CPU_IRET);
  if(c.sreg[1]==0x1000&&c.ip==0&&c.r[SP]==0xFF00)return;
 }CHECK(0);
}
static void irq(void){CHECK(cpu_step(&c)==CPU_INT&&c.int_no==8);CHECK(dos_int(d,&c,8)==CPU_GUEST);finish();}
static void setup(void){cpu_init(&c);d=dos_new(&c);CHECK(d);c.sreg[1]=0x1000;c.sreg[2]=0x3000;c.r[SP]=0xFF00;c.flags=0x202;wr8(&c,0x1000,0,0x90);}
int main(void){
 setup();
 /* No early pulse, default BIOS tick, INT 1Ch hook and IRET preservation. */
 unsigned char hook[]={0x1E,0x50,0x31,0xC0,0x8E,0xD8,0xFF,0x06,0x00,0x05,0x58,0x1F,0xCF};
 memcpy(c.mem+0x20100,hook,sizeof(hook));wr16(&c,0,0x70,0x100);wr16(&c,0,0x72,0x2000);
 c.r[AX]=0xBEEF;c.sreg[3]=0x1234;c.flags=0xA47;
 dos_timer_advance(d,65535);CHECK(in(0x20)==0);dos_timer_advance(d,1);CHECK(in(0x20)==1);
 irq();CHECK(mem_r16(&c,0x46C)==1&&mem_r16(&c,0x500)==1&&c.flags==0xA47&&c.r[AX]==0xBEEF&&c.sreg[3]==0x1234);
 out(0x20,0x0B);CHECK(in(0x20)==0);out(0x20,0x0A);
 /* Masking retains one edge; multiple pulses coalesce, not a tick queue. */
 out(0x21,0xFF);dos_timer_advance(d,65536*5u);CHECK(in(0x20)==1&&c.irq_ack(&c)<0);
 out(0x21,0xFE);irq();CHECK(mem_r16(&c,0x46C)==2);
 c.flags&=~FLAG_IF;dos_timer_advance(d,65536);CHECK(cpu_step(&c)==CPU_OK&&in(0x20)==1);c.ip=0;
 c.flags|=FLAG_IF;irq();CHECK(mem_r16(&c,0x46C)==3);
 /* ISR blocks redelivery until valid EOI, even with IF set. */
 dos_timer_advance(d,65536);CHECK(c.irq_ack(&c)==8);dos_timer_advance(d,65536);
 CHECK(c.irq_ack(&c)<0);out(0x20,0x0B);CHECK(in(0x20)==1);out(0x20,0x20);CHECK(c.irq_ack(&c)==8);out(0x20,0x60);out(0x20,0x0A);
 /* STI and SS loads inhibit delivery for exactly the following instruction. */
 unsigned char shadows[][3]={{0xFB,0x90,0x90},{0x8E,0xD0,0x90},{0x17,0x90,0x90}};
 for(int i=0;i<3;i++){
  c.ip=0;c.irq_shadow=0;c.flags=2;c.r[AX]=0x3000;memcpy(c.mem+0x10000,shadows[i],3);
  if(i==2)push16(&c,0x3000);
  CHECK(cpu_step(&c)==CPU_OK);c.flags|=FLAG_IF;dos_timer_advance(d,65536);
  CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_step(&c)==CPU_INT&&c.int_no==8);
  out(0x20,0x20);c.ip=pop16(&c);c.sreg[1]=pop16(&c);c.flags=pop16(&c);
 }
 c.ip=0;wr8(&c,0x1000,0,0x90);c.flags=0x202;
 /* Reprogrammed mode 2 and one-shot terminal edges. Partial low/high writes. */
 out(0x43,0x34);out(0x40,4);dos_timer_advance(d,100);CHECK(in(0x20)==0);out(0x40,0);
 dos_timer_advance(d,3);CHECK(in(0x20)==0);dos_timer_advance(d,1);irq();CHECK(mem_r16(&c,0x46C)==4);
 out(0x43,0x30);out(0x40,3);out(0x40,0);dos_timer_advance(d,2);CHECK(in(0x20)==0);dos_timer_advance(d,1);irq();
 dos_timer_advance(d,100);CHECK(in(0x20)==0);
 /* A count latch remains stable across oscillator advances. */
 out(0x43,0x34);out(0x40,0x34);out(0x40,0x12);out(0x43,0);
 unsigned lo=in(0x40);dos_timer_advance(d,17);out(0x43,0);
 CHECK((lo|(in(0x40)<<8))==0x1234);
 /* BIOS midnight wrap, independent of the PIT divisor. */
 mem_w16(&c,0x46C,0xAF);mem_w16(&c,0x46E,0x18);
 out(0x43,0x36);out(0x40,0);out(0x40,0);dos_timer_advance(d,65536);irq();
 CHECK(mem_r16(&c,0x46C)==0&&mem_r16(&c,0x46E)==0&&mem_r8(&c,0x470)==1);
 /* A custom INT08 handler chains to the saved ROM vector through INT F0. */
 wr16(&c,0,0xF0*4,0x100);wr16(&c,0,0xF0*4+2,0xF000);
 unsigned char custom[]={0xCD,0xF0,0xCF};memcpy(c.mem+0x20200,custom,3);
 wr16(&c,0,0x20,0x200);wr16(&c,0,0x22,0x2000);
 dos_timer_advance(d,65536);irq();CHECK(mem_r16(&c,0x46C)==1&&in(0x20)==0);
 unsigned char no_chain[]={0x50,0xB0,0x20,0xE6,0x20,0x58,0xCF};
 memcpy(c.mem+0x20200,no_chain,sizeof(no_chain));
 unsigned bios_ticks=mem_r16(&c,0x46C), hook_ticks=mem_r16(&c,0x500);
 dos_timer_advance(d,65536);irq();
 CHECK(mem_r16(&c,0x46C)==bios_ticks&&mem_r16(&c,0x500)==hook_ticks);
 CHECK(c.port_out8(&c,0x20,0x11)<0); /* unsupported PIC reinitialization */
 dos_free(d);cpu_free(&c);
 puts("PASS: PIT0 edges, PIC mask/IRR/ISR/EOI, IF and STI/SS shadow, BIOS clock/INT1C, chained INT08");
}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-timer-') as tmp:
    subprocess.run([host.build(tmp)], check=True)
