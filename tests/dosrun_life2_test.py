#!/usr/bin/env python3
"""Opt-in unmodified Life2 blinker test on a disposable FAT16 image.

Usage: tests/dosrun_life2_test.py IMAGE [KERNEL] [EVIDENCE_DIR]
"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import time
from console_test import Machine, SHELL_PROMPT


def main():
    source = Path(sys.argv[1]).resolve()
    kernel = str(Path(sys.argv[2] if len(sys.argv)>2 else 'build/manios-zkt.elf').resolve())
    evidence = Path(sys.argv[3]).resolve() if len(sys.argv)>3 else Path(tempfile.mkdtemp(prefix='life2-evidence-'))
    evidence.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='life2-fat16-') as tmp:
        disk=Path(tmp)/'life.img';shutil.copyfile(source,disk)
        exe=Path(tmp)/'life2.exe'
        subprocess.run(['mcopy','-i',f'{disk}@@1048576','::/LIFE2.EXE',str(exe)],check=True)
        assert hashlib.sha256(exe.read_bytes()).hexdigest()=='45f45ec0ff09ef9eb7f7a222b255d39ee263f6119467ec28e34a2f042760fd0a'
        m=Machine(kernel,['-drive',f'file={disk},format=raw,if=ide'],memory=64)
        def pump(t):
            until=time.monotonic()+t
            while time.monotonic()<until:m._pump(.03)
        def key(name):
            m.monitor.sendall(f'sendkey {name} 50\n'.encode());pump(.4)
        def cells():
            p=evidence/'current.vga';p.unlink(missing_ok=True)
            m.monitor.sendall(f'pmemsave 0xb8000 4000 "{p}"\n'.encode())
            until=time.monotonic()+5
            while not p.exists() or p.stat().st_size!=4000:
                assert time.monotonic()<until,'VGA capture timeout'
                pump(.03)
            b=p.read_bytes()
            return [(i//80,i%80) for i,ch in enumerate(b[::2]) if ch in (ord('X'),ord('O')) and i//80<23]
        def record(name):
            shutil.copyfile(evidence/'current.vga',evidence/(name+'.vga'))
            m.monitor.sendall(f'screendump {evidence/(name+".ppm")}\n'.encode());pump(.1)
        history=[]
        try:
            m.expect(SHELL_PROMPT,60);m.type_serial('dosrun C:/LIFE2.EXE\n');pump(2)
            def wait_text(marker):
                until=time.monotonic()+10
                while True:
                    cells();screen=(evidence/'current.vga').read_bytes()[::2].decode('cp437')
                    assert 'String formula too complex' not in screen, screen
                    if marker in screen:return
                    assert time.monotonic()<until,marker
                    pump(.1)
            wait_text('spacebar to continue');key('spc')
            wait_text('In EDIT mode');key('spc');wait_text('R=Run')
            pump(1)
            for k in ['m','right','m','right','m']:key(k)
            horizontal=[(11,39),(11,40),(11,41)];vertical=[(10,40),(11,40),(12,40)]
            pump(1);assert cells()==horizontal;record('edited')
            key('r');stage=0;until=time.monotonic()+45
            for target,label in [(horizontal,'horizontal-0'),(vertical,'vertical-1'),(horizontal,'horizontal-2')]:
                while True:
                    state=cells();history.append(state)
                    if state==target:record(label);stage+=1;break
                    assert time.monotonic()<until,f'No complete blinker cycle: reached {stage}/3 states'
                    pump(.15)
            key('q')
            until=time.monotonic()+8
            while True:
                cells()
                screen=(evidence/'current.vga').read_bytes()[::2].decode('cp437')
                if 'Press ESC key to continue' in screen:break
                assert time.monotonic()<until,'Missing Life2 exit confirmation'
                pump(.1)
            record('exit-confirmation');key('esc');out=m.expect(SHELL_PROMPT,15)
            assert "can't run" not in out and 'ran too long' not in out
            print('PASS: native Life2 horizontal → vertical → horizontal without extra keys; Q/Esc exits cleanly')
            print('Evidence:',evidence)
        finally:
            (evidence/'states.json').write_text(json.dumps(history))
            (evidence/'serial.log').write_bytes(m.output);m.close()


if __name__=='__main__':main()
