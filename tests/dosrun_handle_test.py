#!/usr/bin/env python3
"""DOS close/duplicate errors and reopening closed standard handles."""
from pathlib import Path
import subprocess,tempfile,os
import dosrun_host_test as host
host.HARNESS=r'''
#include "cpu.h"
#include "dosapi.h"
#include "drives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static struct cpu c;static struct dos_state *d;
static void call(unsigned ax){c.r[AX]=ax;push16(&c,c.flags);push16(&c,c.sreg[1]);push16(&c,c.ip);CHECK(dos_int(d,&c,0x21)==CPU_OK);c.ip=pop16(&c);c.sreg[1]=pop16(&c);c.flags=pop16(&c);}
static void openfile(void){const char *s="C:\\DATA.TXT";for(unsigned i=0;i<=strlen(s);i++)wr8(&c,c.sreg[3],0x200+i,s[i]);c.r[DX]=0x200;call(0x3d02);}
int main(void){cpu_init(&c);c.sreg[1]=0x1000;c.sreg[2]=0x2000;c.sreg[3]=0x3000;c.r[SP]=0x8000;c.flags=0x202;d=dos_new(&c);CHECK(d);dos_mount('C',".");
 if(isatty(0)) { c.r[BX]=0;c.r[DX]=0xff00;c.flags|=FLAG_CF;call(0x4400);CHECK(!(c.flags&FLAG_CF)&&c.r[DX]==0x80d3);call(0x4500);CHECK(!(c.flags&FLAG_CF));unsigned con=c.r[AX];c.r[BX]=con;call(0x4400);CHECK(c.r[DX]==0x80d3);call(0x3e00); }
 dos_mount('D',".");
 const char *other="D:\\DATA.TXT";for(unsigned i=0;i<=strlen(other);i++)wr8(&c,c.sreg[3],0x400+i,other[i]);
 c.r[DX]=0x400;call(0x3d00);CHECK(!(c.flags&FLAG_CF));c.r[BX]=c.r[AX];call(0x4400);CHECK(!(c.flags&FLAG_CF)&&c.r[DX]==3);call(0x3e00);
 c.r[BX]=0xffff;call(0x3e00);CHECK((c.flags&FLAG_CF)&&c.r[AX]==6);
 openfile();CHECK(!(c.flags&FLAG_CF));unsigned handle=c.r[AX];c.r[BX]=handle;call(0x3e00);CHECK(!(c.flags&FLAG_CF));call(0x3e00);CHECK((c.flags&FLAG_CF)&&c.r[AX]==6);
 /* IOCTL must clear incoming CF and overwrite both bytes of DX. */
 openfile();CHECK(!(c.flags&FLAG_CF));handle=c.r[AX];
 c.r[BX]=handle;c.r[DX]=0xff80;c.flags|=FLAG_CF;call(0x4400);
 CHECK(!(c.flags&FLAG_CF)&&c.r[DX]==2);
 c.r[BX]=0xffff;call(0x4400);CHECK((c.flags&FLAG_CF)&&c.r[AX]==6);
 c.r[BX]=handle;call(0x440f);CHECK((c.flags&FLAG_CF)&&c.r[AX]==1);
 c.r[BX]=handle;call(0x3e00);CHECK(!(c.flags&FLAG_CF));
 /* Closing a DOS standard handle must not close the harness console. */
 c.r[BX]=0;call(0x3e00);CHECK(!(c.flags&FLAG_CF));c.r[CX]=1;c.r[DX]=0x300;call(0x3f00);CHECK((c.flags&FLAG_CF)&&c.r[AX]==6);
 openfile();CHECK(!(c.flags&FLAG_CF)&&c.r[AX]==0);
 c.r[BX]=0;c.r[CX]=1;c.r[DX]=0x300;call(0x3f00);CHECK(!(c.flags&FLAG_CF)&&c.r[AX]==1&&rd8(&c,c.sreg[3],0x300)=='a');
 c.r[BX]=1;call(0x3e00);CHECK(!(c.flags&FLAG_CF));c.r[BX]=0;call(0x4500);CHECK(!(c.flags&FLAG_CF)&&c.r[AX]==1);
 c.r[BX]=1;c.flags|=FLAG_CF;call(0x4400);CHECK(!(c.flags&FLAG_CF)&&c.r[DX]==2);
 c.r[BX]=1;c.r[CX]=1;c.r[DX]=0x300;call(0x3f00);CHECK(!(c.flags&FLAG_CF)&&rd8(&c,c.sreg[3],0x300)=='b');
 /* Force-duplicate, self-duplicate and shared file-position lifetime. */
 c.r[BX]=0;c.r[CX]=0xffff;call(0x4600);CHECK((c.flags&FLAG_CF)&&c.r[AX]==6);
 c.r[BX]=0;c.r[CX]=0;call(0x4600);CHECK(!(c.flags&FLAG_CF));
 c.r[BX]=1;call(0x3e00);CHECK(!(c.flags&FLAG_CF));c.r[BX]=0;c.r[CX]=1;c.r[DX]=0x300;call(0x3f00);CHECK(!(c.flags&FLAG_CF)&&rd8(&c,c.sreg[3],0x300)=='c');
 c.r[BX]=0;call(0x3e00);CHECK(!(c.flags&FLAG_CF));
 dos_free(d);cpu_free(&c);puts("PASS: invalid/repeated close, closed standard handles, lowest free allocation, duplicate lifetime/errors");}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-handles-') as tmp:
 (Path(tmp)/'data.txt').write_text('abcdef')
 exe=host.build(tmp,['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'])
 subprocess.run([exe],cwd=tmp,check=True)

 # A real terminal descriptor and its duplicate must both report CON.
 master,slave=os.openpty()
 try: subprocess.run([exe],cwd=tmp,stdin=slave,check=True)
 finally: os.close(slave);os.close(master)
