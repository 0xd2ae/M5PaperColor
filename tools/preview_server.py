#!/usr/bin/env python3
"""Local configuration-page fixture; does not control a device or call QWeather."""
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path=='/api/status':
            return self.json({'token':'preview','ssid':'Example-WiFi','host':'example.qweatherapi.com','city':'深圳',
                              'location':'101280601','has_key':True,'has_password':True})
        if self.path=='/':
            page=(ROOT/'main/web/index.html').read_bytes()
            self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.end_headers();self.wfile.write(page);return
        self.send_error(404)
    def json(self,body):
        data=json.dumps(body,ensure_ascii=False).encode()
        self.send_response(200);self.send_header('Content-Type','application/json');self.end_headers();self.wfile.write(data)
    def do_POST(self):
        body=self.rfile.read(int(self.headers.get('Content-Length',0)))
        if self.headers.get('X-PC-Token')!='preview':
            self.send_error(403);return
        data=json.loads(body)
        if self.path=='/api/cities':
            return self.json({'code':'200','location':[{'id':'101280601','name':'深圳','adm1':'广东','adm2':'深圳','lat':'22.54','lon':'114.06','tz':'Asia/Shanghai'},
                                                       {'id':'101280604','name':'南山','adm1':'广东','adm2':'深圳','lat':'22.53','lon':'113.92','tz':'Asia/Shanghai'}]})
        if self.path=='/api/holidays' and 'Years' not in data:
            return self.json({'ok':False,'message':'年度表格式错误'})
        return self.json({'ok':True,'message':'预览模拟接口：操作成功，未修改设备'})
    def log_message(self,*args): pass
if __name__=='__main__':
    print('Configuration UI fixture at http://127.0.0.1:8765 (simulated API)',flush=True)
    HTTPServer(('127.0.0.1',8765),Handler).serve_forever()
