// Runs each audio test ROM headlessly and measures the samples the sound chip produces.
//   node tests/audio.js <outdir>
const path = require('path'), fs = require('fs'), cp = require('child_process');
const dist = path.join(__dirname, '..', 'dist');
const outdir = process.argv[2] || '.';
let failed = 0; const ok = (c, m) => { console.log((c ? 'PASS ' : 'FAIL ') + m); if (!c) failed++; };

async function capture(variant, frames) {
  const rom = path.join(outdir, `audio_${variant}.nes`);
  cp.execFileSync('python3', ['-I', path.join(__dirname, 'make_audio_rom.py'), rom, variant]);
  const M = await require(path.join(dist, 'nesticle-node.js'))({ locateFile: f => path.join(dist, f), print() {}, printErr: console.error });
  let vt = 1000; M._nes_virtual_time(vt);
  if (M._nes_init() !== 0) throw new Error('init failed');
  const run = () => { vt += 1000 / 60; M._nes_virtual_time(vt); M._nes_frame(); };
  for (let i = 0; i < 3; i++) run();
  M.FS.writeFile('/a.nes', fs.readFileSync(rom));
  M.ccall('nes_runrom', 'number', ['string'], ['/a.nes']);
  const rate = M._nes_audio_rate(), buf = M._malloc(8192 * 2), all = [];
  M._nes_audio_clear();                                 // discard anything from before the ROM ran
  for (let f = 0; f < frames; f++) {
    run();
    const n = M._nes_audio_pull(buf, 8192);
    all.push(...new Int16Array(M.HEAP16.buffer, buf, n));
  }
  return { s: Int16Array.from(all), rate };
}
const rms = (s, a = 0, b = s.length) => { let t = 0; for (let i = a; i < b; i++) t += s[i] * s[i]; return Math.sqrt(t / Math.max(1, b - a)); };
const crossings = s => { let n = 0; for (let i = 1; i < s.length; i++) if (s[i - 1] < 0 && s[i] >= 0) n++; return n; };
const skip = (r, sec) => Math.floor(r * sec);

(async () => {
  // 1 second of audio = 60 frames; frames carry exactly rate/60 samples on average
  let { s, rate } = await capture('pulse', 90);
  ok(Math.abs(s.length - 90 * rate / 60) < 2, `one frame of samples per emulated frame (${s.length} samples for 90 frames at ${rate} Hz)`);
  let w = s.subarray(skip(rate, 0.25));
  let hz = crossings(w) / (w.length / rate);
  ok(Math.abs(hz - 440.4) < 3, `pulse timer 253 plays ${hz.toFixed(1)} Hz (hardware: 440.4 Hz)`);
  ok(rms(w) > 1200 && rms(w) < 12000, `pulse level is sensible (rms ${rms(w).toFixed(0)})`);

  ({ s, rate } = await capture('pulse_off', 40));
  ok(rms(s) < 20, `channel not enabled in $4015 is silent (rms ${rms(s).toFixed(1)})`);

  ({ s, rate } = await capture('pulse_len', 40));
  ok(rms(s, 0, skip(rate, 0.05)) > 800 && rms(s, skip(rate, 0.25)) < 50, `length counter silences the note (start rms ${rms(s, 0, skip(rate, 0.05)).toFixed(0)}, later ${rms(s, skip(rate, 0.25)).toFixed(0)})`);

  ({ s, rate } = await capture('pulse_env', 70));
  const early = rms(s, skip(rate, 0.05), skip(rate, 0.2)), late = rms(s, skip(rate, 0.95), skip(rate, 1.1));
  ok(late < early * 0.5 && late > 0, `decay envelope fades the note (rms ${early.toFixed(0)} -> ${late.toFixed(0)})`);

  ({ s, rate } = await capture('triangle', 90));
  w = s.subarray(skip(rate, 0.25)); hz = crossings(w) / (w.length / rate);
  ok(Math.abs(hz - 440.4) < 3, `triangle timer 126 plays ${hz.toFixed(1)} Hz (hardware: 440.4 Hz)`);
  ok(rms(w) > 800, `triangle is audible (rms ${rms(w).toFixed(0)})`);

  ({ s, rate } = await capture('noise', 60));
  w = s.subarray(skip(rate, 0.25));
  ok(rms(w) > 1000 && crossings(w) / (w.length / rate) > 1500, `noise is loud and broadband (rms ${rms(w).toFixed(0)}, ${(crossings(w) / (w.length / rate)).toFixed(0)} crossings/s)`);

  ({ s, rate } = await capture('dmc', 90));
  w = s.subarray(skip(rate, 0.25)); hz = crossings(w) / (w.length / rate);
  // 17-byte loop (FF 00 x8, FF): 8 up-ramps per 136 bits at 1789773/54 bits/s = 1949.6 Hz
  ok(Math.abs(hz - 1949.6) < 20, `sample channel plays its looped ramp at ${hz.toFixed(1)} Hz (hardware: 1949.6 Hz)`);
  process.exit(failed ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
