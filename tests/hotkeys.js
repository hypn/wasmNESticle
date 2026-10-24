// Menu hotkeys: F8 toggles sound (and really stops audio), letter shortcuts and arrow keys behave.
//   node tests/hotkeys.js <test.nes>
const path = require('path'), fs = require('fs');
const dist = path.join(__dirname, '..', 'dist');
(async () => {
  const lines = [];
  const M = await require(path.join(dist, 'nesticle-node.js'))({ locateFile: f => path.join(dist, f), print: s => lines.push(s), printErr: console.error });
  let vt = 1000; M._nes_virtual_time(vt);
  if (M._nes_init() !== 0) throw new Error('init failed');
  const run = n => { for (let i = 0; i < n; i++) { vt += 1000 / 60; M._nes_virtual_time(vt); M._nes_frame(); } };
  const tap = sc => { M._nes_key(sc, 1); run(1); M._nes_key(sc, 0); run(1); };
  const since = () => { const l = lines.splice(0); return l.join('|'); };
  let failed = 0; const ok = (c, m) => { console.log((c ? 'PASS ' : 'FAIL ') + m); if (!c) failed++; };

  const audioRom = path.join(__dirname, 'out', 'audio_pulse.nes');
  M.FS.writeFile('/t.nes', fs.readFileSync(audioRom));
  run(3); M.ccall('nes_runrom', 'number', ['string'], ['/t.nes']); run(10);
  const buf = M._malloc(65536); M._nes_audio_clear(); run(10);
  ok(M._nes_audio_pull(buf, 8192) > 5000, 'sound is on: samples are queued');
  since();
  tap(0x42);                                    // F8
  ok(/Sound disabled/.test(since()), 'F8 -> "Sound disabled"');
  M._nes_audio_clear(); run(10);
  ok(M._nes_audio_pull(buf, 8192) === 0, 'sound off: no samples are produced');
  tap(0x42);                                    // F8 again
  ok(/Sound enabled/.test(since()), 'F8 again -> "Sound enabled"');
  M._nes_audio_clear(); run(10);
  ok(M._nes_audio_pull(buf, 8192) > 5000, 'sound back on: samples flow again');

  // keys without a shortcut must not trigger menu items
  since(); for (const sc of [0x48, 0x50, 0x4B, 0x4D, 0x47, 0x4F]) tap(sc);   // arrows, Home, End
  ok(since() === '', 'arrow/Home/End keys trigger no menu commands');
  // another F-key shortcut: F5 = Save NES state
  tap(0x3F);
  ok(/state/i.test(since()), 'F5 (Save NES state) reaches its menu item');
  process.exit(failed ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
