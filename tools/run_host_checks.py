#!/usr/bin/env python3
"""Compile the actual portable firmware logic with sanitizers, then export previews."""
from pathlib import Path
import subprocess
import struct
import json
import hashlib
import gzip
import sys
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'tests/.build'
BUILD.mkdir(parents=True,exist_ok=True)
(BUILD/'weather.gz').write_bytes(gzip.compress(b'{"temp":24}',mtime=0))
resources=['font14.bin','font18.bin','font24.bin','font48.bin','font64.bin','font88.bin','font18m.bin','font24m.bin','lunar.bin','terms.bin','icons20.bin','icons30.bin','icons80.bin']
# Host Mach-O symbols have an extra leading underscore, use C asm symbols explicitly.
asm=['.section __TEXT,__const']
for name in resources:
    symbol='_binary_'+name.replace('.','_')
    asm += [f'.globl {symbol}_start',f'{symbol}_start:',f'.incbin "{ROOT / "main/generated" / name}"',f'.globl {symbol}_end',f'{symbol}_end:']
(BUILD/'assets.S').write_text('\n'.join(asm)+'\n')
cmd=['cc','-std=gnu11','-DPC_HOST_CHECKS','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
     '-I'+str(ROOT/'main'),'-I'+str(ROOT/'components/cjson'),
     str(ROOT/'tests/host_checks.c'),str(ROOT/'main/calendar.c'),str(ROOT/'main/data.c'),
     str(ROOT/'main/buttons.c'),str(ROOT/'main/render.c'),str(ROOT/'main/compression.c'),str(BUILD/'cJSON.o'),
     str(BUILD/'assets.S'),'-lm','-lz','-o',str(BUILD/'checks')]
subprocess.run(['cc','-g','-fsanitize=address,undefined','-Wno-deprecated-declarations','-I'+str(ROOT/'components/cjson'),'-c',str(ROOT/'components/cjson/cJSON.c'),'-o',str(BUILD/'cJSON.o')],check=True)
subprocess.run(cmd,check=True,cwd=ROOT)
subprocess.run([str(BUILD/'checks')],check=True,cwd=ROOT)
palette=[(22,27,22),(255,255,247),(187,26,28),(249,222,55),(43,82,172),(53,128,58)]
for raw in BUILD.glob('*.raw'):
    data=raw.read_bytes()
    assert len(data)==400*600 and all(p<6 for p in data)
    im=Image.new('RGB',(400,600)); im.putdata([palette[p] for p in data])
    im.save(ROOT/'docs/previews'/f'{raw.stem}.png')
manifest=json.loads((ROOT/'main/generated/manifest.json').read_text())
assert hashlib.sha256((ROOT/'assets/fonts/source.ttf').read_bytes()).hexdigest()==manifest['source_sha256']
assert hashlib.sha256((ROOT/'assets/fonts/source-medium.ttf').read_bytes()).hexdigest()==manifest['medium_sha256']
for size,suffix in ((14,''),(18,''),(24,''),(48,''),(64,''),(88,''),(18,'m'),(24,'m')):
    blob=(ROOT/f'main/generated/font{size}{suffix}.bin').read_bytes()
    magic,s,ascent,count=struct.unpack_from('<4sHHI',blob)
    assert magic==b'PCF1' and s==size
    previous=-1; base=12+count*20
    for i in range(count):
        cp,offset,n,w,h,advance,x,y=struct.unpack_from('<IIHBBBbb',blob,12+i*20)
        assert cp>previous and base+offset+n<=len(blob)
        assert sum(v&127 for v in blob[base+offset:base+offset+n])==w*h
        previous=cp
icons=json.loads((ROOT/'main/generated/icons-manifest.json').read_text())
assert hashlib.sha256((ROOT/'assets/qweather/icons.ttf').read_bytes()).hexdigest()==icons['font_sha256']
assert hashlib.sha256((ROOT/'assets/qweather/codepoints.json').read_bytes()).hexdigest()==icons['mapping_sha256']
for size in (20,30,80):
    blob=(ROOT/f'main/generated/icons{size}.bin').read_bytes()
    magic,s,count=struct.unpack_from('<4sHH',blob)
    assert magic==b'PCI1' and s==size
    keys=set();previous=-1;base=8+count*12
    for i in range(count):
        key,reserved,offset,n=struct.unpack_from('<HHII',blob,8+i*12)
        assert key>previous and base+offset+n<=len(blob)
        assert sum(v&127 for v in blob[base+offset:base+offset+n])==size*size
        keys.add(key);previous=key
    for code in (100,101,150,305,400,501,999):assert code*2 in keys
print('Font/icon integrity and six-color previews passed')
