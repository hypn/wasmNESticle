// Exercises web/index.html's inline script against the real wasm build, using a fake DOM:
// pointer -> menu opens, keyboard -> scancodes, file input -> ROM runs.   node tests/web_glue_test.js <rom.nes> <outdir>
const fs = require('fs'), path = require('path'), vm = require('vm');
const { writePng } = require('./png.js');
const [rom, outdir, audioRom] = [process.argv[2], process.argv[3] || '.', process.argv[4]];
const dist = path.join(__dirname, '..', 'dist');
const html = fs.readFileSync(path.join(dist, 'index.html'), 'utf8');
const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]).join('\n');
if (!/<script src="nesticle.js"><\/script>[\s\S]*<script>/.test(html)) throw new Error('nesticle.js must load before the inline script');

// minimal WebAudio fakes: record what the page does with them
const audioLog = { ctxs: 0, modules: [], nodes: [], posted: [], blobs: {} };
class FakeAudioContext {
  constructor() { audioLog.ctxs++; this.state = 'suspended'; this.destination = {}; this.audioWorklet = { addModule: async url => { audioLog.modules.push(url); } }; }
  resume() { this.state = 'running'; }
}
class FakeAudioWorkletNode {
  constructor(ctx, name, opts) { this.name = name; this.opts = opts; audioLog.nodes.push(this);
    this.port = { postMessage: (m, t) => audioLog.posted.push(m) }; }
  connect() { this.connected = true; }
}
let blobN = 0;
const handlers = {}, els = {};
function el(id) {
  return els[id] ||= {
    id, style: {}, classList: { add() {}, remove() {} }, textContent: '', width: 640, height: 480,
    addEventListener(t, f) { (handlers[id + ':' + t] ||= []).push(f); },
    clicks: 0, click() { this.clicks++; }, focus() {}, setPointerCapture() {}, requestFullscreen() { this.fs = (this.fs || 0) + 1; },
    getBoundingClientRect() { return { left: 0, top: 0, width: parseInt(this.style.width) || 640, height: parseInt(this.style.height) || 480 }; },
    getContext: () => ({
      createImageData: (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }),
      putImageData(img) { el.last = img; },
    }),
  };
}
el('worklet').textContent = html.match(/<script type="text\/plain" id="worklet">([\s\S]*?)<\/script>/)[1];
const fire = (id, t, ev) => (handlers[id + ':' + t] || []).forEach(f => f({ preventDefault() {}, ...ev }));
let raf = null;
const sandbox = {
  console, Uint8Array, Uint8ClampedArray, Set, Object, Math, Promise,
  document: { getElementById: el, fullscreenElement: null, addEventListener(t, f) { (handlers['document:' + t] ||= []).push(f); } },
  window: { innerWidth: 1920, innerHeight: 1080, devicePixelRatio: 1, addEventListener(t, f) { (handlers['window:' + t] ||= []).push(f); } },
  requestAnimationFrame: f => { raf = f; },
  AudioContext: FakeAudioContext, AudioWorkletNode: FakeAudioWorkletNode,
  Blob: class { constructor(parts) { this.text = parts.join(''); } },
  URL: { createObjectURL: b => { const u = 'blob:' + (++blobN); audioLog.blobs[u] = b.text; return u; } },
  createNesticle: require(path.join(dist, 'nesticle.js')),
};
sandbox.createNesticle = (opts) => require(path.join(dist, 'nesticle.js'))({ ...opts, locateFile: f => path.join(dist, f) });
vm.createContext(sandbox);
vm.runInContext(script, sandbox);

(async () => {
  for (let i = 0; i < 200 && !raf; i++) await new Promise(r => setTimeout(r, 25));
  if (!raf) throw new Error('page never reached requestAnimationFrame (init failed?)');
  const step = async n => { for (let i = 0; i < n; i++) { const f = raf; raf = null; f(); await new Promise(r => setTimeout(r, 17)); } };
  const shot = (name) => { const im = el.last; writePng(path.join(outdir, name), im.width, im.height, new Uint32Array(im.data.buffer)); console.log('wrote', name, im.width + 'x' + im.height); };
  const count = pred => { const d = el.last.data; let n = 0; for (let i = 0; i < d.length; i += 4) if (pred(d[i], d[i+1], d[i+2])) n++; return n; };
  const ok = (c, m) => { console.log((c ? 'PASS ' : 'FAIL ') + m); if (!c) process.exitCode = 1; };

  await step(3);
  ok(el('status').textContent === '', 'status line is empty when nothing is wrong');
  ok(!/id="fs"|id="scale"|Open ROM/.test(html), 'no Fullscreen/Scale/Open ROM buttons in the page');
  ok(/Double-click the canvas and press space for fullscreen/.test(html) && /O = B/.test(html) && /P = A/.test(html), 'help text lists fullscreen + key bindings');
  const boxOf = () => [parseInt(el('screen').style.width), parseInt(el('screen').style.height)];
  const menuBox = boxOf();
  const D = (x, y) => ({ clientX: x * menuBox[0] / 640, clientY: y * menuBox[1] / 480 });   // desktop px -> box px
  ok(menuBox[0] > 800 && Math.abs(menuBox[0] / menuBox[1] - 4 / 3) < 0.01, `menu view fills the window at the desktop's 4:3 (${menuBox.join('x')} css px)`);
  // click the CPU menu
  fire('screen', 'pointermove', { ...D(40, 4), buttons: 0, pointerId: 1 });
  fire('screen', 'pointerdown', { ...D(40, 4), buttons: 1, pointerId: 1 });
  await step(3);
  shot('glue_menu.png');
  const bluish = count((r, g, b) => b > 60 && r < 40 && g < 40);   // desktop (0,0,76) + menu (0,0,124)
  ok(bluish > 200000, 'desktop is painted (' + bluish + ' blue pixels)');
  ok(count((r, g, b) => r > 180 && g > 180 && b > 180) > 300, 'menu text is drawn (light-grey font, not the old all-peach)');
  ok(count((r, g, b) => g > 150 && r < 80 && b < 80) > 20, 'shortcut letters use the green font (font table applied)');
  fire('screen', 'pointerup', { ...D(40, 4), buttons: 0, pointerId: 1 });
  // audio starts on the first gesture (the menu click above), not before
  ok(audioLog.ctxs === 1 && audioLog.modules.length === 1 && audioLog.nodes.length === 1 && audioLog.nodes[0].connected, 'audio context + worklet node created on the first gesture');
  ok(audioLog.posted.some(m => m && m.rate === 44100), 'page tells the worklet the source rate (44100)');
  ok(/registerProcessor\('nes-audio'/.test(audioLog.blobs[audioLog.modules[0]]), 'worklet module source is loaded');
  // double-click on the canvas -> fullscreen request; not repeated once already fullscreen
  fire('screen', 'dblclick', {}); await step(1);
  ok(el('stage').fs === 1, 'double-click requests fullscreen on the stage');
  sandbox.document.fullscreenElement = el('stage'); fire('screen', 'dblclick', {}); await step(1);
  ok(el('stage').fs === 1, 'double-click while fullscreen does nothing (Esc leaves)');
  sandbox.document.fullscreenElement = null;
  // File -> Load ROM... must open the browser's file chooser (the page's hidden <input>), not NESticle's dialog
  const clk = (x, y) => { for (const [t, b] of [['pointermove', 0], ['pointerdown', 1], ['pointerup', 0]]) fire('screen', t, { ...D(x, y), buttons: b, pointerId: 1 }); };
  clk(300, 300); await step(2);   // dismiss the CPU menu opened earlier
  clk(12, 4); await step(2); clk(30, 14); await step(3);
  ok(el('file').clicks === 1, 'File > Load ROM... opens the browser file picker (input clicked ' + el('file').clicks + 'x)');
  shot('glue_after_load_click.png');
  ok(count((r, g, b) => r < 40 && g < 40 && b < 40) < 3000, 'NESticle\'s own Load dialog did not open (no black list area)');
  // choose a ROM through the file input, as a user would
  const bytes = fs.readFileSync(rom);
  await new Promise(res => {
    const target = { files: [{ name: 'My Homebrew (v1).nes', size: bytes.length, arrayBuffer: async () => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length) }], value: 'x' };
    (handlers['file:change'] || []).forEach(f => f({ target }));
    setTimeout(res, 100);
  });
  await step(60);
  ok(el('status').textContent === '', 'no "Running ..." message after loading a ROM');
  shot('glue_rom.png');
  const scr = el('screen'), cssW = parseInt(scr.style.width), cssH = parseInt(scr.style.height);
  ok(scr.width === 256 && scr.height === 224, `canvas is cropped to the game picture (${scr.width}x${scr.height})`);
  ok(cssW === menuBox[0] && cssH === menuBox[1], `the canvas box does not change size between menu view and game view (${menuBox.join('x')} -> ${cssW}x${cssH})`);
  // the 256x224 picture is centred in the 4:3 box; the mouse maps through that (side bars clamp to the edge)
  const at = (x, y) => { fire('screen', 'pointermove', { clientX: x, clientY: y, buttons: 0, pointerId: 1 }); return vm.runInContext('[mx, my]', sandbox); };
  const view = [0, 1, 2, 3].map(i => vm.runInContext(`M._nes_view(${i})`, sandbox));
  ok(JSON.stringify(view) === '[192,128,256,224]', 'game view is the centred 256x224 picture: ' + view);
  ok(JSON.stringify(at(cssW / 2, cssH / 2)) === '[320,240]', 'mouse at the centre of the box -> centre of the game picture (320,240)');
  ok(JSON.stringify(at(0, 0)) === '[192,128]' && JSON.stringify(at(cssW - 1, cssH - 1)) === '[447,351]', 'mouse in the side bars/corners clamps to the picture edge');
  const sc = Math.min(cssW / 256, cssH / 224), ox = (cssW - 256 * sc) / 2;
  ok(JSON.stringify(at(ox + 10 * sc + 1, 5 * sc + 1)) === '[202,133]', 'mouse inside the picture maps pixel-exactly (10,5) -> (202,133)');
  ok(!count((r, g, b) => r + g + b === 0) || count((r, g, b) => r + g + b === 0) < 2000, 'no black letterbox inside the canvas');
  // pointer position maps through the crop: the centre of the canvas is the centre of the game picture
  fire('screen', 'pointermove', { clientX: 0, clientY: 0, buttons: 0, pointerId: 1 });
  ok(count((r, g, b) => r + g + b > 60) > 40000, 'game frame has colour (not black)');
  ok(count((r, g, b) => r > 150 && g < 100 && b < 100) > 5000, 'test ROM red stripes visible');
  // ESC toggles the GUI back on, via the keyboard path
  fire('screen', 'keydown', { code: 'Escape' });
  fire('screen', 'keyup', { code: 'Escape' });
  await step(3);
  shot('glue_esc.png');
  ok(count((r, g, b) => r === 0 && g === 0 && b === 124) > 1000, 'Escape brings the menu bar back');
  ok(boxOf().join() === menuBox.join(), 'and the canvas box is still the same size');
  ok(el('screen').width === 640 && el('screen').height === 480, 'full desktop view restored with the GUI');
  // a ROM that makes sound: sample blocks must reach the worklet
  if (audioRom) {
    const ab = fs.readFileSync(audioRom);
    const before = audioLog.posted.filter(m => ArrayBuffer.isView(m)).reduce((n, m) => n + m.length, 0);
    (handlers['file:change'] || []).forEach(f => f({ target: { files: [{ name: 'tone.nes', size: ab.length, arrayBuffer: async () => ab.buffer.slice(ab.byteOffset, ab.byteOffset + ab.length) }], value: '' } }));
    await new Promise(r => setTimeout(r, 100));
    await step(40);
    const blocks = audioLog.posted.filter(m => ArrayBuffer.isView(m)), n = blocks.reduce((t, m) => t + m.length, 0) - before;
    const peak = Math.max(...blocks.slice(-20).map(b => b.reduce((m, v) => Math.max(m, Math.abs(v)), 0)));
    ok(n > 25 * 735, `sound ROM: ${n} samples posted to the worklet over ~40 frames`);
    ok(peak > 0.03 && peak <= 1, `samples are scaled to +-1 (peak ${peak.toFixed(3)})`);
  }
  // a trap inside the emulator must not freeze the page: it reports, restarts, and keeps working
  {
    const warn = console.error; console.error = () => {};              // (the page logs the crash)
    vm.runInContext("M._nes_frame = () => { throw new Error('simulated memory fault'); }", sandbox);
    const oldM = vm.runInContext('M', sandbox);
    for (let i = 0; i < 10 && !raf; i++) await new Promise(r => setTimeout(r, 10));
    const f0 = raf; if (f0) { raf = null; f0(); }                         // the next frame traps
    for (let i = 0; i < 200 && !/restarted/.test(el('status').textContent); i++) await new Promise(r => setTimeout(r, 25));
    console.error = warn;
    ok(/crashed and was restarted/.test(el('status').textContent), 'a trap is reported and the emulator restarted: ' + el('status').textContent);
    ok(vm.runInContext('M', sandbox) !== oldM, 'a fresh emulator instance replaced the dead one');
    ok(!!raf, 'the frame loop was re-armed');
    await step(3);
    ok(count((r, g, b) => r === 0 && g === 0 && b === 124) > 1000, 'the restarted emulator draws the menu bar again');
    fire('screen', 'pointermove', { ...D(40, 4), buttons: 0, pointerId: 1 });
    fire('screen', 'pointerdown', { ...D(40, 4), buttons: 1, pointerId: 1 });
    await step(3);
    ok(count((r, g, b) => r === 0 && g === 0 && b === 124) > 5000, 'and responds to the mouse (menu opens)');
    fire('screen', 'pointerup', { ...D(40, 4), buttons: 0, pointerId: 1 });
    sandbox.__t = 0;
  }
  // the worklet itself, run for real in a sandbox with a 48 kHz sound card
  {
    const processors = {};
    const ctx2 = vm.createContext({ sampleRate: 48000, Float32Array, Math,
      AudioWorkletProcessor: class { constructor() { this.port = {}; } },
      registerProcessor: (n, c) => { processors[n] = c; } });
    vm.runInContext(audioLog.blobs[audioLog.modules[0]], ctx2);
    const P = new processors['nes-audio'](), out = () => [new Float32Array(128)];
    P.port.onmessage({ data: { rate: 44100 } });
    const ramp = new Float32Array(2000).map((_, i) => i / 2000);
    P.port.onmessage({ data: ramp });
    const o = out(); P.process([], [o]);
    const step = 44100 / 48000;
    let maxerr = 0; for (let i = 0; i < 128; i++) maxerr = Math.max(maxerr, Math.abs(o[0][i] - (i * step) / 2000));
    ok(maxerr < 1e-4, `worklet resamples 44.1k -> 48k by linear interpolation (max error ${maxerr.toExponential(1)})`);
    let more = 0; for (let k = 0; k < 20; k++) { const oo = out(); P.process([], [oo]); more = oo[0][127]; }
    ok(more === 0, 'underrun plays silence (no garbage when the queue runs dry)');
    const P2 = new processors['nes-audio'](); P2.port.onmessage({ data: { rate: 44100 } });
    P2.port.onmessage({ data: new Float32Array(30000).fill(0.5) });
    ok(P2.w - P2.r <= 44100 * 0.25 / 2 + 1, `queue is capped to bound latency (${P2.w - P2.r} samples kept of 30000)`);
    const stereo = [new Float32Array(128), new Float32Array(128)]; P2.process([], [stereo]);
    ok(stereo[0][5] === 0.5 && stereo[1][5] === 0.5, 'mono output is copied to every output channel');
  }
  process.exit(process.exitCode || 0);
})().catch(e => { console.error(e); process.exit(1); });
