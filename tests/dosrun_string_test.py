#!/usr/bin/env python3
"""State-level regressions for DOS string operations and packed-program shifts."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static struct cpu c;
static void start(const unsigned char *code, size_t n) {
    cpu_init(&c); c.sreg[1]=0x100; c.sreg[3]=0x200; c.sreg[0]=0x300;
    c.ip=0; c.r[SI]=0x20; c.r[DI]=0x30;
    memcpy(c.mem+0x1000,code,n);
}
#define START(...) do { unsigned char code[]={__VA_ARGS__}; start(code,sizeof(code)); } while (0)
int main(void) {
    for (int word=0;word<2;word++) {
        int size=word+1;
        START(0xA4+word); wr16(&c,0x200,0x20,0x1234);
        CHECK(cpu_step(&c)==CPU_OK); CHECK((word?rd16(&c,0x300,0x30):rd8(&c,0x300,0x30))==(word?0x1234:0x34));
        CHECK(c.r[SI]==0x20+size && c.r[DI]==0x30+size); cpu_free(&c);
        START(0xAC+word); wr16(&c,0x200,0x20,0x1234); c.r[AX]=0xAB00;
        CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[AX]==(word?0x1234:0xAB34)); CHECK(c.r[SI]==0x20+size); cpu_free(&c);
        START(0xAA+word); c.r[AX]=0x1234; c.flags|=FLAG_DF;
        CHECK(cpu_step(&c)==CPU_OK); CHECK((word?rd16(&c,0x300,0x30):rd8(&c,0x300,0x30))==(word?0x1234:0x34)); CHECK(c.r[DI]==0x30-size); cpu_free(&c);
        START(0xA6+word); wr16(&c,0x200,0x20,0x1234); wr16(&c,0x300,0x30,0x1234);
        CHECK(cpu_step(&c)==CPU_OK); CHECK(c.flags&FLAG_ZF); CHECK(c.r[SI]==0x20+size); cpu_free(&c);
        START(0xAE + word); c.r[AX]=0x1234; wr16(&c,0x300,0x30,0x1234);
        CHECK(cpu_step(&c)==CPU_OK); CHECK(c.flags&FLAG_ZF); CHECK(c.r[DI]==0x30+size); cpu_free(&c);
    }
    for (int op=0xA4;op<=0xAF;op++) {
        if (op==0xA8 || op==0xA9) continue;
        for (int prefix=0xF2;prefix<=0xF3;prefix++) {
            START(prefix,op); c.r[CX]=0; c.r[AX]=0x1234; c.flags|=FLAG_ZF;
            CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[CX]==0 && c.r[SI]==0x20 && c.r[DI]==0x30 && c.r[AX]==0x1234);
            CHECK(rd16(&c,0x300,0x30)==0 && (c.flags&FLAG_ZF)); cpu_free(&c);
        }
    }
    START(0xF3,0xA5); c.r[CX]=2; c.flags|=FLAG_DF; c.r[SI]=0x22; c.r[DI]=0x32;
    wr16(&c,0x200,0x20,0x1234); wr16(&c,0x200,0x22,0x5678);
    CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[CX]==0 && c.r[SI]==0x1E && c.r[DI]==0x2E);
    CHECK(rd16(&c,0x300,0x30)==0x1234 && rd16(&c,0x300,0x32)==0x5678); cpu_free(&c);
    START(0xF3,0xAE); c.r[CX]=3; c.r[AX]=' '; memcpy(c.mem+0x3030,"  X",3);
    CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[CX]==0 && c.r[DI]==0x33 && !(c.flags&FLAG_ZF)); cpu_free(&c);
    START(0xF2,0xAE); c.r[CX]=4; c.r[AX]='X'; memcpy(c.mem+0x3030,"abXz",4);
    CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[CX]==1 && c.r[DI]==0x33 && (c.flags&FLAG_ZF)); cpu_free(&c);
    START(0xF3,0xA6); c.r[CX]=4; memcpy(c.mem+0x2020,"abXz",4); memcpy(c.mem+0x3030,"abYz",4);
    CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[CX]==1 && c.r[SI]==0x23 && c.r[DI]==0x33 && !(c.flags&FLAG_ZF)); cpu_free(&c);
    START(0xF2,0xA6); c.r[CX]=4; memcpy(c.mem+0x2020,"abXz",4); memcpy(c.mem+0x3030,"cdXz",4);
    CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[CX]==1 && (c.flags&FLAG_ZF)); cpu_free(&c);
    for(int order=0;order<2;order++) {
        START(order?0xF3:0x26,order?0x26:0xF3,0xA5); c.sreg[0]=0; c.r[CX]=2; wr16(&c,0,0x20,0x1234); wr16(&c,0,0x22,0x5678);
        CHECK(cpu_step(&c)==CPU_OK); CHECK(rd16(&c,0,0x30)==0x1234 && rd16(&c,0,0x32)==0x5678 && c.r[CX]==0); cpu_free(&c);
    }
    START(0xF2,0xAC); c.r[CX]=3; memcpy(c.mem+0x2020,"abc",3);
    CHECK(cpu_step(&c)==CPU_OK); CHECK((c.r[AX]&255)=='c' && c.r[CX]==0 && c.r[SI]==0x23); cpu_free(&c);
    START(0xA8,0x0F); c.r[AX]=0x10; CHECK(cpu_step(&c)==CPU_OK); CHECK(c.ip==2 && (c.flags&FLAG_ZF) && c.r[AX]==0x10); cpu_free(&c);
    START(0xA9,0x00,0xFF); c.r[AX]=0x100; CHECK(cpu_step(&c)==CPU_OK); CHECK(c.ip==3 && !(c.flags&FLAG_ZF)); cpu_free(&c);
    START(0xC0,0xE0,4); c.r[AX]=3; CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[AX]==48 && c.ip==3); cpu_free(&c);
    START(0xC1,0xE0,4); c.r[AX]=0x123; CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[AX]==0x1230 && c.ip==3); cpu_free(&c);
    START(0xD5,10); c.r[AX]=0x0909;
    CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[AX]==99 && c.ip==2 && !(c.flags&FLAG_ZF)); cpu_free(&c);
    START(0xD5,16); c.r[AX]=0x1000;
    CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[AX]==0 && (c.flags&FLAG_ZF)); cpu_free(&c);
    START(0xD5,0); c.r[AX]=0xAB80;
    CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[AX]==0x80 && (c.flags&FLAG_SF)); cpu_free(&c);
    START(0xD7); c.r[BX]=0xFFFE; c.r[AX]=0xAB03; c.flags=0xAD7; wr8(&c,0x200,1,7);
    CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[AX]==0xAB07 && c.flags==0xAD7); cpu_free(&c);
    START(0x26,0xD7); c.sreg[0]=0; c.r[BX]=0x20; c.r[AX]=0xAB02; wr8(&c,0,0x22,0x42);
    CHECK(cpu_step(&c)==CPU_OK); CHECK(c.r[AX]==0xAB42); cpu_free(&c);
    /* BSUM's hexadecimal conversion sequence exercises DAS for every digit. */
    for(int digit=0;digit<16;digit++) {
        START(0x3C,0x0A,0x1C,0x69,0x2F); c.r[AX]=digit;
        CHECK(cpu_step(&c)==CPU_OK); CHECK(cpu_step(&c)==CPU_OK); CHECK(cpu_step(&c)==CPU_OK);
        CHECK(c.r[AX]==(digit<10?'0'+digit:'A'+digit-10)); cpu_free(&c);
    }
    puts("PASS: byte/word strings, REP/REPE/REPNE, zero counts, direction, segment prefixes, TEST, shifts, XLAT, AAD and DAS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="dosrun-strings-") as tmp:
    src=Path(tmp)/"test.c"; src.write_text(HARNESS)
    exe=Path(tmp)/"test"
    subprocess.run(["gcc","-std=gnu11","-O2","-Wall","-Wextra","-I",str(ROOT/"userland/dosrun"),str(src),str(ROOT/"userland/dosrun/cpu.c"),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
