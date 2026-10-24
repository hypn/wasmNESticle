// A scrolling ROM (all four nametables, partly off-screen at almost every position) must run for
// ~25 s of emulated time without the emulator trapping, and the picture must keep changing.
//   node tests/scroll.js <outdir>
const path = require('path'), fs = require('fs'), cp = require('child_process'), crypto = require('crypto');
const dist = path.join(__dirname, '..', 'dist'), outdir = process.argv[2] || '.';
(async () => {
  const rom = path.join(outdir, 'scroll.nes');
  cp.execFileSync('python3', ['-I', path.join(__dirname, 'make_scroll_rom.py'), rom]);
  const M = await require(path.join(dist, 'nesticle-node.js'))({ locateFile: f => path.join(dist, f), print() {}, printErr: console.error });
  let vt = 1000; M._nes_virtual_time(vt);
  if (M._nes_init() !== 0) throw new Error('init failed');
  const run = () => { vt += 1000 / 60; M._nes_virtual_time(vt); M._nes_frame(); };
  for (let i = 0; i < 3; i++) run();
  M.FS.writeFile('/s.nes', fs.readFileSync(rom));
  M.ccall('nes_runrom', 'number', ['string'], ['/s.nes']);
  const seen = new Set(); let f = 0, trap = null;
  try {
    for (; f < 1500; f++) {
      run();
      if (f % 7 === 0) { const w = M._nes_width() * M._nes_height(); seen.add(crypto.createHash('md5').update(Buffer.from(M.HEAPU32.buffer, M._nes_framebuffer(), w * 4)).digest('hex')); }
    }
  } catch (e) { trap = e; }
  const ok = (c, m) => { console.log((c ? 'PASS ' : 'FAIL ') + m); if (!c) process.exitCode = 1; };
  ok(!trap, trap ? `emulator trapped at frame ${f}: ${trap.message}` : 'scrolled through all four nametables for 1500 frames without a trap');
  ok(seen.size > 100, `the picture keeps changing (${seen.size} distinct frames sampled)`);
})();
