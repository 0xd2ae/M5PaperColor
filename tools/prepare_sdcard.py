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
from collections import OrderedDict
import hashlib
import io
import json
import math
import pickle
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STAGING = ROOT / 'sdcard'
FONTS = STAGING / 'papercolor/fonts'
ORT_VERSION = '1.30.0'
ORT_FILES = ('ort.wasm.min.js', 'ort-wasm-simd-threaded.mjs', 'ort-wasm-simd-threaded.wasm')
RMBG14_TGZ_URL = 'https://registry.npmjs.org/@rmbg/model-briaai/-/model-briaai-0.0.1.tgz'
RMBG14_RAW_SHA256 = 'a6648479275dfd0ede0f3a8abc20aa5c437b394681b05e5af6d268250aaf40f3'
RMBG14_DYN_SHA256 = 'ab074bed4797f57988343aec19691c1e7e8db726a393c86c1bb5ce1147cbbbc8'
LUT3D_LUTS_URL = 'https://raw.githubusercontent.com/HuiZeng/Image-Adaptive-3DLUT/master/pretrained_models/sRGB/LUTs.pth'
LUT3D_LUTS_SHA256 = 'c1bb2bc4b7239c1a7e96159f5923123ba796b1fceb0b8c3132b423ea825b821a'
LUT3D_CLS_URL = 'https://raw.githubusercontent.com/HuiZeng/Image-Adaptive-3DLUT/master/pretrained_models/sRGB/classifier.pth'
LUT3D_CLS_SHA256 = 'bae9865395625ecae58cfe86147e521093bb1e29e7b2544e02adb238b8035021'
SCI_URL = 'https://raw.githubusercontent.com/vis-opt-group/SCI/main/CVPR/weights/medium.pt'
SCI_SHA256 = '4ff1e7e66e8f18114f790e5085ca4c936f0858e329e7ad1cb65b8aa09a9c6916'

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

def _pb_varint(v):
    out = bytearray()
    v &= (1 << 64) - 1
    while v > 0x7f:
        out.append((v & 0x7f) | 0x80)
        v >>= 7
    out.append(v)
    return bytes(out)

def _pb_field(tag, wire, data):
    key = _pb_varint((tag << 3) | wire)
    if wire == 0:
        return key + _pb_varint(data)
    if wire == 5:
        return key + data
    raw = data.encode() if isinstance(data, str) else data
    return key + _pb_varint(len(raw)) + raw

def _pb_tensor(name, dims, raw_bytes, data_type=1):
    b = b''.join(_pb_field(1, 0, d) for d in dims)
    return b + _pb_field(2, 0, data_type) + _pb_field(8, 2, name) + _pb_field(9, 2, raw_bytes)

def _pb_attr_ints(name, ints):
    return _pb_field(1, 2, name) + b''.join(_pb_field(8, 0, v) for v in ints) + _pb_field(20, 0, 7)

def _pb_attr_int(name, val):
    return _pb_field(1, 2, name) + _pb_field(3, 0, val) + _pb_field(20, 0, 2)

def _pb_attr_float(name, val):
    return _pb_field(1, 2, name) + _pb_field(2, 5, struct.pack('<f', val)) + _pb_field(20, 0, 1)

def _pb_node(op, inputs, outputs, attrs=()):
    b = b''.join(_pb_field(1, 2, i) for i in inputs) + b''.join(_pb_field(2, 2, o) for o in outputs)
    return b + _pb_field(4, 2, op) + b''.join(_pb_field(5, 2, a) for a in attrs)

def _pb_value_info(name, dims, elem_type=1):
    shape = b''.join(_pb_field(1, 2, _pb_field(1, 0, d) if isinstance(d, int) else _pb_field(2, 2, d)) for d in dims)
    tt = _pb_field(1, 0, elem_type) + _pb_field(2, 2, shape)
    return _pb_field(1, 2, name) + _pb_field(2, 2, _pb_field(1, 2, tt))

def _download(url, sha256_hex):
    for attempt in range(4):
        try:
            blob = urllib.request.urlopen(url, timeout=60).read()
            break
        except Exception:
            if attempt == 3:
                raise
    if hashlib.sha256(blob).hexdigest() != sha256_hex:
        sys.exit(f'{url} failed its SHA-256 integrity check')
    return blob

class _TorchUnpickler(pickle.Unpickler):
    def find_class(self, module, name):
        if (module, name) == ('torch._utils', '_rebuild_tensor_v2'):
            return lambda storage, offset, sz, stride, req_grad, hooks: (storage[2], offset, sz)
        if (module, name) == ('collections', 'OrderedDict'):
            return OrderedDict
        return lambda *a, **kw: a
    def persistent_load(self, pid):
        return pid

def _load_legacy_pth(blob):
    f = io.BytesIO(blob)
    for _ in range(3):
        pickle.load(f)
    sd = _TorchUnpickler(f).load()
    skeys = pickle.load(f)
    storages = {}
    for k in skeys:
        n = struct.unpack('<q', f.read(8))[0]
        storages[k] = f.read(n * 4)
    return sd, storages

def _load_zip_pth(blob):
    with zipfile.ZipFile(io.BytesIO(blob)) as zf:
        pkl_name = [n for n in zf.namelist() if n.endswith('data.pkl')][0]
        prefix = pkl_name[:-len('data.pkl')]
        sd = _TorchUnpickler(io.BytesIO(zf.read(pkl_name))).load()
        out = {}
        for k, (sk, off, sz) in sd.items():
            if 'tracked' in k:
                continue
            count = 1
            for d in sz:
                count *= d
            raw = zf.read(f'{prefix}data/{sk}')
            out[k] = (sz, raw[off * 4:(off + count) * 4])
        return out

def fetch_rmbg14(dest_file):
    if dest_file.exists() and hashlib.sha256(dest_file.read_bytes()).hexdigest() == RMBG14_DYN_SHA256:
        return
    for attempt in range(4):
        try:
            blob = urllib.request.urlopen(RMBG14_TGZ_URL, timeout=90).read()
            break
        except Exception:
            if attempt == 3:
                raise
    with tarfile.open(fileobj=io.BytesIO(blob)) as tar:
        raw = b''.join(tar.extractfile(f'package/briaai-{i}.onnx').read() for i in range(1, 6))
    if hashlib.sha256(raw).hexdigest() != RMBG14_RAW_SHA256:
        sys.exit('RMBG-1.4 ONNX chunks failed their SHA-256 integrity check')
    # Unlock spatial dimensions from fixed 1024x1024 to dynamic HxW so 512x512 runs 4x faster in WASM.
    old_sig = b'Z+\n\x05input\x12"\n \x08\x01\x12\x1c\n\x0c\x12\nbatch_size\n\x02\x08\x03\n\x03\x08\x80\x08\n\x03\x08\x80\x08'
    new_sig = b'Z+\n\x05input\x12"\n \x08\x01\x12\x1c\n\x0c\x12\nbatch_size\n\x02\x08\x03\n\x03\x12\x01H\n\x03\x12\x01W'
    if raw.count(old_sig) != 1:
        sys.exit('Unexpected input signature in RMBG-1.4 ONNX')
    dest_file.write_bytes(raw.replace(old_sig, new_sig, 1))

def fetch_lut3d_sci(dest_file, size=256):
    sci_w = _load_zip_pth(_download(SCI_URL, SCI_SHA256))
    sd_luts, st_luts = _load_legacy_pth(_download(LUT3D_LUTS_URL, LUT3D_LUTS_SHA256))
    sd_cls, st_cls = _load_legacy_pth(_download(LUT3D_CLS_URL, LUT3D_CLS_SHA256))

    # Transpose each (3, 33^3) LUT to (35937, 3) so Gather(axis=0) fetches RGB together.
    lut_parts = []
    for idx in ('0', '1', '2'):
        sk, off, _ = sd_luts[idx]['LUT']
        raw = st_luts[sk][off * 4:(off + 3 * 35937) * 4]
        vals = struct.unpack(f'<{3 * 35937}f', raw)
        r, g, b = vals[:35937], vals[35937:71874], vals[71874:]
        interleaved = [v for trip in zip(r, g, b) for v in trip]
        lut_parts.append(struct.pack(f'<{3 * 35937}f', *interleaved))
    lut_basis_bytes = b''.join(lut_parts)

    inits = []
    nodes = []
    def add_f32(name, dims, floats_or_bytes):
        raw = floats_or_bytes if isinstance(floats_or_bytes, (bytes, bytearray)) else struct.pack(f'<{len(floats_or_bytes)}f', *floats_or_bytes)
        inits.append(_pb_tensor(name, dims, raw, 1))
    def add_i64(name, dims, ints):
        inits.append(_pb_tensor(name, dims, struct.pack(f'<{len(ints)}q', *ints), 7))

    # Stage 1: SCI Self-Calibrated Illumination (CVPR 2022 Oral) for local shadow & backlit recovery.
    add_f32('sci_in_w', *sci_w['enhance.in_conv.0.weight'])
    add_f32('sci_in_b', *sci_w['enhance.in_conv.0.bias'])
    conv3_pad1 = [_pb_attr_ints('kernel_shape', [3, 3]), _pb_attr_ints('pads', [1, 1, 1, 1])]
    nodes.append(_pb_node('Conv', ['input', 'sci_in_w', 'sci_in_b'], ['sci_c0'], conv3_pad1))
    nodes.append(_pb_node('Relu', ['sci_c0'], ['sci_fea0']))

    cw = struct.unpack('<81f', sci_w['enhance.blocks.0.0.weight'][1])
    cb = struct.unpack('<3f', sci_w['enhance.blocks.0.0.bias'][1])
    bw = struct.unpack('<3f', sci_w['enhance.blocks.0.1.weight'][1])
    bb = struct.unpack('<3f', sci_w['enhance.blocks.0.1.bias'][1])
    bm = struct.unpack('<3f', sci_w['enhance.blocks.0.1.running_mean'][1])
    bv = struct.unpack('<3f', sci_w['enhance.blocks.0.1.running_var'][1])
    sc = [bw[c] / math.sqrt(bv[c] + 1e-5) for c in range(3)]
    fused_w = [cw[i] * sc[i // 27] for i in range(81)]
    fused_b = [(cb[c] - bm[c]) * sc[c] + bb[c] for c in range(3)]
    add_f32('sci_b0_w', (3, 3, 3, 3), fused_w)
    add_f32('sci_b0_b', (3,), fused_b)
    nodes.append(_pb_node('Conv', ['sci_fea0', 'sci_b0_w', 'sci_b0_b'], ['sci_cb0'], conv3_pad1))
    nodes.append(_pb_node('Relu', ['sci_cb0'], ['sci_rb0']))
    nodes.append(_pb_node('Add', ['sci_fea0', 'sci_rb0'], ['sci_fea1']))

    add_f32('sci_out_w', *sci_w['enhance.out_conv.0.weight'])
    add_f32('sci_out_b', *sci_w['enhance.out_conv.0.bias'])
    nodes.append(_pb_node('Conv', ['sci_fea1', 'sci_out_w', 'sci_out_b'], ['sci_cout'], conv3_pad1))
    nodes.append(_pb_node('Sigmoid', ['sci_cout'], ['sci_res']))
    nodes.append(_pb_node('Add', ['input', 'sci_res'], ['sci_illu_raw']))

    add_f32('zero_f', (1,), [0.0])
    add_f32('one_f', (1,), [1.0])
    add_f32('illu_min', (1,), [1e-4])
    add_f32('shadow_slope', (1,), [1.85])
    add_f32('sci_strength', (1,), [0.78])

    nodes.append(_pb_node('Clip', ['sci_illu_raw', 'illu_min', 'one_f'], ['sci_illu']))
    nodes.append(_pb_node('Div', ['input', 'sci_illu'], ['sci_div']))
    nodes.append(_pb_node('Clip', ['sci_div', 'zero_f', 'one_f'], ['sci_r']))

    nodes.append(_pb_node('ReduceMean', ['input'], ['lum_map'], [_pb_attr_ints('axes', [1]), _pb_attr_int('keepdims', 1)]))
    nodes.append(_pb_node('Mul', ['lum_map', 'shadow_slope'], ['lum_scaled']))
    nodes.append(_pb_node('Sub', ['one_f', 'lum_scaled'], ['sh_raw']))
    nodes.append(_pb_node('Clip', ['sh_raw', 'zero_f', 'one_f'], ['sh_clip']))
    nodes.append(_pb_node('Mul', ['sh_clip', 'sh_clip'], ['sh_sq']))
    nodes.append(_pb_node('Mul', ['sh_sq', 'sci_strength'], ['w_local']))
    nodes.append(_pb_node('Sub', ['sci_r', 'input'], ['sci_diff']))
    nodes.append(_pb_node('Mul', ['w_local', 'sci_diff'], ['sci_delta']))
    nodes.append(_pb_node('Add', ['input', 'sci_delta'], ['x_stage1']))

    # Stage 2: Image-Adaptive 3D-LUT (TPAMI 2022, MIT-Adobe FiveK Expert C)
    def cls_blob(key):
        sk, off, sz = sd_cls[key]
        cnt = 1
        for d in sz:
            cnt *= d
        return sz, st_cls[sk][off * 4:(off + cnt) * 4]

    cur = 'x_stage1'
    conv3_s2 = [_pb_attr_ints('kernel_shape', [3, 3]), _pb_attr_ints('strides', [2, 2]), _pb_attr_ints('pads', [1, 1, 1, 1])]
    for c_idx, n_idx in ((1, 3), (4, 6), (7, 9), (10, 12), (13, None)):
        add_f32(f'c{c_idx}_w', *cls_blob(f'model.{c_idx}.weight'))
        add_f32(f'c{c_idx}_b', *cls_blob(f'model.{c_idx}.bias'))
        nodes.append(_pb_node('Conv', [cur, f'c{c_idx}_w', f'c{c_idx}_b'], [f'conv_{c_idx}'], conv3_s2))
        nodes.append(_pb_node('LeakyRelu', [f'conv_{c_idx}'], [f'lrelu_{c_idx}'], [_pb_attr_float('alpha', 0.2)]))
        cur = f'lrelu_{c_idx}'
        if n_idx is not None:
            add_f32(f'in{n_idx}_s', *cls_blob(f'model.{n_idx}.weight'))
            add_f32(f'in{n_idx}_b', *cls_blob(f'model.{n_idx}.bias'))
            nodes.append(_pb_node('InstanceNormalization', [cur, f'in{n_idx}_s', f'in{n_idx}_b'], [f'in_{n_idx}'], [_pb_attr_float('epsilon', 1e-5)]))
            cur = f'in_{n_idx}'

    add_f32('c16_w', *cls_blob('model.16.weight'))
    add_f32('c16_b', *cls_blob('model.16.bias'))
    nodes.append(_pb_node('Conv', [cur, 'c16_w', 'c16_b'], ['pred_4d'], [_pb_attr_ints('kernel_shape', [8, 8])]))
    add_i64('shape_1_3', (2,), [1, 3])
    nodes.append(_pb_node('Reshape', ['pred_4d', 'shape_1_3'], ['pred_1x3']))

    add_f32('lut_basis', (3, 107811), lut_basis_bytes)
    nodes.append(_pb_node('MatMul', ['pred_1x3', 'lut_basis'], ['lut_flat']))
    add_i64('shape_lut', (2,), [35937, 3])
    nodes.append(_pb_node('Reshape', ['lut_flat', 'shape_lut'], ['lut']))

    npx = size * size
    add_f32('scale_f', (1,), [32.0 / 1.0001])
    add_i64('shape_3_n', (2,), [3, npx])
    add_i64('split_1_1_1', (3,), [1, 1, 1])
    add_i64('shape_n', (1,), [npx])
    add_i64('shape_n_1', (2,), [npx, 1])

    nodes.append(_pb_node('Clip', ['x_stage1', 'zero_f', 'one_f'], ['inp_clip']))
    nodes.append(_pb_node('Mul', ['inp_clip', 'scale_f'], ['inp_scaled']))
    nodes.append(_pb_node('Floor', ['inp_scaled'], ['inp_floor']))
    nodes.append(_pb_node('Sub', ['inp_scaled', 'inp_floor'], ['inp_d']))
    nodes.append(_pb_node('Cast', ['inp_floor'], ['inp_id'], [_pb_attr_int('to', 7)]))

    nodes.append(_pb_node('Reshape', ['inp_id', 'shape_3_n'], ['id_3n']))
    nodes.append(_pb_node('Split', ['id_3n', 'split_1_1_1'], ['r_id_1', 'g_id_1', 'b_id_1'], [_pb_attr_int('axis', 0)]))
    for ch in ('r', 'g', 'b'):
        nodes.append(_pb_node('Reshape', [f'{ch}_id_1', 'shape_n'], [f'{ch}_id']))

    nodes.append(_pb_node('Reshape', ['inp_d', 'shape_3_n'], ['d_3n']))
    nodes.append(_pb_node('Split', ['d_3n', 'split_1_1_1'], ['r_d_1', 'g_d_1', 'b_d_1'], [_pb_attr_int('axis', 0)]))
    for ch in ('r', 'g', 'b'):
        nodes.append(_pb_node('Reshape', [f'{ch}_d_1', 'shape_n_1'], [f'{ch}_d']))
        nodes.append(_pb_node('Sub', ['one_f', f'{ch}_d'], [f'{ch}_1md']))

    add_i64('stride_g', (1,), [33])
    add_i64('stride_b', (1,), [1089])
    nodes.append(_pb_node('Mul', ['g_id', 'stride_g'], ['g_off']))
    nodes.append(_pb_node('Mul', ['b_id', 'stride_b'], ['b_off']))
    nodes.append(_pb_node('Add', ['r_id', 'g_off'], ['rg_off']))
    nodes.append(_pb_node('Add', ['rg_off', 'b_off'], ['id000']))

    nodes.append(_pb_node('Mul', ['r_1md', 'g_1md'], ['w_r0g0']))
    nodes.append(_pb_node('Mul', ['r_d', 'g_1md'], ['w_r1g0']))
    nodes.append(_pb_node('Mul', ['r_1md', 'g_d'], ['w_r0g1']))
    nodes.append(_pb_node('Mul', ['r_d', 'g_d'], ['w_r1g1']))

    corners = (
        ('000', 0, 'w_r0g0', 'b_1md'),
        ('100', 1, 'w_r1g0', 'b_1md'),
        ('010', 33, 'w_r0g1', 'b_1md'),
        ('110', 34, 'w_r1g1', 'b_1md'),
        ('001', 1089, 'w_r0g0', 'b_d'),
        ('101', 1090, 'w_r1g0', 'b_d'),
        ('011', 1122, 'w_r0g1', 'b_d'),
        ('111', 1123, 'w_r1g1', 'b_d'),
    )
    acc = None
    for tag, off, wg, wb in corners:
        if off == 0:
            idx_name = 'id000'
        else:
            add_i64(f'off_{tag}', (1,), [off])
            idx_name = f'id{tag}'
            nodes.append(_pb_node('Add', ['id000', f'off_{tag}'], [idx_name]))
        nodes.append(_pb_node('Gather', ['lut', idx_name], [f'v{tag}'], [_pb_attr_int('axis', 0)]))
        nodes.append(_pb_node('Mul', [wg, wb], [f'w{tag}']))
        nodes.append(_pb_node('Mul', [f'v{tag}', f'w{tag}'], [f'vw{tag}']))
        if acc is None:
            acc = f'vw{tag}'
        else:
            nxt = f'sum_{tag}'
            nodes.append(_pb_node('Add', [acc, f'vw{tag}'], [nxt]))
            acc = nxt

    nodes.append(_pb_node('Transpose', [acc], ['out_3n'], [_pb_attr_ints('perm', [1, 0])]))
    add_i64('shape_out', (4,), [1, 3, size, size])
    nodes.append(_pb_node('Reshape', ['out_3n', 'shape_out'], ['out_raw']))
    nodes.append(_pb_node('Clip', ['out_raw', 'zero_f', 'one_f'], ['output']))

    graph = (b''.join(_pb_field(1, 2, n) for n in nodes) +
             _pb_field(2, 2, 'lut3d_sci') +
             b''.join(_pb_field(5, 2, t) for t in inits) +
             _pb_field(11, 2, _pb_value_info('input', [1, 3, size, size])) +
             _pb_field(12, 2, _pb_value_info('output', [1, 3, size, size])))
    dest_file.write_bytes(_pb_field(1, 0, 7) + _pb_field(7, 2, graph) + _pb_field(8, 2, _pb_field(2, 0, 13)))

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('card', nargs='?', type=Path, default=STAGING, help='TF card mount point (default: sdcard/ staging dir)')
    parser.add_argument('--config', type=Path, help='copy this JSON to papercolor/config.json (see config.example.json)')
    parser.add_argument('--no-fonts', action='store_true', help='skip the 9 MB font packs')
    parser.add_argument('--ort', action='store_true', help=f'download onnxruntime-web {ORT_VERSION} and pretrained 3D-LUT+SCI + RMBG-1.4 models')
    parser.add_argument('--no-default-models', action='store_true', help='with --ort, download only the runtime unless model paths are given')
    parser.add_argument('--enhance-model', type=Path, help='image-to-image ONNX model: 1x3xNxN RGB 0..1 in and out')
    parser.add_argument('--enhance-size', type=int, default=256)
    parser.add_argument('--saliency-model', type=Path, help='saliency ONNX model: 1x3xNxN in, 1x1xNxN map out')
    parser.add_argument('--saliency-size', type=int, default=512)
    parser.add_argument('--saliency-mean', type=float, nargs=3, default=[0.5, 0.5, 0.5])
    parser.add_argument('--saliency-std', type=float, nargs=3, default=[1.0, 1.0, 1.0])
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
        if not all((ort / f).exists() for f in ORT_FILES):
            print(f'Downloading onnxruntime-web {ORT_VERSION}…', flush=True)
            fetch_ort(ort)
        manifest = {'runtime': ORT_FILES[0], 'version': ORT_VERSION, 'license': 'MIT (onnxruntime-web)'}
        old = ort / 'manifest.json'
        if old.exists():
            manifest = {**json.loads(old.read_text()), **manifest}
        if args.enhance_model:
            copy(args.enhance_model, ort / 'enhance.onnx')
            manifest['enhance'] = {'file': 'enhance.onnx', 'size': args.enhance_size, 'name': args.enhance_model.stem}
        elif args.ort and not args.no_default_models:
            print('Building pretrained 3D-LUT (FiveK) + SCI enhance.onnx (2.4 MB)…', flush=True)
            fetch_lut3d_sci(ort / 'enhance.onnx', args.enhance_size)
            manifest['enhance'] = {'file': 'enhance.onnx', 'size': args.enhance_size, 'name': '3D-LUT+SCI'}
        if args.saliency_model:
            copy(args.saliency_model, ort / 'saliency.onnx')
            manifest['saliency'] = {'file': 'saliency.onnx', 'size': args.saliency_size,
                                    'mean': args.saliency_mean, 'std': args.saliency_std, 'name': args.saliency_model.stem}
        elif args.ort and not args.no_default_models:
            print('Downloading pretrained RMBG-1.4 INT8 saliency.onnx (44.4 MB)…', flush=True)
            fetch_rmbg14(ort / 'saliency.onnx')
            manifest['saliency'] = {'file': 'saliency.onnx', 'size': args.saliency_size,
                                    'mean': [0.5, 0.5, 0.5], 'std': [1.0, 1.0, 1.0], 'name': 'RMBG-1.4'}
        old.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        if not (manifest.get('enhance') or manifest.get('saliency')):
            print('Note: no model configured yet; the AI button stays hidden until --enhance-model or --saliency-model is given.')
    print(f'Card layout ready in {base}')

if __name__ == '__main__':
    main()
