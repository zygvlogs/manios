#!/usr/bin/env python3
"""Enhanced BIOS keyboard queue consumption regression."""
from pathlib import Path
import subprocess
import tempfile
import dosrun_host_test as host
host.HARNESS = r'''
#include "cpu.h"
#include "dosapi.h"
#include "drives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct cpu c;
static struct dos_state *d;
static void init(void) {
 cpu_init(&c); c.sreg[1]=0x1000; c.sreg[2]=0x2000; c.sreg[3]=0x3000;
 c.sreg[0]=0x4000; c.r[SP]=0xFF00; c.flags=0x202;
}
static void instruction(const unsigned char *code,size_t n) { c.ip=0; memcpy(c.mem+0x10000,code,n); }
#define CODE(...) do { unsigned char code[]={__VA_ARGS__}; instruction(code,sizeof(code)); } while(0)
static void dispatch(int vec) {
 push16(&c,c.flags); push16(&c,c.sreg[1]); push16(&c,c.ip);
 CHECK(dos_int(d,&c,vec)==CPU_OK);
 c.ip=pop16(&c); c.sreg[1]=pop16(&c); c.flags=pop16(&c);
}
static void call(int vec,int ax) { c.r[AX]=ax; dispatch(vec); }
static void text(uint16_t seg,uint16_t off,const char *s) {
 do { wr8(&c,seg,off++,*s); } while(*s++);
}
int main(void) {
 init();d=dos_new(&c);CHECK(d);int keys[2],input[2];CHECK(pipe(keys)==0&&pipe(input)==0);
 CHECK(dup2(input[0],0)>=0);close(input[0]);dos_set_keyboard_fd(d,keys[0]);
 for(int i=0;i<3;i++){
  unsigned char pgdn=0x87;CHECK(write(keys[1],&pgdn,1)==1);dos_keyboard_poll(d);
  uint16_t head=rd16(&c,0x40,0x1A),tail=rd16(&c,0x40,0x1C);CHECK(head!=tail);
  for(int poll=0;poll<3;poll++){
   call(0x16,0x1100);CHECK(!(c.flags&FLAG_ZF)&&c.r[AX]==0x5100);
   CHECK(rd16(&c,0x40,0x1A)==head&&rd16(&c,0x40,0x1C)==tail);
  }
  call(0x16,0x1000);CHECK(c.r[AX]==0x5100&&rd16(&c,0x40,0x1A)==tail);
  call(0x16,0x1100);CHECK(c.flags&FLAG_ZF);
 }
 close(keys[1]);close(input[1]);dos_free(d);cpu_free(&c);
 puts("PASS: enhanced BIOS readiness preserves PgDn; AH=10 consumes each event once");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-keyqueue-') as tmp:
    exe=host.build(tmp)
    subprocess.run([exe],cwd=tmp,check=True)
