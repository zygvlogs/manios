#!/usr/bin/env python3
"""PIT channel 2 and PC-speaker state, byte I/O decoding and refusal paths."""
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
static struct dos_state *d;
static void out(unsigned port,unsigned v) { CHECK(c.port_out8(&c,port,v)==0); }
static unsigned in(unsigned port) { uint8_t v;CHECK(c.port_in8(&c,port,&v)==0);return v; }
static void load(unsigned n) { out(0x42,n&255);out(0x42,n>>8); }
static unsigned latched(void) { out(0x43,0x80);unsigned lo=in(0x42);return lo|(in(0x42)<<8); }
static void code(unsigned op,unsigned operand) {
 c.sreg[1]=0x1000;c.ip=0;wr8(&c,0x1000,0,op);wr8(&c,0x1000,1,operand);
}
int main(void) {
 cpu_init(&c);d=dos_new(&c);CHECK(d&&c.port_context&&c.port_out8);
 /* A control word is required, not simply swallowed. */
 CHECK(c.port_out8(&c,0x42,0x33)<0);uint8_t v;CHECK(c.port_in8(&c,0x42,&v)<0);
 out(0x43,0xB6);out(0x42,0x33);CHECK(dos_speaker_frequency(d)==0);
 out(0x61,3);CHECK(dos_speaker_frequency(d)==0); /* Incomplete divisor. */
 out(0x42,5);CHECK(dos_speaker_frequency(d)==896&&latched()==0x533);
 out(0x42,0x33);CHECK(dos_speaker_frequency(d)==896); /* Pending low byte. */
 out(0x42,6);CHECK(dos_speaker_frequency(d)==751&&latched()==0x633);
 out(0x61,0);CHECK(dos_speaker_frequency(d)==0&&(in(0x61)&0x20));
 out(0x61,2);CHECK(dos_speaker_frequency(d)==0); /* Speaker on, gate off. */
 out(0x61,1);CHECK(dos_speaker_frequency(d)==0); /* Gate on, speaker off. */
 out(0x61,3);CHECK(dos_speaker_frequency(d)==751);
 puts("PASS: Windows B6/33/05 and 33/06 tone sequence, partial divisor, gate and speaker enable");
 /* Even and odd mode-3 output duty and frozen gate-low counts. */
 out(0x61,0);out(0x43,0xB6);load(6);out(0x61,3);
 CHECK((in(0x61)&0x20)&&latched()==6);c.insns+=2;CHECK((in(0x61)&0x20)&&latched()==2);
 c.insns++;CHECK(!(in(0x61)&0x20)&&latched()==6);c.insns+=3;CHECK(in(0x61)&0x20);
 load(5);CHECK(latched()==5);c.insns++;CHECK(latched()==4);c.insns++;CHECK((in(0x61)&0x20)&&latched()==2);
 c.insns++;CHECK(!(in(0x61)&0x20)&&latched()==4);c.insns++;CHECK(latched()==2);
 out(0x61,0);unsigned count=latched();c.insns+=1000;CHECK(latched()==count&&(in(0x61)&0x20));
 out(0x61,3);CHECK(latched()==5&&(in(0x61)&0x20));
 /* A latch is stable across elapsed clocks and cannot be overwritten unread. */
 load(0x1234);out(0x43,0x80);unsigned lo=in(0x42);c.insns+=50;out(0x43,0x80);
 CHECK((lo|(in(0x42)<<8))==0x1234);CHECK(latched()!=0x1234);
 out(0x61,0);load(0);out(0x61,3);CHECK(dos_speaker_frequency(d)==18&&latched()==0);
 c.insns+=32768;CHECK(!(in(0x61)&0x20));
 c.insns=0xFFFFFFFEu;out(0x61,0);load(6);out(0x61,3);c.insns+=3;CHECK(!(in(0x61)&0x20));
 puts("PASS: mode-3 odd/even counts, OUT2 status, frozen/rising gate, latching, zero=65536 and wrap");
 /* Unsupported commands must fail without corrupting a live timer. */
 unsigned bad[]={0x36,0x76,0xB4,0xB7,0x96,0xD6,0xFF};
 unsigned hz=dos_speaker_frequency(d);
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)CHECK(c.port_out8(&c,0x43,bad[i])<0&&dos_speaker_frequency(d)==hz);
 CHECK(c.port_out8(&c,0x40,7)<0&&c.port_out8(&c,0x41,7)<0&&c.port_out8(&c,0x3DA,7)<0);
 out(0x61,0x0F);CHECK((in(0x61)&15)==15);out(0x61,in(0x61));CHECK((in(0x61)&15)==15);
 /* Actual immediate/DX opcodes consume exactly their operands and preserve
  * flags, AX on OUT, AH on IN, and unrelated registers. */
 for(int dx=0;dx<2;dx++) {
  code(dx?0xEE:0xE6,0x43);c.r[DX]=0x43;c.r[AX]=0xAAB6;c.flags=0xAD7;
  CHECK(cpu_step(&c)==CPU_OK&&c.ip==(dx?1:2)&&c.r[AX]==0xAAB6&&c.flags==0xAD7&&c.r[DX]==0x43);
  out(0x61,0);load(0x533);out(0x61,3);
  code(dx?0xEC:0xE4,0x61);c.r[DX]=0x61;c.r[AX]=0xAB00;c.flags=0xAD7;
  CHECK(cpu_step(&c)==CPU_OK&&c.ip==(dx?1:2)&&(c.r[AX]&0xFF00)==0xAB00&&(c.r[AX]&3)==3&&c.flags==0xAD7);
 }
 unsigned ops[]={0xE4,0xE6,0xEC,0xEE,0xE5,0xE7,0xED,0xEF};
 for(unsigned i=0;i<sizeof(ops)/sizeof(ops[0]);i++) {
  code(ops[i],0x60);c.r[DX]=0x60;c.r[AX]=0xBEEF;c.flags=0xAD7;
  CHECK(cpu_step(&c)==CPU_FAULT&&c.ip==1&&c.r[AX]==0xBEEF&&c.flags==0xAD7);
 }
 dos_free(d);CHECK(!c.port_in8&&!c.port_out8&&!c.port_context);
 code(0xE6,0x43);CHECK(cpu_step(&c)==CPU_FAULT);cpu_free(&c);
 puts("PASS: byte IN/OUT instruction semantics, unsupported ports/modes/widths and device cleanup");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="dosrun-pit-") as tmp:
    exe = host.build(tmp)
    subprocess.run([exe], check=True)
