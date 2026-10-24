#!/usr/bin/env python3
"""Run the real VGA console against memory and port/IRQ stubs."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='vga-wrap-') as tmp:
    root = Path(tmp)
    (root/'cpu.h').write_text('''#include <stdint.h>
static uint32_t cpu_irq_save(void){return 0;}
static void cpu_irq_restore(uint32_t f){(void)f;}
''')
    (root/'io.h').write_text('''#include <stdint.h>
static void outb(uint16_t p,uint8_t b){(void)p;(void)b;}
''')
    (root/'memlayout.h').write_text('''#include <stdint.h>
static uint16_t hardware[2000];
#define P2V(x) hardware
''')
    source = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "VGA_SOURCE"
int device_register(struct device *d){(void)d;return 0;}
#define WRITE(s) vga_write(s,strlen(s))
int main(void){
 vga_clear();WRITE("top");WRITE("\033[25;80H!");
 assert((hardware[0]&255)=='t'&&(hardware[1999]&255)=='!');
 /* Positioning after bottom-right must cancel deferred wrap. */
 WRITE("\033[2;1Hnext");assert((hardware[0]&255)=='t');
 assert((hardware[80]&255)=='n');
 /* A following printable really does wrap and scroll. */
 WRITE("\033[25;80HX");WRITE("\033[31mY");
 assert((hardware[1999-80]&255)=='X'&&(hardware[1920]&255)=='Y');
 vga_clear();WRITE("\033[1;80HAB");assert((hardware[79]&255)=='A'&&(hardware[80]&255)=='B');
 vga_clear();WRITE("\033[1;80HA\rB");assert((hardware[0]&255)=='B'&&(hardware[80]&255)==' ');
 vga_clear();WRITE("\033[1;80HA\nB");assert((hardware[80]&255)=='B'&&(hardware[160]&255)==' ');
 vga_clear();WRITE("\033[1;80HA\bB");assert((hardware[78]&255)=='B');
 vga_clear();WRITE("\033[1;73H\tB");assert((hardware[80]&255)=='B'&&(hardware[88]&255)==' ');
 /* Fill all 2000 cells, then repaint one cell without screen movement. */
 vga_clear();char full[2000];memset(full,'x',sizeof(full));vga_write(full,sizeof(full));
 for(int i=0;i<2000;i++)assert((hardware[i]&255)=='x');
 WRITE("\033[1;1HZ");assert((hardware[0]&255)=='Z'&&(hardware[1999]&255)=='x');
 puts("PASS: VGA deferred wrapping, bottom-right repaint, SGR, CR/LF and backspace");
}
'''.replace('VGA_SOURCE', str(ROOT/'zkt/drivers/vga_text.c'))
    (root/'test.c').write_text(source)
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-I',tmp,str(root/'test.c'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
