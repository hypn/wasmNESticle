// Mid-frame scroll changes (split screen). A ROM encodes each nametable row's number in tile colours and each
// pixel row's position inside its tile; the screenshot is decoded back and compared with what the PPU's
// scroll registers say it must show: top section from t (X=11, Y=25), then after the IRQ line L a $2006 write
// loads v (row 42, fine Y 2, coarse X 4) and a $2005 first write sets fine X 6. A mid-frame $2005 *Y* write
// must not move the picture.   node tests/split.js <outdir>
const path = require('path'), fs = require('fs'), cp = require('child_process');
const dist = path.join(__dirname, '..', 'dist'), outdir = process.argv[2] || '.';
const pal = fs.readFileSync(path.join(__dirname, '..', 'assets', 'NESticle.pal'));
const rgb = i => (0xFF000000 | pal[i * 3 + 2] << 16 | pal[i * 3 + 1] << 8 | pal[i * 3]) >>> 0;
const COLOR = new Map([[rgb(0x0F), 0], [rgb(0x16), 1], [rgb(0x2A), 2], [rgb(0x12), 3]]);
let failed = 0; const ok = (c, m) => { console.log((c ? 'PASS ' : 'FAIL ') + m); if (!c) failed++; };

async function capture(L) {
  const rom = path.join(outdir, `split_${L}.nes`);
  cp.execFileSync('python3', ['-I', path.join(__dirname, 'make_split_rom.py'), rom, String(L)]);
  const M = await require(path.join(dist, 'nesticle-node.js'))({ locateFile: f => path.join(dist, f), print() {}, printErr() {} });
  let vt = 1000; M._nes_virtual_time(vt);
  if (M._nes_init() !== 0) throw new Error('init failed');
  const run = n => { for (let i = 0; i < n; i++) { vt += 1000 / 60; M._nes_virtual_time(vt); M._nes_frame(); } };
  run(3); M.FS.writeFile('/s.nes', fs.readFileSync(rom)); M.ccall('nes_runrom', 'number', ['string'], ['/s.nes']); run(20);
  const w = M._nes_width(), vx = M._nes_view(0), vy = M._nes_view(1), vw = M._nes_view(2), vh = M._nes_view(3);
  const px = new Uint32Array(M.HEAPU32.buffer, M._nes_framebuffer(), w * M._nes_height()).slice();
  return { px, w, vx, vy, vw, vh };
}
// what does frame row f show? -> {k: nametable row, r: pixel row in tile} decoded with scroll X sx, or null
function decode(img, f, sx) {
  const at = x => COLOR.get(img.px[(img.vy + f) * img.w + img.vx + ((x % 256) + 256) % 256]);
  const p0 = at(0 - sx), p1 = at(1 - sx);
  if (p0 === undefined || p1 === undefined || p1 > 1) return null;
  let k = 0;
  for (let j = 0; j < 5; j++) { const b = at(j * 8 + 2 - sx); if (b !== 1 && b !== 2) return null; k |= (b - 1) << j; }
  return { k, r: p0 | p1 << 2 };
}
const expect = R => ({ k: Math.floor((R % 240) / 8), r: R % 8 });
const same = (a, b) => a && b && a.k === b.k && a.r === b.r;

(async () => {
  for (const L of [100, 120, 150]) {
    const img = await capture(L);
    ok(img.vw === 256 && img.vh === 224, `L=${L}: game view is 256x224`);
    // frame row f is NES line n=f+8.  Top section: row (25+n); bottom section from line F1: row (42+n-F1), sx=38.
    const rows = Array.from({ length: img.vh }, (_, f) => ({ n: f + 8, top: decode(img, f, 11), bot: decode(img, f, 38) }));
    if (process.env.SPLIT_DEBUG) {   // which hypothesis does each line match?  T=top B=bottom(F1=L+1) b=both .=none
      const F = L + 1; let line = '';
      for (const r of rows) { const t = same(r.top, expect(25 + r.n)), b = same(r.bot, expect(42 + r.n - F)); line += t && b ? 'b' : t ? 'T' : b ? 'B' : '.'; }
      console.log(`L=${L} (line n=8..231):\n` + line.match(/.{1,56}/g).join('\n'));
    }
    let found = null;
    for (let F1 = L - 3; F1 <= L + 6 && !found; F1++) {
      if (rows.every(r => r.n < F1 ? same(r.top, expect(25 + r.n)) : same(r.bot, expect(42 + r.n - F1)))) found = F1;
    }
    ok(found !== null, `L=${L}: every line matches "top scroll (X=11,Y=25), then v=row 42/fineY 2/X=38 from one line F1"`);
    if (found !== null) ok(found >= L && found <= L + 2, `L=${L}: the split starts at line ${found} (IRQ line ${L}; hardware: the line after the IRQ line = ${L + 1})`);
    else {
      const bad = rows.filter(r => !(same(r.top, expect(25 + r.n)) || same(r.bot, expect(42 + r.n - (L + 1))))).slice(0, 4);
      console.log('  first undecodable/mismatching lines:', JSON.stringify(bad.map(b => ({ n: b.n, top: b.top, bot: b.bot }))));
    }
  }
  process.exit(failed ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
