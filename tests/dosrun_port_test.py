#!/usr/bin/env python3
"""Synthetic video status, byte IN semantics and unsupported-port regressions."""
import subprocess
import tempfile
import dosrun_host_test as host

host.HARNESS = r'''
#include "cpu.h"
#include "dosapi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct cpu c;
static void code(const unsigned char *p,size_t n) {
 c.sreg[1]=0x1000;c.ip=0;memcpy(c.mem+0x10000,p,n);
}
int main(void) {
 cpu_init(&c);unsigned char in[]={0xEC};code(in,sizeof(in));c.r[DX]=0x3DA;
 CHECK(cpu_step(&c)==CPU_FAULT); /* No implicit device on a bare CPU. */
 struct dos_state *d=dos_new(&c);CHECK(d&&c.port_in8);
 unsigned seen=0,previous=0,vertical_edges=0;
 c.insns=0;c.flags=0xAD7;c.r[DX]=0x3DA;c.r[BX]=0x1234;
 for(int i=0;i<16384;i++) {
  c.ip=0;c.r[AX]=0xABFF;CHECK(cpu_step(&c)==CPU_OK);
  unsigned v=c.r[AX]&255;
  CHECK((c.r[AX]&0xFF00)==0xAB00&&c.flags==0xAD7&&c.r[DX]==0x3DA&&c.r[BX]==0x1234&&c.ip==1);
  CHECK(!(v&~9u));CHECK(!(v&8)||(v&1));
  seen|=1u<<v;if((v^previous)&8)vertical_edges++;previous=v;
 }
 CHECK((seen&((1u<<0)|(1u<<1)|(1u<<9)))==((1u<<0)|(1u<<1)|(1u<<9)));
 CHECK(vertical_edges>=4); /* Retrace must keep changing, not stick high. */
 uint8_t a,b;uint32_t insns=c.insns;
 CHECK(c.port_in8(&c,0x3DA,&a)==0&&c.port_in8(&c,0x3DA,&b)==0&&a==b&&c.insns==insns);
 puts("PASS: active display, horizontal blank and recurring vertical retrace; IN preserves AH/flags/registers");
 /* Real guest IN/TEST/Jcc loops wait for both edges of both bits. No
  * interrupts, host clock updates or port-read-side toggles are needed. */
 unsigned char poll[]={0xBA,0xDA,3,
  0xEC,0xA8,1,0x75,0xFB, 0xEC,0xA8,1,0x74,0xFB,
  0xEC,0xA8,1,0x75,0xFB,
  0xEC,0xA8,8,0x75,0xFB, 0xEC,0xA8,8,0x74,0xFB,
  0xEC,0xA8,8,0x75,0xFB, 0xF4};
 uint32_t seeds[]={0,3850,0xFFFFFFF0u};
 for(unsigned k=0;k<sizeof(seeds)/sizeof(seeds[0]);k++) {
  code(poll,sizeof(poll));c.insns=seeds[k];c.flags=2;int done=0;
  for(int i=0;i<50000;i++) {
   enum cpu_stop stop=cpu_step(&c);if(stop==CPU_HALT){done=1;break;}CHECK(stop==CPU_OK);
  }
  CHECK(done&&!(c.flags&FLAG_IF));
 }
 puts("PASS: both-polarity bit 0/3 polling loops terminate with interrupts disabled, including counter wrap");
 uint16_t ports[]={0,6,0x60,0x3BA,0x3DB,0xFFFF};
 for(unsigned k=0;k<sizeof(ports)/sizeof(ports[0]);k++) {
  code(in,sizeof(in));c.r[DX]=ports[k];c.r[AX]=0xBEEF;c.flags=0xAD7;
  CHECK(cpu_step(&c)==CPU_FAULT&&c.fault_opcode==0xEC&&c.r[AX]==0xBEEF&&c.flags==0xAD7);
 }
 unsigned char refused[]={0xED,0xEE,0xEF,0xE4,0xE5,0xE6,0xE7};
 for(unsigned k=0;k<sizeof(refused);k++) {
  unsigned char op[]={refused[k],0xDA};code(op,sizeof(op));c.r[DX]=0x3DA;c.r[AX]=0xBEEF;c.flags=0xAD7;
  CHECK(cpu_step(&c)==CPU_FAULT&&c.fault_opcode==refused[k]&&c.r[AX]==0xBEEF&&c.flags==0xAD7);
 }
 dos_free(d);CHECK(!c.port_in8);code(in,sizeof(in));c.r[DX]=0x3DA;CHECK(cpu_step(&c)==CPU_FAULT);
 cpu_free(&c);puts("PASS: unknown/read-only ports, word I/O and truncated immediate port aliases remain unsupported");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="dosrun-ports-") as tmp:
    exe = host.build(tmp)
    subprocess.run([exe], check=True)
