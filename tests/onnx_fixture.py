#!/usr/bin/env python3
"""Tiny ONNX models with the same I/O contract as the optional card models, for page tests.

enhance:  1x3xNxN RGB 0..1 -> 1x3xNxN, brightens by 15 %.
saliency: 1x3xNxN -> 1x1xNxN, channel mean (bright areas are "salient").
Needs the onnx package: pip install --target tests/.build/pydeps onnx
"""
import sys
from pathlib import Path
import onnx
from onnx import TensorProto, helper

def save(path, nodes, out_channels, size, inits=()):
    x = helper.make_tensor_value_info('input', TensorProto.FLOAT, [1, 3, size, size])
    y = helper.make_tensor_value_info('output', TensorProto.FLOAT, [1, out_channels, size, size])
    graph = helper.make_graph(nodes, path.stem, [x], [y], initializer=list(inits))
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid('', 18)])
    model.ir_version = 8
    onnx.checker.check_model(model)
    onnx.save(model, path)

def main(out):
    out.mkdir(parents=True, exist_ok=True)
    gain = helper.make_tensor('gain', TensorProto.FLOAT, [], [1.15])
    save(out / 'enhance.onnx', [helper.make_node('Mul', ['input', 'gain'], ['output'])], 3, 256, [gain])
    axes = helper.make_tensor('axes', TensorProto.INT64, [1], [1])
    save(out / 'saliency.onnx', [helper.make_node('ReduceMean', ['input', 'axes'], ['output'], keepdims=1)], 1, 320, [axes])
    print(f'wrote {out}/enhance.onnx and {out}/saliency.onnx')

if __name__ == '__main__':
    main(Path(sys.argv[1] if len(sys.argv) > 1 else 'tests/.build/onnx'))
