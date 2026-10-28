#!/usr/bin/env python3
"""INT29 fast console writes, direct and chained, retain caller state."""
import subprocess,tempfile
import dosrun_host_test as host
host.HARNESS=r'''
#include "cpu.h"
#include "dosapi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){struct cpu c;cpu_init(&c);struct dos_state*d=dos_new(&c);CHECK(d);
 for(int far=0;far<2;far++) {
  const unsigned char direct[]={0xcd,0x29,0xf4},chain[]={0x9c,0xff,0x1e,0xa4,0x00,0xf4};
  c.sreg[0]=0x3000;c.sreg[1]=0x1000;c.sreg[2]=0x2000;c.sreg[3]=0;
  c.r[SP]=0x8000;c.ip=0;c.flags=0x203;c.flags_hi=0x24;
  cpu_put_reg32(&c,AX,0x12345a4b+far);cpu_put_reg32(&c,BX,0x87654321);
  memcpy(c.mem+0x10000,far?chain:direct,far?sizeof(chain):sizeof(direct));
  int done=0;
  for(int n=0;n<30;n++) {
   enum cpu_stop r=cpu_step(&c);
   if(r==CPU_INT){r=dos_int(d,&c,c.int_no);if(r==CPU_GUEST)continue;CHECK(r==CPU_OK||r==CPU_IRET);c.ip=pop16(&c);c.sreg[1]=pop16(&c);c.flags=pop16(&c);}
   else if(r==CPU_HALT){done=1;break;}else CHECK(r==CPU_OK||r==CPU_IRET);
  }
  CHECK(done&&c.r[SP]==0x8000&&c.flags==0x203&&c.flags_hi==0x24);
  CHECK(cpu_reg32(&c,AX)==0x12345a4bu+far&&cpu_reg32(&c,BX)==0x87654321);
  CHECK(c.sreg[0]==0x3000&&c.sreg[1]==0x1000&&c.sreg[2]==0x2000&&c.sreg[3]==0);
  CHECK(mem_r8(&c,0xb8000+far*2)==(unsigned)('K'+far));
 }
 dos_free(d);cpu_free(&c);fprintf(stderr,"PASS: INT29 direct/chained output, VGA cells, registers and flags\n");}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-fast-console-') as tmp:
 exe=host.build(tmp,['-fsanitize=address,undefined','-no-pie'])
 result=subprocess.run([exe],capture_output=True)
 if result.returncode: print(result.stderr.decode(),end="")
 result.check_returncode()
 assert result.stdout==b'KL',repr(result.stdout)
 print(result.stderr.decode(),end='')
