// Runs tests/out/test.nes in the headless wasm build and checks: NMI fires every frame, OAM DMA +
// sprite rendering, background colours from the NES palette, and the keyboard -> $4016 -> A button path.
//   node tests/rom_features.js <test.nes> <outdir>
const fs = require('fs'), path = require('path');
const { writePng } = require('./png.js');
const [rom, outdir] = [process.argv[2], process.argv[3] || '.'];
const dist = path.join(__dirname, '..', 'dist');
const pal = fs.readFileSync(path.join(__dirname, '..', 'assets', 'NESticle.pal'));
const col = i => [pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]];

(async () => {
  const M = await require(path.join(dist, 'nesticle-node.js'))({ locateFile: f => path.join(dist, f), print() {}, printErr: console.error });
  let vt = 1000; M._nes_virtual_time(vt);
  if (M._nes_init() !== 0) throw new Error('init failed');
  const run = n => { for (let i = 0; i < n; i++) { vt += 1000 / 60; M._nes_virtual_time(vt); M._nes_frame(); } };
  const frame = () => { const w = M._nes_width(), h = M._nes_height(); return { w, h, px: new Uint32Array(M.HEAPU32.buffer, M._nes_framebuffer(), w * h).slice() }; };
  const count = (f, [r, g, b]) => { const want = (0xFF << 24 | b << 16 | g << 8 | r) >>> 0; let n = 0; for (const p of f.px) if (p === want) n++; return n; };
  let failed = 0;
  const ok = (c, m) => { console.log((c ? 'PASS ' : 'FAIL ') + m); if (!c) failed++; };

  run(2);
  M.FS.writeFile('/test.nes', fs.readFileSync(rom));
  M.ccall('nes_runrom', 'number', ['string'], ['/test.nes']);
  run(5);
  const c0 = M._nes_peek(0);
  run(30);
  const c1 = M._nes_peek(0);
  ok(c1 - c0 >= 28 && c1 - c0 <= 32, `NMI fires once per frame (frame counter advanced ${c1 - c0} in 30 frames)`);

  let f = frame();
  ok(count(f, col(0x16)) > 8000, `background drawn in palette colour 0x16 (${count(f, col(0x16))} px)`);
  ok(count(f, col(0x2A)) > 8000, `background colour 2 drawn (${count(f, col(0x2A))} px)`);
  ok(count(f, col(0x27)) === 64, `sprite via OAM DMA: exactly one 8x8 sprite in colour 0x27 (${count(f, col(0x27))} px)`);
  ok(count(f, col(0x30)) === 0, 'A not pressed: no white yet');
  // flip bits: marker pixel (colour 0x11) must land in a different corner of each 8x8 sprite
  const want = (c => (0xFF << 24 | c[2] << 16 | c[1] << 8 | c[0]) >>> 0)(col(0x27)), mark = (c => (0xFF << 24 | c[2] << 16 | c[1] << 8 | c[0]) >>> 0)(col(0x11));
  let x0 = 1e9, y0 = 1e9;
  f.px.forEach((p, i) => { if (p === want) { x0 = Math.min(x0, i % f.w); y0 = Math.min(y0, Math.floor(i / f.w)); } });
  const marks = []; f.px.forEach((p, i) => { if (p === mark) marks.push([i % f.w - x0, Math.floor(i / f.w) - y0]); });
  marks.sort((a, b) => a[0] - b[0]);
  const expected = [[40, 0], [56 + 7, 0], [72, 7], [88 + 7, 7]];     // none, H, V, H+V
  ok(JSON.stringify(marks) === JSON.stringify(expected), `sprite flip bits: markers at ${JSON.stringify(marks)} (want ${JSON.stringify(expected)})`);
  writePng(path.join(outdir, 'feat_idle.png'), f.w, f.h, new Uint32Array(f.px.buffer));

  M._nes_key(0x19, 1);            // 'P' = A button in the default keymap
  run(5);
  f = frame();
  ok(count(f, col(0x30)) > 8000, `holding P (A) turns background colour 1 white (${count(f, col(0x30))} px)`);
  ok(count(f, col(0x16)) === 0, 'red stripes replaced');
  writePng(path.join(outdir, 'feat_A.png'), f.w, f.h, new Uint32Array(f.px.buffer));
  M._nes_key(0x19, 0);
  run(5);
  f = frame();
  ok(count(f, col(0x16)) > 8000 && count(f, col(0x30)) === 0, 'releasing P restores red');
  process.exit(failed ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
