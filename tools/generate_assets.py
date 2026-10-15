#!/usr/bin/env python3
"""Reproducible font and lunar resources. Run from any directory with tools venv."""
import datetime as dt
import hashlib
import json
import struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont
from lunar_python import Solar

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'main/generated'
SOURCE = ROOT / 'assets/fonts/source.ttf'
MEDIUM = ROOT / 'assets/fonts/source-medium.ttf'
# Derived bitmap packs deliberately have a new name (OFL Reserved Font Names).
UI = '''一二三四五六日年月星期今天明后上下午正冬腊初十廿卅闰零〇甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申酉戌亥鼠牛虎兔龙蛇马羊猴鸡狗猪春节元宵端午七夕中秋重阳除劳动国庆清休班假调安排数据导入本年度未联网离线天气暂无已过期更新请配置手机连接热点浏览返回城市温度最低最高晴阴多云雨雪小中大暴雷阵雾霾风沙尘冻冰台强热冷米每秒湿百电量估算未知时间设置同步失败正在获取长按顶部进入退出分钟二维码星期资料晴空生活每日中国日历和丰收迎秋待校时无法读取屏幕刷新超时缺失农历不支持夏春秋冬度来源设置成功保存预报风向北南东西东北西北东南西南摄氏露点体感薄雾浮扬沙特极雹雨夹雪热带飓暴风骤疏散雾露晴间阴月休班离线今天明天后天'''
UI += ''.join(p.read_text() for p in (ROOT/'main').glob('*.c'))
ASCII = ''.join(chr(i) for i in range(32,127)) + '°℃%→−?·：／（）—'
# WenKai strokes are thinner than a pixel at small sizes; a 50% cutoff breaks them apart.
THRESHOLD = {14:80, 18:80, 24:96}
# Lower cutoff keeps Medium visibly heavier; below ~48 dense glyphs such as 霾 fill in.
MEDIUM_THRESHOLD = 56
SOLAR_TERMS = ['小寒','大寒','立春','雨水','惊蛰','春分','清明','谷雨','立夏','小满','芒种','夏至',
               '小暑','大暑','立秋','处暑','白露','秋分','寒露','霜降','立冬','小雪','大雪','冬至']

def rle(pixels, threshold=128):
    result = bytearray()
    last, count = 0, 0
    for pixel in pixels:
        value = int(pixel >= threshold)
        if count and (value != last or count == 127):
            result.append((last << 7) | count)
            count = 0
        last, count = value, count + 1
    if count:
        result.append((last << 7) | count)
    return result

def font_pack(size, chars, cmap, source=SOURCE, suffix='', threshold=None):
    font = ImageFont.truetype(str(source), size)
    ascent, _ = font.getmetrics()
    chars = sorted(set(chars) & set(map(chr,cmap)))
    entries, data = bytearray(), bytearray()
    for ch in chars:
        x0,y0,x1,y1 = font.getbbox(ch, anchor='ls')
        w,h = max(0,x1-x0),max(0,y1-y0)
        image = Image.new('L',(max(1,w),max(1,h)))
        ImageDraw.Draw(image).text((-x0,-y0),ch,font=font,fill=255,anchor='ls',stroke_width=0)
        bitmap = rle(image.get_flattened_data(),threshold or THRESHOLD.get(size,128)) if w*h else bytearray()
        entries += struct.pack('<IIHBBBbb5x',ord(ch),len(data),len(bitmap),w,h,
                               round(font.getlength(ch)),x0,ascent+y0)
        data += bitmap
    pack = struct.pack('<4sHHI',b'PCF1',size,ascent,len(chars)) + entries + data
    (OUT/f'font{size}{suffix}.bin').write_bytes(pack)
    return {'glyphs':len(chars),'bytes':len(pack)}

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    source_font = TTFont(SOURCE)
    cmap = source_font.getBestCmap()
    medium_font = TTFont(MEDIUM)
    medium_cmap = medium_font.getBestCmap()
    gb = []
    for high in range(0xB0,0xF8):
        for low in range(0xA1,0xFF):
            try: gb.append(bytes([high,low]).decode('gb2312'))
            except UnicodeDecodeError: pass
    info = {'source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
            'source_url':'https://github.com/lxgw/LxgwWenKai-Lite',
            'source_version':source_font['name'].getDebugName(5),
            'medium_sha256':hashlib.sha256(MEDIUM.read_bytes()).hexdigest(),
            'medium_version':medium_font['name'].getDebugName(5),
            'bitmap_family':'PaperCalendarBitmap','fonts':{}}
    for size in (14,18,24,48,64,88):
        chars = ASCII + UI + (''.join(gb) if size == 18 else '')
        if size >= 48: chars = '0123456789-°℃?'
        info['fonts'][str(size)] = font_pack(size,chars,cmap)
        if size in (18,24): info['fonts'][f'{size}m'] = font_pack(size,chars,medium_cmap,MEDIUM,'m',MEDIUM_THRESHOLD)
    first,last = dt.date(2000,1,1),dt.date(2099,12,31)
    data = bytearray()
    terms = bytearray(24*(last.year-first.year+1))
    day = first
    while day <= last:
        lunar = Solar.fromYmd(day.year,day.month,day.day).getLunar()
        data += struct.pack('<Hbb',lunar.getYear(),lunar.getMonth(),lunar.getDay())
        name = lunar.getJieQi()
        if name:
            index = SOLAR_TERMS.index(name)
            assert index//2 == day.month-1
            terms[(day.year-first.year)*24+index] = day.day
        day += dt.timedelta(days=1)
    assert all(terms)
    (OUT/'lunar.bin').write_bytes(data)
    (OUT/'terms.bin').write_bytes(terms)
    info['lunar'] = {'first':str(first),'last':str(last),'bytes':len(data),
                     'generator':'lunar-python 1.4.8'}
    info['terms'] = {'first':first.year,'last':last.year,'bytes':len(terms),
                     'layout':'24 day-of-month bytes per year, 小寒 first'}
    (OUT/'manifest.json').write_text(json.dumps(info,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(info,ensure_ascii=False,indent=2))

if __name__ == '__main__': main()
