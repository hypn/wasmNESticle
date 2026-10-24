// An invalid opcode reached inside an IRQ handler must stop the CPU (NESticle's own message), not hang
// the emulator. A hang cannot be caught in-process, so the check runs in a child with a time limit.
//   node tests/irq_jam.js <outdir>
const path = require('path'), fs = require('fs'), cp = require('child_process');
const dist = path.join(__dirname, '..', 'dist');
if (process.argv[2] === '--child') {
  (async () => {
    const lines = [];
    const M = await require(path.join(dist, 'nesticle-node.js'))({ locateFile: f => path.join(dist, f), print: s => lines.push(s), printErr() {} });
    let vt = 1000; M._nes_virtual_time(vt);
    if (M._nes_init() !== 0) process.exit(2);
    const run = n => { for (let i = 0; i < n; i++) { vt += 1000 / 60; M._nes_virtual_time(vt); M._nes_frame(); } };
    run(3); M.FS.writeFile('/j.nes', fs.readFileSync(process.argv[3])); M.ccall('nes_runrom', 'number', ['string'], ['/j.nes']);
    run(120);
    console.log(JSON.stringify({ running: M._nes_state(1), msgs: lines.filter(l => /Unrecognized|stopped/.test(l)) }));
  })();
} else {
  const outdir = process.argv[2] || '.', rom = path.join(outdir, 'irqjam.nes');
  cp.execFileSync('python3', ['-I', path.join(__dirname, 'make_irqjam_rom.py'), rom]);
  const r = cp.spawnSync('node', [__filename, '--child', rom], { timeout: 20000, encoding: 'utf8' });
  const ok = (c, m) => { console.log((c ? 'PASS ' : 'FAIL ') + m); if (!c) process.exitCode = 1; };
  if (r.error && r.error.code === 'ETIMEDOUT') ok(false, 'the emulator hung (no result within 20 s)');
  else {
    const out = JSON.parse(r.stdout.trim().split('\n').pop());
    ok(true, 'the emulator did not hang');
    ok(out.running === 0 && out.msgs.some(m => /Unrecognized opcode 2/.test(m)), `the CPU was stopped with NESticle's message: ${out.msgs.join(' | ')}`);
  }
}
