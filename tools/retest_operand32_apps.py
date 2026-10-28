#!/usr/bin/env python3
"""Uses local audit assets only; writes no application binaries into Git.
See docs/compatibility/2026-10-25-operand32-retests.md for asset provenance.
"""
from pathlib import Path
import sys, json, subprocess, tempfile, hashlib, time
import argparse
parser=argparse.ArgumentParser(description="Retest locally available DOS apps on freshly formatted FAT16 disks")
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--marathon-assets',type=Path,default=Path('/tmp/manios-marathon-200'))
args=parser.parse_args()
G=Path(__file__).resolve().parents[1]; A=G.parent; O=args.output.resolve()
if O.is_relative_to(G): raise SystemExit('Output must be outside the repository (third-party assets)')
O.mkdir(parents=True,exist_ok=False)
sys.path.insert(0,str(G/'tests'))
from dosrun_test import build_disk, mt, put
from console_test import Machine,SHELL_PROMPT,TestFailure
from gfx_test import screendump
S=args.marathon_assets
cases=[
('pedit','dosrun C:/PEDIT.EXE FIXTURE.TXT', A/'catalog-audit/extracted/pedit', None),
('pedit-light','dosrun C:/PEDITLGT.EXE FIXTURE.TXT',A/'catalog-audit/extracted/pedit',None),
('directory-maven','dosrun C:/DM.EXE /sC',A/'catalog2-audit/extracted/dmaven',None),
('foxtype','dosrun C:/FOXTYPE.EXE C:/FIXTURE.TXT',S/'stage-foxtype.exe','FOXTYPE.EXE'),
('lha','dosrun C:/LHA.EXE a C:/TEST.LZH C:/FIXTURE.TXT',S/'stage-lha.exe','LHA.EXE'),
('lzop','dosrun C:/LZOP.EXE C:/FIXTURE.TXT',S/'stage-lzop.exe','LZOP.EXE'),
('unrar','dosrun C:/UNRAR.EXE',S/'stage-unrar.exe','UNRAR.EXE'),
('dos32a','dosrun C:/DOS32A.EXE /?',S/'stage-dos32a.exe','DOS32A.EXE'),
]
# Extract game packages from existing audit disks, then format fresh disks.
meta={r['id']:r for r in json.loads((A/'games-audit/images.json').read_text())}
for name in ['shufflev','minemay','mahjong']:
 d=O/(name+'-assets');d.mkdir(exist_ok=True)
 subprocess.run(['mcopy','-s','-o','-i',str(A/'games-audit/disks'/f'{name}.img')+'@@1048576','::*',str(d)],check=True)
 cases.append(({'minemay':'mine-mayhem'}.get(name,name),'dosrun C:/'+meta[name]['main'],d,None))
mem = O/'mem-assets.exe'
with mem.open('wb') as f:
 subprocess.run(['7z','e','-so',str(A/'freedos-bonus-audit/FD14BNS.iso'),'FDOS-x86/FREEDOS/BIN/MEM.EXE'],stdout=f,check=True)
cases.append(('freedos-mem','dosrun C:/MEM.EXE',mem,'MEM.EXE'))
rows=[]
for name,cmd,src,dest in cases:
 disk=O/(name+'.img'); tmp=build_disk(str(disk))
 mt(str(disk),'mdel','::*')
 if src.is_dir():
  for p in src.iterdir():mt(str(disk),'mcopy','-s',str(p),'::/')
 else: put(str(disk),dest,src.read_bytes(),tmp)
 put(str(disk),'FIXTURE.TXT',b'alpha\r\nbeta target\r\ngamma\r\n',tmp)
 record={'id':name,'command':cmd,'kernel_sha256':hashlib.sha256((G/'build/manios-zkt.elf').read_bytes()).hexdigest(),'inputs':{str(p.relative_to(src) if src.is_dir() else p.name):hashlib.sha256(p.read_bytes()).hexdigest() for p in (src.rglob('*') if src.is_dir() else [src]) if p.is_file()}}
 m=Machine(str(G/'build/manios-zkt.elf'),['-drive',f'file={disk},format=raw,if=ide'],memory=64)
 try:
  m.expect(SHELL_PROMPT,timeout=60);m.type_serial('cd /n/ata0p1\n');m.expect(SHELL_PROMPT)
  m.type_serial(cmd+'\n')
  try:record['output']=m.expect(SHELL_PROMPT,timeout=12);record['returned']=True
  except TestFailure:record['output']=m.output[m.mark:].decode(errors='replace');record['returned']=False
  screendump(m,str(O),name)
  record['full_log'] = name + '.log'
  record['diagnostics'] = [line for line in m.output.decode(errors='replace').splitlines() if 'dosrun:' in line]
  record['screenshot']=name+'.ppm'
 finally:(O/(name+'.log')).write_bytes(m.output);m.close()
 rows.append(record);(O/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(name,record['returned'],record['output'][-500:].replace('\n',' | '),flush=True)
