#!/usr/bin/env python3
"""Initial MZ max allocation must leave a valid free MCB for child EXEC."""
from pathlib import Path
import subprocess,tempfile,struct
import dosrun_host_test as host
host.HARNESS=r'''
#include "cpu.h"
#include "loader.h"
#include "dosapi.h"
#include "drives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void text(struct cpu*c,unsigned off,const char*s){for(unsigned i=0;i<=strlen(s);i++)wr8(c,0x1000,off+i,s[i]);}
static enum cpu_stop call(struct dos_state*d,struct cpu*c,unsigned ax){c->r[AX]=ax;push16(c,c->flags);push16(c,c->sreg[1]);push16(c,c->ip);enum cpu_stop r=dos_int(d,c,0x21);if(r==CPU_OK){c->ip=pop16(c);c->sreg[1]=pop16(c);c->flags=pop16(c);}return r;}
int main(void) {
 const char*names[]={"bounded.exe","maximum.exe","high.exe","child.com"};
 for(unsigned i=0;i<4;i++) {
  struct cpu c;cpu_init(&c);char err[256];CHECK(load_program(&c,names[i],"",err,sizeof(err))==0);
  unsigned end=i==0?0x1031:0xa000;CHECK(rd16(&c,0x1000,2)==end);
  if(i==2)CHECK(c.sreg[1]==0x9fff&&mem_r8(&c,0x9fff0)==0xf4);
  struct dos_state*d=dos_new(&c);CHECK(d);dos_mount('C',".");dos_set_program(d,0x1000,0x1010);dos_build_psp(d);
  CHECK(rd16(&c,0x1000,2)==end&&rd16(&c,0xfff,3)==end-0x1000&&rd16(&c,0xfff,1)==0x1000);
  CHECK(rd8(&c,0xfff,0)==(i==0?'M':'Z'));
  if(i==0) {
   CHECK(rd8(&c,end,0)=='Z'&&rd16(&c,end,1)==0&&rd16(&c,end,3)==0xa000-end-1);
   /* This parent never calls AH4A. Its declared allocation is sufficient. */
   c.sreg[0]=c.sreg[3]=0x1000;c.sreg[2]=0x1010;c.r[SP]=0x200;
   text(&c,0x40,"C:\\child.com");c.r[DX]=0x40;c.r[BX]=0x60;
   for(unsigned j=0;j<14;j++)wr8(&c,0x1000,0x60+j,0);
   wr16(&c,0x1000,0x62,0x80);wr16(&c,0x1000,0x64,0x1000);wr8(&c,0x1000,0x80,0);wr8(&c,0x1000,0x81,'\r');
   CHECK(call(d,&c,0x4b00)==CPU_OK&&!(c.flags&FLAG_CF)&&c.sreg[3]!=0x1000);
   CHECK(call(d,&c,0x4c2a)==CPU_OK&&c.sreg[3]==0x1000);
   CHECK(call(d,&c,0x4d00)==CPU_OK&&c.r[AX]==42);
   CHECK(rd16(&c,0x1000,2)==0x1031&&rd16(&c,0xfff,3)==0x31);
  }
  dos_free(d);cpu_free(&c);
 }
 puts("PASS: bounded/full/high-loaded MZ and COM allocation, PSP/MCB agreement, child EXEC without parent shrink");
}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-initial-memory-') as tmp:
 for name,minimum,maximum in [('bounded.exe',0x20,0x20),('maximum.exe',0x20,0xffff),('high.exe',0,0)]:
  header=struct.pack('<14H',0x5a4d,33,1,0,2,minimum,maximum,0,0x200,0,0,0,28,0)+b'\0'*4
  (Path(tmp)/name).write_bytes(header+b'\xf4')
 (Path(tmp)/'child.com').write_bytes(b'\xb8\x2a\x4c\xcd\x21')
 exe=host.build(tmp,['-fsanitize=address,undefined','-no-pie']);subprocess.run([exe],cwd=tmp,check=True)
