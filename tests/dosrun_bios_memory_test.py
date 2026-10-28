#!/usr/bin/env python3
"""BIOS memory probes must not return leftover registers as available RAM."""
import subprocess, tempfile
import dosrun_host_test as host
host.HARNESS = r'''
#include "cpu.h"
#include "dosapi.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void call(struct dos_state*d,struct cpu*c,unsigned v) {
 push16(c,c->flags);push16(c,c->sreg[1]);push16(c,c->ip);
 CHECK(dos_int(d,c,v)==CPU_OK);c->ip=pop16(c);c->sreg[1]=pop16(c);c->flags=pop16(c);
}
int main(void) {
 struct cpu c;cpu_init(&c);c.sreg[2]=0x3000;c.r[SP]=0x8000;
 struct dos_state*d=dos_new(&c);CHECK(d);
 CHECK(mem_r16(&c,0x413)==640);
 for(unsigned ax=0;ax<65536;ax+=257) {
  c.r[AX]=ax;c.r[BX]=0x1234;c.flags=0x203;call(d,&c,0x12);
  CHECK(c.r[AX]==640&&c.r[BX]==0x1234&&c.flags==0x203);
 }
 mem_w16(&c,0x413,639);call(d,&c,0x12);CHECK(c.r[AX]==639);
 unsigned queries[]={0x8800,0xe801,0xe820,0x8700,0x8900,0xc000};
 for(unsigned i=0;i<sizeof(queries)/sizeof(queries[0]);i++) {
  c.r[AX]=queries[i];c.flags=0x202;call(d,&c,0x15);
  CHECK((c.r[AX]>>8)==0x86&&(c.flags&FLAG_CF));
 }
 dos_free(d);cpu_free(&c);puts("PASS: INT12 BDA memory size, register independence; unsupported INT15 memory/configuration probes fail");
}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-bios-memory-') as tmp:
    exe=host.build(tmp,['-fsanitize=address,undefined','-no-pie'])
    subprocess.run([exe],cwd=tmp,check=True)
