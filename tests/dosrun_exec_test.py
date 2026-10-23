#!/usr/bin/env python3
"""DOS EXEC process lifetime, errors, relocation and parent restoration."""
from pathlib import Path
import subprocess
import tempfile
import struct
import dosrun_host_test as host
host.HARNESS = r'''
#include "cpu.h"
#include "dosapi.h"
#include "drives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static struct cpu c;
static struct dos_state *d;
static enum cpu_stop call(int ax) {
 c.r[AX]=ax; push16(&c,c.flags);push16(&c,c.sreg[1]);push16(&c,c.ip);
 enum cpu_stop r=dos_int(d,&c,0x21);
 if(r!=CPU_EXIT){c.ip=pop16(&c);c.sreg[1]=pop16(&c);c.flags=pop16(&c);}return r;
}
static void text(int seg,int off,const char *s){do{wr8(&c,seg,off++,*s);}while(*s++);}
static void exec(const char *path,int ax){
 uint16_t psp=c.sreg[3];text(psp,0x500,path);c.r[DX]=0x500;c.sreg[0]=psp;c.r[BX]=0x600;
 wr16(&c,psp,0x600,0);wr16(&c,psp,0x602,0x700);wr16(&c,psp,0x604,psp);
 wr8(&c,psp,0x700,5);text(psp,0x701," test\r");
 CHECK(call(ax)==CPU_OK);
}
int main(void){
 cpu_init(&c); d=dos_new(&c);CHECK(d);dos_mount('C',".");
 c.sreg[0]=c.sreg[1]=c.sreg[2]=c.sreg[3]=0x1000;c.r[SP]=0xFF00;c.ip=0x100;c.flags=0x202;
 dos_set_program(d,0x1000,0x1010);dos_build_psp(d);
 /* Full parent allocation: EXEC fails without damaging the caller. */
 exec("C:\\CHILD.COM",0x4B00);CHECK((c.flags&1)&&c.r[AX]==8&&c.sreg[3]==0x1000);
 c.sreg[0]=0x1000;c.r[BX]=0x1000;call(0x4A00);CHECK(!(c.flags&1));
 /* Enough room for an environment, but not the child: rollback both. */
 c.r[BX]=0x7FE0;call(0x4800);CHECK(!(c.flags&1));uint16_t occupied=c.r[AX];
 c.r[BX]=0xFFFF;call(0x4800);uint16_t before=c.r[BX];
 exec("C:\\CHILD.COM",0x4B00);CHECK((c.flags&1)&&c.r[AX]==8);
 c.r[BX]=0xFFFF;call(0x4800);CHECK(c.r[BX]==before);
 c.sreg[0]=occupied;call(0x4900);CHECK(!(c.flags&1));
 for(int i=0;i<20;i++){
  c.r[DX]=0x900;call(0x1A00); /* parent DTA */
  wr16(&c,0x1000,0x800,0xCAFE);
  exec("C:\\CHILD.COM",0x4B00);CHECK(!(c.flags&1)&&c.sreg[3]!=0x1000);
  uint16_t child=c.sreg[3];CHECK(rd16(&c,child,0x16)==0x1000);
  CHECK(rd8(&c,child,0x80)==5&&rd8(&c,child,0x86)=='\r');
  uint16_t env=rd16(&c,child,0x2C);CHECK(env!=0xF00&&rd8(&c,env,0)=='P');
  CHECK(rd16(&c,child-1,1)==child&&rd16(&c,env-1,1)==child);
  /* Nested process after shrinking child; both parents resume. */
  c.sreg[0]=child;c.r[BX]=0x1000;call(0x4A00);CHECK(!(c.flags&1));
  exec("C:\\CHILD.COM",0x4B00);CHECK(!(c.flags&1)&&c.sreg[3]!=child);
  CHECK(call(0x4C2A)==CPU_OK&&c.sreg[3]==child);
  call(0x4D00);CHECK(c.r[AX]==42);call(0x4D00);CHECK(c.r[AX]==0);
  CHECK(call(0x4C07)==CPU_OK&&c.sreg[3]==0x1000&&c.r[SP]==0xFF00);
  CHECK(rd16(&c,0x1000,0x800)==0xCAFE);
  call(0x2F00);CHECK(c.sreg[0]==0x1000&&c.r[BX]==0x900);
  call(0x4D00);CHECK(c.r[AX]==7);
 }
 /* Closing an inherited child handle must not close the parent's handle. */
 text(0x1000,0x500,"C:\\DATA.TXT");c.r[DX]=0x500;call(0x3D00);CHECK(!(c.flags&1));
 uint16_t handle=c.r[AX];
 exec("C:\\CHILD.COM",0x4B00);CHECK(!(c.flags&1));
 c.r[BX]=handle;call(0x3E00);CHECK(!(c.flags&1));
 CHECK(call(0x0000)==CPU_OK&&c.sreg[3]==0x1000);
 c.r[BX]=handle;c.r[CX]=1;c.r[DX]=0xA00;call(0x3F00);
 CHECK(!(c.flags&1)&&c.r[AX]==1&&rd8(&c,0x1000,0xA00)=='X');
 c.r[BX]=handle;call(0x3E00);CHECK(!(c.flags&1));
 text(0x1000,0x500,"C:\\DATA.TXT");c.r[DX]=0x500;call(0x3D80);CHECK(!(c.flags&1));
 handle=c.r[AX];exec("C:\\CHILD.COM",0x4B00);CHECK(!(c.flags&1));
 CHECK(rd8(&c,c.sreg[3],0x18+handle)==0xFF);
 c.r[BX]=handle;c.r[CX]=1;c.r[DX]=0xA00;call(0x3F00);CHECK((c.flags&1)&&c.r[AX]==6);
 CHECK(call(0x4C00)==CPU_OK);c.r[BX]=handle;call(0x3E00);
 exec("C:\\MISSING.COM",0x4B00);CHECK((c.flags&1)&&c.r[AX]==2);
 exec("C:\\BAD.EXE",0x4B00);CHECK((c.flags&1)&&c.r[AX]==11);
 exec("C:\\CHILD.COM",0x4B02);CHECK((c.flags&1)&&c.r[AX]==1);
 exec("C:\\RELOC.EXE",0x4B00);CHECK(!(c.flags&1));
 CHECK(c.sreg[1]==c.sreg[3]+0x10&&rd16(&c,c.sreg[1],8)==c.sreg[1]);
 CHECK(call(0x4C00)==CPU_OK&&c.sreg[3]==0x1000);
 exec("C:\\HIGH.EXE",0x4B00);CHECK(!(c.flags&1));
 CHECK(c.sreg[1]==0x9FFF);CHECK(call(0x4C00)==CPU_OK);
 /* Actually execute the child's termination instructions through cpu_step. */
 exec("C:\\CHILD.COM",0x4B00);CHECK(!(c.flags&1));
 CHECK(cpu_step(&c)==CPU_OK);CHECK(cpu_step(&c)==CPU_INT);
 CHECK(dos_int(d,&c,c.int_no)==CPU_OK);
 c.ip=pop16(&c);c.sreg[1]=pop16(&c);c.flags=pop16(&c);
 CHECK(c.sreg[3]==0x1000&&c.r[SP]==0xFF00);call(0x4D00);CHECK(c.r[AX]==42);
 dos_free(d);cpu_free(&c);puts("PASS: EXEC COM/MZ, nested return, PSP/environment, DTA, allocation cleanup and errors");
}
'''
with tempfile.TemporaryDirectory(prefix='dos-exec-') as tmp:
    work=Path(tmp)
    (work/'child.com').write_bytes(bytes.fromhex('b82a4ccd21'))
    (work/'data.txt').write_text('X')
    (work/'bad.exe').write_bytes(b'MZ')
    data=bytearray(host.mz_image(bytes.fromhex('b8004ccd210000000000'),header_size=32))
    struct.pack_into('<H',data,6,1)
    struct.pack_into('<H',data,10,0x1000)
    struct.pack_into('<HH',data,28,8,0)
    (work/'reloc.exe').write_bytes(data)
    high=bytearray(host.mz_image(bytes.fromhex('b8004ccd21')))
    struct.pack_into('<HH',high,10,0,0)
    struct.pack_into('<H',high,16,14)
    (work/'high.exe').write_bytes(high)
    exe=host.build(tmp, ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"])
    subprocess.run([exe],cwd=tmp,check=True)
