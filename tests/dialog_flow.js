// The in-app path: File -> Load ROM dialog lists a .nes in the virtual FS, double-click loads it,
// CPU -> Reset starts it, and the ROM's NMI counter advances.   node tests/dialog_flow.js <test.nes> <outdir>
const fs = require('fs'), path = require('path');
const { writePng } = require('./png.js');
const [rom, outdir] = [process.argv[2], process.argv[3] || '.'];
const dist = path.join(__dirname, '..', 'dist');
(async () => {
  const M = await require(path.join(dist, 'nesticle-node.js'))({ locateFile: f => path.join(dist, f), print() {}, printErr: console.error });
  let vt = 1000, mx = 0, my = 0, mb = 0; M._nes_virtual_time(vt);
  if (M._nes_init() !== 0) throw new Error('init failed');
  const run = n => { for (let i = 0; i < n; i++) { vt += 1000 / 60; M._nes_virtual_time(vt); M._nes_frame(); } };
  const mouse = (x, y, b) => { mx = x; my = y; mb = b; M._nes_mouse(mx, my, mb); run(2); };
  const click = (x, y) => { mouse(x, y, 0); mouse(x, y, 1); mouse(x, y, 0); };
  const frame = () => { const w = M._nes_width(), h = M._nes_height(); return { w, h, px: new Uint32Array(M.HEAPU32.buffer, M._nes_framebuffer(), w * h).slice() }; };
  const shot = n => { const f = frame(); writePng(path.join(outdir, n), f.w, f.h, new Uint32Array(f.px.buffer)); };
  let failed = 0; const ok = (c, m) => { console.log((c ? 'PASS ' : 'FAIL ') + m); if (!c) failed++; };

  run(3);
  M.FS.writeFile('/My_Game.nes', fs.readFileSync(rom));
  click(12, 4);  click(30, 14);  run(5);               // File -> Load ROM...
  let f = frame();
  const selected = f.px.filter(p => p === 0xFF00AA00 || ((p & 0xFF) < 40 && ((p >> 8) & 0xFF) > 120 && ((p >> 16) & 0xFF) < 40)).length;
  ok(selected > 300, `Load ROM dialog open with a highlighted list entry (${selected} green px)`);
  shot('dlg_open.png');
  ok(M._nes_state(5) === 0, 'no ROM loaded yet');
  click(100, 56); click(100, 56); run(10);             // double-click the entry
  ok(M._nes_state(5) === 1, 'double-click loaded the ROM (video window exists)');
  ok(M._nes_state(1) === 0, 'loading does not auto-start the CPU (as in the original)');
  click(300, 300);                                      // click the desktop so the dialog closes if modal
  click(40, 4); click(50, 14); run(10);                 // CPU -> Reset
  ok(M._nes_state(1) === 1, 'CPU -> Reset starts emulation');
  const c0 = M._nes_peek(0); run(30); const c1 = M._nes_peek(0);
  ok(c1 - c0 >= 28 && c1 - c0 <= 32, `ROM's NMI counter advances (${c1 - c0} in 30 frames)`);
  shot('dlg_running.png');
  process.exit(failed ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
