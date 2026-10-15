#!/usr/bin/env python3
"""Lay out a FAT32 TF card (or a staging directory) for PaperColor.

    papercolor/config.json        optional offline config (plaintext Wi-Fi password and API key)
    papercolor/fonts/font{18,24,36}.bin   large bitmap fonts from generate_assets.py --sd-out
    papercolor/images/            anniversary images, written by the setup page
    papercolor/web/ort/           optional ONNX Runtime Web (WASM backend) and models

Examples:
    tools/prepare_sdcard.py /Volumes/PAPERCOLOR
    tools/prepare_sdcard.py /Volumes/PAPERCOLOR --config my-config.json --ort \\
        --saliency-model u2netp.onnx --saliency-size 320 --saliency-mean 0.485 0.456 0.406 --saliency-std 0.229 0.224 0.225
"""
import argparse
import base64
import hashlib
import io
import json
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STAGING = ROOT / 'sdcard'
FONTS = STAGING / 'papercolor/fonts'
ORT_VERSION = '1.30.0'
ORT_FILES = ('ort.wasm.min.js', 'ort-wasm-simd-threaded.mjs', 'ort-wasm-simd-threaded.wasm')

def ensure_fonts():
    if all((FONTS / f'font{s}.bin').exists() for s in (18, 24, 36)):
        return
    print('Generating card fonts (about 15 s)…', flush=True)
    subprocess.run([sys.executable, str(ROOT / 'tools/generate_assets.py'), '--sd-out', str(FONTS)], check=True)

def copy(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if src.resolve() != dst.resolve():
        shutil.copyfile(src, dst)

def check_config(path):
    data = json.loads(path.read_text(encoding='utf-8'))
    wifi, weather, city = data.get('wifi', {}), data.get('qweather', {}), data.get('city', {})
    missing = [k for k, v in (('wifi.ssid', wifi.get('ssid')), ('qweather.host', weather.get('host')),
                              ('qweather.api_key', weather.get('api_key')), ('city.name', city.get('name'))) if not v]
    if missing:
        sys.exit(f'{path}: missing {", ".join(missing)}')
    password = wifi.get('password') or ''
    if password and not 8 <= len(password) <= 63:
        sys.exit(f'{path}: Wi-Fi password must be 8–63 characters or empty')

def fetch_ort(dest):
    meta = json.load(urllib.request.urlopen(f'https://registry.npmjs.org/onnxruntime-web/{ORT_VERSION}'))
    algorithm, digest = meta['dist']['integrity'].split('-', 1)
    blob = urllib.request.urlopen(meta['dist']['tarball']).read()
    if base64.b64encode(hashlib.new(algorithm, blob).digest()).decode() != digest:
        sys.exit('onnxruntime-web tarball failed its npm integrity check')
    with tarfile.open(fileobj=io.BytesIO(blob)) as tar:
        for name in ORT_FILES:
            (dest / name).write_bytes(tar.extractfile(f'package/dist/{name}').read())
        licence = next((n for n in tar.getnames() if n.lower().startswith('package/licen')), None)
        if licence:
            (dest / 'LICENSE-onnxruntime.txt').write_bytes(tar.extractfile(licence).read())

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('card', nargs='?', type=Path, default=STAGING, help='TF card mount point (default: sdcard/ staging dir)')
    parser.add_argument('--config', type=Path, help='copy this JSON to papercolor/config.json (see config.example.json)')
    parser.add_argument('--no-fonts', action='store_true', help='skip the 9 MB font packs')
    parser.add_argument('--ort', action='store_true', help=f'download onnxruntime-web {ORT_VERSION} (WASM backend)')
    parser.add_argument('--enhance-model', type=Path, help='image-to-image ONNX model: 1x3xNxN RGB 0..1 in and out')
    parser.add_argument('--enhance-size', type=int, default=256)
    parser.add_argument('--saliency-model', type=Path, help='saliency ONNX model: 1x3xNxN in, 1x1xNxN map out')
    parser.add_argument('--saliency-size', type=int, default=320)
    parser.add_argument('--saliency-mean', type=float, nargs=3, default=[0, 0, 0])
    parser.add_argument('--saliency-std', type=float, nargs=3, default=[1, 1, 1])
    args = parser.parse_args()

    base = args.card / 'papercolor'
    for sub in ('fonts', 'images', 'web'):
        (base / sub).mkdir(parents=True, exist_ok=True)
    copy(STAGING / 'papercolor/config.example.json', base / 'config.example.json')
    if not args.no_fonts:
        ensure_fonts()
        for name in ('font18.bin', 'font24.bin', 'font36.bin', 'manifest.json'):
            copy(FONTS / name, base / 'fonts' / name)
    if args.config:
        check_config(args.config)
        copy(args.config, base / 'config.json')
        print('WARNING: config.json on the card holds the Wi-Fi password and API key in plaintext.')

    if args.ort or args.enhance_model or args.saliency_model:
        ort = base / 'web/ort'
        ort.mkdir(parents=True, exist_ok=True)
        if args.ort or not (ort / ORT_FILES[0]).exists():
            print(f'Downloading onnxruntime-web {ORT_VERSION}…', flush=True)
            fetch_ort(ort)
        manifest = {'runtime': ORT_FILES[0], 'version': ORT_VERSION, 'license': 'MIT (onnxruntime-web)'}
        old = ort / 'manifest.json'
        if old.exists():
            manifest = {**json.loads(old.read_text()), **manifest}
        if args.enhance_model:
            copy(args.enhance_model, ort / 'enhance.onnx')
            manifest['enhance'] = {'file': 'enhance.onnx', 'size': args.enhance_size}
        if args.saliency_model:
            copy(args.saliency_model, ort / 'saliency.onnx')
            manifest['saliency'] = {'file': 'saliency.onnx', 'size': args.saliency_size,
                                    'mean': args.saliency_mean, 'std': args.saliency_std}
        old.write_text(json.dumps(manifest, indent=2) + '\n')
        if not (manifest.get('enhance') or manifest.get('saliency')):
            print('Note: no model configured yet; the AI button stays hidden until --enhance-model or --saliency-model is given.')
    print(f'Card layout ready in {base}')

if __name__ == '__main__':
    main()
