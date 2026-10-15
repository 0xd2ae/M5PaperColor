// Instantiate main/web/e6.wasm the way app.js does and run the full-size pipeline once.
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';

const bytes = readFileSync(new URL('../main/web/e6.wasm', import.meta.url));
const module = await WebAssembly.compile(bytes);
const imports = {};
for (const i of WebAssembly.Module.imports(module)) (imports[i.module] ??= {})[i.name] = () => 0;
const { exports: e } = await WebAssembly.instantiate(module, imports);
e._initialize?.();
e.e6_init();

const W = 600, H = 900, OW = 400, OH = 600;
const src = e.malloc(W * H * 4), out = e.malloc(OW * OH), params = e.malloc(14 * 4), packed = e.malloc(OW * OH / 2);
let rgba = new Uint8Array(e.memory.buffer, src, W * H * 4);
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
  const i = (y * W + x) * 4;
  rgba[i] = x * 255 / W; rgba[i + 1] = y * 255 / H; rgba[i + 2] = 128; rgba[i + 3] = 255;
}
new Float32Array(e.memory.buffer, params, 14).set([0, 0, 1.05, 1.25, 0.25, 0, 0, 1, 0.25, 0.4, 25, 0, 0.44, 2]);
const start = performance.now();
assert.equal(e.e6_process(src, W, H, params, out, OW, OH), 0);
const ms = performance.now() - start;
const idx = new Uint8Array(e.memory.buffer, out, OW * OH);
const counts = [0, 0, 0, 0, 0, 0];
for (const v of idx) { assert.ok(v < 6); counts[v]++; }
assert.ok(counts.filter(Boolean).length >= 4);
e.e6_pack(out, OW * OH, packed);
const p = new Uint8Array(e.memory.buffer, packed, OW * OH / 2);
assert.equal(p[0] >> 4, idx[0]);
const score = e.e6_score(src, W, H, out, OW, OH);
assert.ok(Number.isFinite(score) && score > 0);
console.log(`WASM pipeline ${ms.toFixed(0)} ms, inks ${counts.join('/')}, score ${score.toFixed(2)}`);
