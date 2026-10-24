// Headless smoke test: boots the wasm build in Node, runs some frames, optionally replays input,
// and writes the framebuffer to a PNG so the UI can actually be looked at.
//   node tests/render_png.js out.png [frames] [script]
// script: comma list of  put:file  rom:file.nes  move:x:y  down:b  up  key:SC  keyup:SC  frames:N  shot:file.png
const fs = require('fs'), path = require('path');

const { writePng } = require('./png.js');

(async () => {
  const out = process.argv[2] || 'out.png';
  const frames = parseInt(process.argv[3] || '5', 10);
  const script = (process.argv[4] || '').split(',').filter(Boolean);
  const dist = path.join(__dirname, '..', 'dist');
  const create = require(path.join(dist, 'nesticle-node.js'));
  const logs = [];
  const M = await create({
    locateFile: f => path.join(dist, f),
    print: s => { logs.push(s); if (process.env.NES_DEBUG) console.log(s); }, printErr: s => { logs.push('ERR ' + s); console.error(s); },
  });
  let vt = 1000;
  M._nes_virtual_time(vt);              // deterministic: one frame = 1/60 s of virtual time
  const rc = M._nes_init();
  if (rc !== 0) { console.log(logs.join('\n')); console.error('nes_init failed', rc); process.exit(1); }
  const shot = file => {
    const w = M._nes_width(), h = M._nes_height();
    const ptr = M._nes_framebuffer() >> 2;
    writePng(file, w, h, new Uint32Array(M.HEAPU32.buffer, ptr * 4, w * h).slice());
    console.log(`wrote ${file} ${w}x${h}`);
  };
  const run = n => { for (let i = 0; i < n; i++) { vt += 1000 / 60; M._nes_virtual_time(vt); M._nes_frame(); } };
  let mx = 0, my = 0, mb = 0;
  run(frames);
  for (const step of script) {
    const [op, a, b] = step.split(':');
    if (op === 'move') { mx = +a; my = +b; M._nes_mouse(mx, my, mb); }
    else if (op === 'down') { mb = +a || 1; M._nes_mouse(mx, my, mb); }
    else if (op === 'up') { mb = 0; M._nes_mouse(mx, my, mb); }
    else if (op === 'key') M._nes_key(+a, 1);
    else if (op === 'keyup') M._nes_key(+a, 0);
    else if (op === 'rom') { const dst = '/' + path.basename(a); M.FS.writeFile(dst, fs.readFileSync(a)); console.log('runrom', dst, '->', M.ccall('nes_runrom', 'number', ['string'], [dst])); }
    else if (op === 'put') M.FS.writeFile('/' + path.basename(a), fs.readFileSync(a));   // add a file without running it
    else if (op === 'frames') run(+a);
    else if (op === 'shot') shot(a);
    else throw new Error('bad script step ' + step);
    if (op !== 'frames' && op !== 'shot') run(2);
  }
  if (process.env.NES_DEBUG) console.log('state pc=%s running=%d paused=%d ticks=%d gui=%d nv=%d', ...[0,1,2,3,4,5].map((i,k)=>k?M._nes_state(i):'0x'+M._nes_state(i).toString(16)));
  shot(out);
  if (logs.length) console.log('--- program output:\n' + logs.join('\n'));
})().catch(e => { console.error(e); process.exit(1); });
