#!/usr/bin/env python3
"""DOS time tracks BIOS ticks, including minute/hour/midnight boundaries."""
from pathlib import Path
import subprocess,tempfile
import dosrun_host_test as host
host.HARNESS=r'''
#include "cpu.h"
#include "dosapi.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){struct cpu c;cpu_init(&c);c.sreg[2]=0x3000;c.r[SP]=0x8000;struct dos_state*d=dos_new(&c);CHECK(d);
 unsigned ticks[]={0,1,18,19,1093,65544,0x1800af,0};
 for(unsigned i=0;i<sizeof(ticks)/sizeof(ticks[0]);i++){
  mem_w16(&c,0x46c,ticks[i]);mem_w16(&c,0x46e,ticks[i]>>16);mem_w8(&c,0x470,1);
  c.r[AX]=0x2c00;c.flags=0x203;push16(&c,c.flags);push16(&c,c.sreg[1]);push16(&c,c.ip);
  CHECK(dos_int(d,&c,0x21)==CPU_OK);c.ip=pop16(&c);c.sreg[1]=pop16(&c);c.flags=pop16(&c);
  /* Independent rational conversion, compared in centiseconds. */
  unsigned expected=(unsigned)((unsigned long long)ticks[i]*6553600/1193182)%8640000;
  unsigned got=(c.r[CX]>>8)*360000+(c.r[CX]&255)*6000+(c.r[DX]>>8)*100+(c.r[DX]&255);
  CHECK(got==expected);CHECK((c.r[CX]>>8)<24&&(c.r[CX]&255)<60&&(c.r[DX]>>8)<60&&(c.r[DX]&255)<100);
  CHECK(c.flags==0x203&&mem_r8(&c,0x470)==1);
 }
 dos_free(d);cpu_free(&c);puts("PASS: DOS time follows BIOS ticks, conversion boundaries and midnight without consuming rollover");}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-time-') as tmp:
 exe=host.build(tmp,['-fsanitize=address,undefined','-no-pie']);subprocess.run([exe],cwd=tmp,check=True)
