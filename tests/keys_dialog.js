// Settings > Redefine input > Device 1 > Redefine keys...: each button shows its own key, and
// re-assigning one changes only that binding.   node tests/keys_dialog.js <outdir>
const path = require('path');
const { writePng } = require('./png.js');
const outdir = process.argv[2] || '.';
const dist = path.join(__dirname, '..', 'dist');
(async () => {
  const M = await require(path.join(dist, 'nesticle-node.js'))({ locateFile: f => path.join(dist, f), print() {}, printErr: console.error });
  let vt = 1000; M._nes_virtual_time(vt);
  if (M._nes_init() !== 0) throw new Error('init failed');
  const run = n => { for (let i = 0; i < n; i++) { vt += 1000 / 60; M._nes_virtual_time(vt); M._nes_frame(); } };
  const mouse = (x, y, b) => { M._nes_mouse(x, y, b); run(2); };
  const click = (x, y) => { mouse(x, y, 0); mouse(x, y, 1); mouse(x, y, 0); };
  const frame = () => { const w = M._nes_width(), h = M._nes_height(); return { w, h, px: new Uint32Array(M.HEAPU32.buffer, M._nes_framebuffer(), w * h).slice() }; };
  const row = (f, y0) => { const r = []; for (let y = y0; y < y0 + 10; y++) for (let x = 140; x < 208; x++) r.push(f.px[y * f.w + x]); return r.join(','); };
  const km = () => [8, 9, 10, 11].map(i => M._nes_keymap(0, i));
  let failed = 0; const ok = (c, m) => { console.log((c ? 'PASS ' : 'FAIL ') + m); if (!c) failed++; };

  run(3);
  click(113, 4); mouse(120, 14, 0); run(2); mouse(210, 14, 0); run(2); click(210, 14); run(5);   // Settings > Redefine input > Device 1
  click(105, 142); run(5);                                                    // Redefine keys...
  mouse(400, 400, 0);                           // keep the cursor sprite out of the compared rows
  let f = frame();
  const rows = [168, 180, 192, 204].map(y => row(f, y));
  ok(new Set(rows).size === 4, 'B, A, Select and Start fields show four different key names');
  ok(JSON.stringify(km()) === JSON.stringify([0x18, 0x19, 0x1A, 0x26]), 'defaults are O, P, [, L: ' + km().map(v => v.toString(16)));

  click(175, 170); run(5);                      // focus the "B" field
  M._nes_key(0x2C, 1); run(2); M._nes_key(0x2C, 0); run(3);   // press Z
  mouse(400, 400, 0); run(20);
  f = frame();
  const rows2 = [168, 180, 192, 204].map(y => row(f, y));
  ok(rows2[0] !== rows[0] && rows2[1] === rows[1] && rows2[2] === rows[2] && rows2[3] === rows[3], 'only the B field shows the new key; A, Select, Start unchanged');
  writePng(path.join(outdir, 'keys_dialog.png'), f.w, f.h, new Uint32Array(f.px.buffer));
  ok(JSON.stringify(km()) === JSON.stringify([0x18, 0x19, 0x1A, 0x26]), 'stored config is unchanged until "Save settings" (original behaviour)');
  click(209, 59); run(3);                       // close the Redefine keys window
  click(105, 157); run(3);                      // Save settings
  ok(JSON.stringify(km()) === JSON.stringify([0x2C, 0x19, 0x1A, 0x26]), 'after Save settings only B changed: ' + km().map(v => v.toString(16)));
  process.exit(failed ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
