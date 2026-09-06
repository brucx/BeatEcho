// No npm dependencies; execute with Node.js >= 20.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const bytes = fs.readFileSync(path.join(root, 'web/beat_echo.wasm'));
const module = await WebAssembly.compile(bytes);
assert.deepEqual(WebAssembly.Module.imports(module), [], 'offline WASM must not import a runtime');
const g = (await WebAssembly.instantiate(module, {})).exports;
let rounds = 0, hits = 0;
for (const difficulty of [0, 1, 2]) {
  for (const bpm of [60, 90, 137, 160]) {
    for (let seed = 1; seed <= 10; ++seed) {
      g.be_init(bpm, difficulty, 0, seed);
      let now = 1000;
      for (let round = 1; round <= 12; ++round) {
        g.be_select(now);
        assert.equal(g.be_value(3), round);
        const start = g.be_value(14);
        for (let i = 0; i < g.be_value(10); ++i) {
          assert.equal(g.be_press(g.be_note(i, 1), (start + g.be_note(i, 0)) / 1000), 1);
          ++hits;
        }
        g.be_tick(g.be_value(19) / 1000 + 1);
        assert.equal(g.be_value(7), 1000);
        assert.equal(g.be_value(11), 1);
        assert.equal(g.be_value(4), 3);
        now = g.be_value(19) / 1000 + 500;
        ++rounds;
      }
      assert.equal(g.be_value(0), 6);
      assert.equal(g.be_value(12), 1);
    }
  }
}
g.be_init(90, 0, 0, 42);
g.be_select(1000);
const initial = Array.from({length:g.be_value(10)}, (_,i)=>[g.be_note(i,0),g.be_note(i,1)]);
g.be_tick(g.be_value(19)/1000+1);
assert.equal(g.be_value(4), 2);
g.be_select(20000);
assert.deepEqual(Array.from({length:g.be_value(10)}, (_,i)=>[g.be_note(i,0),g.be_note(i,1)]), initial);
g.be_abort();
assert.equal(g.be_value(0), 0);
g.be_select(NaN);
g.be_tick(Infinity); // exported conversions must not trap on non-finite input
assert.equal(g.be_press(99, 0), 6);
for(let voice=0;voice<11;++voice) {
  const pointer=g.be_sound(voice);
  const pcm=new Int16Array(g.memory.buffer,pointer,g.be_sound_frames());
  assert.ok(pcm.some(x=>x!==0), `voice ${voice} must not be silent`);
  assert.ok(pcm.slice(-100).every(x=>x===0), `voice ${voice} must decay`);
}
const html=fs.readFileSync(path.join(root,'web/index.html'),'utf8');
assert.ok(html.includes(bytes.toString('base64')), 'published HTML must contain current WASM');
console.log(`PASS: WASM ABI, ${rounds} complete rounds, ${hits} exact hits, retry, abort, 11 sounds, embedded artifact`);
