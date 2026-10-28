#!/usr/bin/env python3
"""The environment trailer must name the executable actually loaded."""
from pathlib import Path
import subprocess,tempfile
import dosrun_host_test as host
host.HARNESS=r'''
#include "cpu.h"
#include "dosapi.h"
#include "drives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){struct cpu c;cpu_init(&c);struct dos_state*d=dos_new(&c);CHECK(d);dos_set_program(d,0x1000,0x1010);
 dos_mount('Z',"/");dos_mount('C',"/n/disk");dos_mount('D',"/n/disk/sub");char name[256];
 CHECK(dos_guest_path("/n/disk/app.exe",name,sizeof(name))==0&&!strcmp(name,"C:\\app.exe"));
 CHECK(dos_guest_path("/n/disk/sub/tool.exe",name,sizeof(name))==0&&!strcmp(name,"D:\\tool.exe"));
 CHECK(dos_guest_path("/n/diskette/app.exe",name,sizeof(name))==0&&!strcmp(name,"Z:\\n\\diskette\\app.exe"));
 CHECK(dos_guest_path("/n/disk/app.exe",name,4)<0);
 dos_mount('E',".");CHECK(dos_guest_path("./relative.exe",name,sizeof(name))==0&&!strcmp(name,"E:\\relative.exe"));CHECK(dos_guest_path(".different/app.exe",name,sizeof(name))<0);
 CHECK(dos_set_executable(d,"/n/disk/app.exe")==0);dos_build_psp(d);
 unsigned env=rd16(&c,0x1000,0x2c),off=0;while(off<4094&&(rd8(&c,env,off)||rd8(&c,env,off+1)))off++;
 CHECK(off<4094&&rd16(&c,env,off+2)==1);
 const char *expected="C:\\app.exe";for(unsigned i=0;i<=strlen(expected);i++)CHECK(rd8(&c,env,off+4+i)==expected[i]);
 dos_free(d);cpu_free(&c);puts("PASS: executable environment trailer, longest mount, path boundary and truncation checks");}
'''
with tempfile.TemporaryDirectory(prefix='dosrun-environment-') as tmp:
 exe=host.build(tmp,['-fsanitize=address,undefined','-no-pie']);subprocess.run([exe],cwd=tmp,check=True)
