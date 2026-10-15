#!/usr/bin/env python3
"""Rasterize pinned QWeather glyphs to RLE masks; retain upstream silhouettes."""
import hashlib
import json
import struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'assets/qweather'
OUT=ROOT/'main/generated'
def rle(values):
    result=bytearray();previous=0;count=0
    for pixel in values:
        value=int(pixel>=128)
        if count and (value!=previous or count==127):
            result.append((previous<<7)|count);count=0
        previous=value;count+=1
    if count:result.append((previous<<7)|count)
    return result

def main():
    mapping=json.loads((ASSETS/'codepoints.json').read_text())
    glyphs=[]
    for name,cp in mapping.items():
        code=name.removesuffix('-fill')
        if code.isdigit() and 100<=int(code)<=999:
            glyphs.append((int(code)*2+int(name.endswith('-fill')),cp))
    glyphs.sort()
    assert 999*2 in {key for key,cp in glyphs}
    manifest=json.loads((ASSETS/'source.json').read_text())
    manifest['font_sha256']=hashlib.sha256((ASSETS/'icons.ttf').read_bytes()).hexdigest()
    manifest['mapping_sha256']=hashlib.sha256((ASSETS/'codepoints.json').read_bytes()).hexdigest()
    manifest['sizes']={}
    for size in (20,30,80):
        font=ImageFont.truetype(str(ASSETS/'icons.ttf'),size*4)
        entries,data=bytearray(),bytearray()
        for key,cp in glyphs:
            ch=chr(cp);x0,y0,x1,y1=font.getbbox(ch)
            im=Image.new('L',(size*4,size*4))
            ImageDraw.Draw(im).text(((size*4-(x1-x0))/2-x0,(size*4-(y1-y0))/2-y0),ch,font=font,fill=255)
            im=im.resize((size,size),Image.Resampling.LANCZOS)
            packed=rle(im.get_flattened_data())
            entries+=struct.pack('<HHII',key,0,len(data),len(packed));data+=packed
        blob=struct.pack('<4sHH',b'PCI1',size,len(glyphs))+entries+data
        (OUT/f'icons{size}.bin').write_bytes(blob)
        manifest['sizes'][str(size)]={'glyphs':len(glyphs),'bytes':len(blob)}
    (OUT/'icons-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(manifest,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
