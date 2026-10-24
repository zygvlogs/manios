#!/usr/bin/env python3
"""Opt-in VIEW 17.2 navigation test; copies the supplied FAT16 image.

Usage: python3 tests/dosrun_view_test.py IMAGE [KERNEL] [EVIDENCE_DIR] [--extended]
IMAGE must contain the original VIEW.EXE and 35,362-byte VIEW.DOC in root.
No downloaded application or modified original fixture is required.
"""
from pathlib import Path
import hashlib
import re
import json
import shutil
import subprocess
import sys
import tempfile
import time
from console_test import Machine, SHELL_PROMPT, TestFailure


def main():
    image = Path(sys.argv[1]).resolve()
    kernel = str(Path(sys.argv[2] if len(sys.argv) > 2 else 'build/manios-zkt.elf').resolve())
    evidence = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else Path(tempfile.mkdtemp(prefix='view-evidence-'))
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='view-fat16-') as tmp:
        disk = Path(tmp)/'view.img'
        shutil.copyfile(image, disk)
        doc = Path(tmp)/'view.doc'
        subprocess.run(['mcopy', '-i', f'{disk}@@1048576', '::/VIEW.DOC', str(doc)], check=True)
        assert len(doc.read_bytes()) == 35362
        assert hashlib.sha256(doc.read_bytes()).hexdigest() == '965c30c2429809c90350c03f8151c4e9117591599804fe0e5b7de2143e831fd2'
        m = Machine(kernel, ['-drive', f'file={disk},format=raw,if=ide'], memory=64)
        def pump(seconds):
            deadline = time.monotonic()+seconds
            while time.monotonic() < deadline:
                m._pump(.05)
        def snapshot(name):
            path = evidence/(name+'.vga')
            path.unlink(missing_ok=True)
            m.monitor.sendall(f'pmemsave 0xb8000 4000 "{path}"\n'.encode())
            deadline = time.monotonic()+5
            while not path.exists() or path.stat().st_size != 4000:
                if time.monotonic() > deadline:
                    raise TestFailure('VGA capture timed out')
                pump(.1)
            data = path.read_bytes()
            text = '\n'.join(data[y*160:(y+1)*160:2].decode('cp437') for y in range(25))
            (evidence/(name+'.txt')).write_text(text)
            m.monitor.sendall(f'screendump {evidence/(name+".ppm")}\n'.encode())
            pump(.2)
            return text
        try:
            m.expect(SHELL_PROMPT, 60)
            m.type_serial('dosrun C:/VIEW.EXE C:/VIEW.DOC\n')
            pump(15)
            initial = snapshot('initial')
            assert 'C:\\VIEW.DOC' in initial
            assert 'Version 17.2' in initial and 'VIEWCFG.COM' in initial
            pages = []
            for i, marker in enumerate(['VIEWSEEK.EXE', 'THE VIEW SCREEN', 'Wordstar']):
                m.monitor.sendall(b'sendkey pgdn 50\n'); pump(8)
                page = snapshot(f'pgdn-{i+1}')
                assert 'C:\\VIEW.DOC' in page, page
                assert marker in page, (i, page)
                assert page != (pages[-1] if pages else initial)
                pages.append(page)
            m.monitor.sendall(b'sendkey pgup 50\n'); pump(8)
            back = snapshot('pgup')
            assert 'THE VIEW SCREEN' in back and 'THE ESC KEY' in back
            assert back == pages[1], 'PgUp must restore the second page exactly'
            if '--extended' in sys.argv:
                history = []
                def move(key, label):
                    m.monitor.sendall(f'sendkey {key} 50\n'.encode()); pump(.7)
                    text = snapshot(label)
                    match = re.search(r'Lines\s+(\d+) to\s+(\d+) of\s+(\d+)', text)
                    assert match, text
                    span = tuple(map(int, match.groups()))
                    history.append({'action': key, 'lines': span})
                    (evidence/'navigation.json').write_text(json.dumps(history, indent=2))
                    return text, span
                first, span = move('home', 'home')
                assert span == (1, 23, 825) and first == initial
                previous = 1
                for n in range(40):
                    last, span = move('pgdn', f'forward-{n:02}')
                    assert span[0] > previous, history
                    previous = span[0]
                    if '[END OF DOCUMENT]' in last:
                        break
                else:
                    raise AssertionError('PgDn never reached EOF')
                assert '[END OF DOCUMENT]' in last
                end, endspan = move('end', 'end')
                assert '[END OF DOCUMENT]' in end and endspan[1] >= 824
                previous = endspan[0]
                for n in range(40):
                    text, span = move('pgup', f'backward-{n:02}')
                    assert span[0] < previous, history
                    previous = span[0]
                    if span[0] == 1:
                        break
                else:
                    raise AssertionError('PgUp never returned to start')
                assert text == initial
                (evidence/'navigation.json').write_text(json.dumps(history, indent=2))
                m.monitor.sendall(b'sendkey f2 50\n'); pump(1)
                snapshot('search-prompt')
                # Physical typing avoids the serial console line discipline.
                for char in 'bookmarks':
                    m.monitor.sendall(f'sendkey {char} 50\n'.encode()); pump(.15)
                m.monitor.sendall(b'sendkey ret 50\n'); pump(2)
                found = snapshot('search-result')
                assert 'BOOKMARKS' in found and found != initial
                m.monitor.sendall(b'sendkey f2 50\n'); pump(2)
                next_found = snapshot('search-next')
                assert 'BOOKMARKS' in next_found and next_found != found
                print('PASS: PgDn to EOF, PgUp to start, Home/End and repeated F2 search')
            m.monitor.sendall(b'sendkey esc 50\n')
            out = m.expect(SHELL_PROMPT, 15)
            assert 'Thank you for using VIEW.' in out
            assert "can't run" not in out
            print('PASS: original VIEW.DOC; three PgDn content changes, PgUp and clean Esc')
            print('Evidence:', evidence)
        finally:
            (evidence/'serial.log').write_bytes(m.output)
            m.close()


if __name__ == '__main__':
    main()
