#!/usr/bin/env python3
"""Local configuration-page fixture; does not control a device or call QWeather.

Simulates the setup API including anniversaries, images and /api/render. Frames come from
tests/.build/render_cli (built by tools/run_host_checks.py), i.e. the firmware's own renderer with a
fixed sample forecast. Uploaded files live in a temporary directory; /sdweb/ is served from
sdcard/papercolor/web/ when tools/prepare_sdcard.py has filled it.

usage: preview_server.py [--no-card] [--no-time]
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs
from datetime import datetime, timedelta, timezone
import json, re, struct, subprocess, sys, tempfile, time

ROOT=Path(__file__).resolve().parents[1]
SDWEB=ROOT/'sdcard/papercolor/web'
RENDER=ROOT/'tests/.build/render_cli'
STORE=Path(tempfile.mkdtemp(prefix='papercolor-preview-'))
HOLIDAYS=STORE/'holidays.json'
HOLIDAYS.write_bytes((ROOT/'main/generated/holidays.json').read_bytes())
ITEMS=[]
CARD='--no-card' not in sys.argv
TIME='--no-time' not in sys.argv
LIMITS={'e6':120000,'jpg':1600*1024,'json':8192}
TYPES={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.mjs':'text/javascript; charset=utf-8',
       '.wasm':'application/wasm','.json':'application/json','.onnx':'application/octet-stream'}
FONTS=ROOT/'sdcard/papercolor/fonts'
FETCHED=time.time()

def today():
    return datetime.now(timezone(timedelta(hours=8))).strftime('%Y-%m-%d')
def holiday_years():
    years=sorted(int(y) for y in json.loads(HOLIDAYS.read_text()).get('Years',{}))
    return (years[0],years[-1]) if years else (0,0)
def render(scene,image=b''):
    scene={'today':today(),'time_valid':TIME,'card':CARD,'battery':76,
           'updated':datetime.fromtimestamp(FETCHED,timezone(timedelta(hours=8))).strftime('更新 %m-%d %H:%M'),**scene}
    data=json.dumps(scene,ensure_ascii=False).encode()
    args=[str(RENDER),str(HOLIDAYS)]+([str(FONTS)] if CARD and FONTS.is_dir() else [])
    out=subprocess.run(args,input=struct.pack('<H',len(data))+data+image,capture_output=True,timeout=10)
    if out.returncode or len(out.stdout)!=120000: raise ValueError(out.stderr.decode(errors='replace').strip() or 'render failed')
    return out.stdout
def stored_image(item):
    p=STORE/f"{item['id']}.e6"
    return p.read_bytes() if item.get('image') and p.exists() else b''

class Handler(BaseHTTPRequestHandler):
    def send(self,data,kind,status=200,headers=()):
        self.send_response(status);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)))
        self.send_header('Cache-Control','no-store')
        for k,v in headers: self.send_header(k,v)
        self.end_headers();self.wfile.write(data)
    def json(self,body):
        self.send(json.dumps(body,ensure_ascii=False).encode(),'application/json; charset=utf-8')
    def reply(self,ok,message):
        self.json({'ok':ok,'message':message})
    def frame(self,scene,image=b''):
        if not RENDER.exists(): return self.send(b'run tools/run_host_checks.py first','text/plain',503)
        try: data=render(scene,image)
        except ValueError as e: return self.send(str(e).encode(),'text/plain; charset=utf-8',400)
        status=f"time={int(TIME)},weather=fresh,card={int(CARD)},battery=76"
        self.send(data,'application/octet-stream',headers=[('X-Status',status)])
    def image_query(self,query):
        q=parse_qs(query);i=q.get('id',[''])[0];k=q.get('kind',[''])[0]
        if not re.fullmatch(r'[a-z0-9]{1,16}',i) or k not in LIMITS: return None,None
        return STORE/f'{i}.{k}',k
    def do_GET(self):
        url=urlparse(self.path)
        files={'/':'main/web/index.html','/app.js':'main/web/app.js','/e6.wasm':'main/web/e6.wasm'}
        if url.path in files:
            p=ROOT/files[url.path];return self.send(p.read_bytes(),TYPES[p.suffix])
        if url.path=='/api/status':
            first,last=holiday_years()
            fonts=[s for s in (18,24,36) if CARD and (FONTS/f'font{s}.bin').exists()]
            return self.json({'ssid':'Example-WiFi','host':'example.qweatherapi.com','city':'深圳','location':'101280601',
                              'has_key':True,'has_password':True,'card':CARD,'time_valid':TIME,'now':int(time.time()),
                              'battery':76,'weather':'fresh','weather_fetched':int(FETCHED),'offline':False,
                              'holiday_first':first,'holiday_last':last,'anniversaries':len(ITEMS),'page':'calendar',
                              'celebrate_today':False,'fonts':fonts,'onnx':CARD and (SDWEB/'ort/manifest.json').exists()})
        if url.path=='/api/anniversaries':
            return self.json({'card':CARD,'max':32,'items':ITEMS if CARD else []})
        if url.path=='/api/render':
            q={k:v[0] for k,v in parse_qs(url.query).items()}
            if q.get('page','calendar')=='anniversary':
                items=ITEMS if CARD else []
                index=next((n for n,a in enumerate(items) if a['id']==q.get('id')),0)
                if not items: return self.frame({'kind':'anniversary','count':0})
                return self.frame({'kind':'anniversary','index':index,'count':len(items),'item':items[index]},stored_image(items[index]))
            scene={'kind':'calendar'}
            m=re.fullmatch(r'(\d{4})-(\d{2})',q.get('month',''))
            if 'month' in q and not (m and 2000<=int(m[1])<=2099 and 1<=int(m[2])<=12): return self.send(b'month','text/plain',400)
            if m: scene.update(year=int(m[1]),month=int(m[2]))
            return self.frame(scene)
        if url.path=='/api/image':
            p,_=self.image_query(url.query)
            if not p or not p.exists(): return self.send_error(404)
            return self.send(p.read_bytes(),'image/jpeg' if p.suffix=='.jpg' else 'application/octet-stream')
        if url.path.startswith('/sdweb/'):
            rel=url.path[len('/sdweb/'):]
            p=(SDWEB/rel).resolve()
            if '..' in rel or not str(p).startswith(str(SDWEB.resolve())) or not p.is_file(): return self.send_error(404)
            return self.send(p.read_bytes(),TYPES.get(p.suffix,'application/octet-stream'))
        self.send_error(404)
    def do_POST(self):
        url=urlparse(self.path)
        body=self.rfile.read(int(self.headers.get('Content-Length',0)))
        if url.path=='/api/render':
            if len(body)<2: return self.send(b'size','text/plain',400)
            n=body[0]|body[1]<<8
            if not 0<n<=2048 or len(body)-2-n not in (0,120000): return self.send(b'size','text/plain',400)
            item=json.loads(body[2:2+n]); image=body[2+n:]
            index=next((i for i,a in enumerate(ITEMS) if a['id']==item.get('id')),None)
            count=len(ITEMS)+(index is None)
            if not image and item.get('image'): image=stored_image({**item,'image':True})
            return self.frame({'kind':'anniversary','index':len(ITEMS) if index is None else index,'count':count,'item':item},image)
        if url.path=='/api/image':
            if not CARD: return self.reply(False,'未检测到 TF 卡')
            p,k=self.image_query(url.query)
            if not p: return self.reply(False,'图片参数无效')
            if (k=='e6' and len(body)!=LIMITS[k]) or len(body)>LIMITS[k] or not body: return self.reply(False,'图片大小不正确')
            if k=='e6' and any((b>>4)>5 or (b&15)>5 for b in body): return self.reply(False,'图片大小不正确')
            p.write_bytes(body);return self.reply(True,'已写入 TF 卡')
        data=json.loads(body or b'{}')
        if url.path=='/api/cities':
            return self.json({'code':'200','location':[{'id':'101280601','name':'深圳','adm1':'广东','adm2':'深圳','lat':'22.54','lon':'114.06','tz':'Asia/Shanghai'},
                                                       {'id':'101280604','name':'南山','adm1':'广东','adm2':'深圳','lat':'22.53','lon':'113.92','tz':'Asia/Shanghai'}]})
        if url.path=='/api/holidays':
            if not CARD: return self.reply(False,'未检测到 TF 卡')
            if 'Years' not in data: return self.reply(False,'年度表格式错误')
            HOLIDAYS.write_bytes(body);return self.reply(True,'年度表已保存并生效')
        if url.path=='/api/anniversaries':
            if not CARD: return self.reply(False,'未检测到 TF 卡')
            if not re.fullmatch(r'[a-z0-9]{1,16}',data.get('id','')) or not data.get('name'): return self.reply(False,'纪念日格式错误')
            data['image']=bool(data.get('image')) and (STORE/f"{data['id']}.e6").exists()
            if not data['image']:
                for k in LIMITS: (STORE/f"{data['id']}.{k}").unlink(missing_ok=True)
            for n,a in enumerate(ITEMS):
                if a['id']==data['id']: ITEMS[n]=data;break
            else:
                if len(ITEMS)>=32: return self.reply(False,'纪念日数量已达上限')
                ITEMS.append(data)
            return self.json({'ok':True,'message':'已保存，可在屏幕上预览','missing':'','image':data['image']})
        if url.path=='/api/anniversaries/delete':
            ITEMS[:]=[a for a in ITEMS if a['id']!=data.get('id')]
            for k in LIMITS: (STORE/f"{data.get('id')}.{k}").unlink(missing_ok=True)
            return self.reply(True,'已删除')
        if url.path=='/api/preview':
            return self.reply(True,'预览模拟接口：屏幕不会刷新')
        return self.reply(True,'预览模拟接口：操作成功，未修改设备')
    def log_message(self,*args): pass

if __name__=='__main__':
    print(f'Configuration UI fixture at http://127.0.0.1:8765 (simulated API, files in {STORE})',flush=True)
    ThreadingHTTPServer(('127.0.0.1',8765),Handler).serve_forever()
