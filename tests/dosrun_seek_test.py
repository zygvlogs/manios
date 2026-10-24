#!/usr/bin/env python3
"""Focused signed INT 21h AH=42h regression using disposable host files."""
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
 init();d=dos_new(&c);CHECK(d);
 int fd=open("seek.dat",O_CREAT|O_RDWR,0600);CHECK(fd>=0);
 CHECK(write(fd,"abcdef",6)==6);close(fd);dos_mount('C',".");text(c.sreg[3],0x200,"C:\\seek.dat");c.r[DX]=0x200;call(0x21,0x3D02);CHECK(!(c.flags&FLAG_CF)&&c.r[AX]==5);
 int origins[]={0,0,1,1,2,2,2};
 int offsets[]={0,2,2,-1,0,-1,-6};
 int positions[]={0,2,4,3,6,5,0};
 for(int i=0;i<7;i++) {
  uint32_t off=(uint32_t)offsets[i];c.r[BX]=5;c.r[CX]=off>>16;c.r[DX]=off;
  c.flags|=FLAG_CF;call(0x21,0x4200|origins[i]);
  CHECK(!(c.flags&FLAG_CF)&&c.r[AX]==positions[i]&&c.r[DX]==0);
  if(i==5) {c.r[CX]=1;c.r[DX]=0x100;call(0x21,0x3F00);
   CHECK(!(c.flags&FLAG_CF)&&c.r[AX]==1&&rd8(&c,c.sreg[3],0x100)=='f');}
 }
 c.r[CX]=0xFFFF;c.r[DX]=0xFFFF;call(0x21,0x4200);CHECK((c.flags&FLAG_CF)&&c.r[AX]==25);
 c.r[CX]=0x7FFF;c.r[DX]=0xFFFF;call(0x21,0x4200);CHECK(!(c.flags&FLAG_CF)&&c.r[DX]==0x7FFF&&c.r[AX]==0xFFFF);
c.r[DX]=0xFFFF; call(0x21,0x4201);CHECK(!(c.flags&FLAG_CF)&&c.r[DX]==0xFFFF&&c.r[AX]==0xFFFE);
 c.r[CX]=0;c.r[DX]=2;call(0x21,0x4201);CHECK((c.flags&FLAG_CF)&&c.r[AX]==25);
 c.r[BX]=0xFFFF;call(0x21,0x4200);CHECK((c.flags&FLAG_CF)&&c.r[AX]==6);
 c.r[BX]=5;call(0x21,0x4203);CHECK((c.flags&FLAG_CF)&&c.r[AX]==1);
 dos_free(d);cpu_free(&c);puts("PASS: signed DOS seeks and error returns");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-seek-') as tmp:
    exe=host.build(tmp)
    subprocess.run([exe],cwd=tmp,check=True)
