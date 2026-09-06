/* Browser adapter only. Pattern generation, judgement and PCM synthesis live in C++. */
'use strict';
(async () => {
  const $ = id => document.getElementById(id);
  const pads = [...document.querySelectorAll('.pad')];
  const lamps = [...document.querySelectorAll('.beat-lights i')];
  const held = Array.from({length: 8}, () => new Set());
  const flashUntil = Array(8).fill(0);
  const codes = ['KeyA','KeyS','KeyD','KeyF','KeyJ','KeyK','KeyL','Semicolon'];
  let api, context, master, audioReady, buffers = [];
  let cueEpoch = -1, nextCue = 0, oldPhase = -1, feedbackUntil = 0, busy = false;
  let best = 0;
  const sources = new Set();
  const value = field => api.be_value(field);
  const active = () => value(0) >= 1 && value(0) <= 4;
  const editable = () => value(0) === 0 || value(0) === 6;
  const warn = message => { $('warning').textContent = message; $('warning').hidden = !message; };
  const text = (id, string) => { if ($(id).textContent !== String(string)) $(id).textContent = String(string); };
  try { best = Math.max(0, Math.min(1e9, Number(localStorage.getItem('beat-echo.best.v1')) || 0)); } catch (_) {}

  function stopSounds() {
    for (const source of sources) { try { source.stop(); } catch (_) {} }
    sources.clear();
  }
  function syncEpoch() {
    if (cueEpoch === value(29)) return;
    cueEpoch = value(29); nextCue = 0; stopSounds();
  }
  function audibleTimeMs(stamp = performance.now()) {
    if (!context) return 0;
    if (!Number.isFinite(stamp) || Math.abs(stamp - performance.now()) > 10000) stamp = performance.now();
    if (context.getOutputTimestamp) {
      const t = context.getOutputTimestamp();
      if (t.performanceTime > 0 && t.contextTime > 0) {
        return Math.max(0, Math.min(context.currentTime, t.contextTime + (stamp - t.performanceTime) / 1000)) * 1000;
      }
    }
    // Fallback is less precise. Manual input compensation is still available.
    const latency = context.outputLatency || context.baseLatency || 0;
    return Math.max(0, context.currentTime - latency + (stamp - performance.now()) / 1000) * 1000;
  }
  async function ensureAudio() {
    if (!context) {
      const AudioContextClass = window.AudioContext || window.webkitAudioContext;
      if (!AudioContextClass) throw new Error('此浏览器没有 Web Audio。请使用支持音频的现代浏览器。');
      context = new AudioContextClass({latencyHint: 'interactive'});
      master = context.createGain();
      master.gain.value = Number($('volume').value) / 100;
      master.connect(context.destination);
      context.addEventListener('statechange', () => {
        if (context.state !== 'running' && api && active()) abort('音频已中断，本次挑战已停止，未继续计分。');
      });
      audioReady = (async () => {
        await context.resume();
        const rate = api.be_sample_rate(), frames = api.be_sound_frames();
        for (let voice = 0; voice < 11; ++voice) {
          const pointer = api.be_sound(voice);
          const pcm = new Int16Array(api.memory.buffer, pointer, frames);
          const buffer = context.createBuffer(1, frames, rate);
          const channel = buffer.getChannelData(0);
          for (let i = 0; i < frames; ++i) channel[i] = pcm[i] / 32768;
          buffers.push(buffer);
        }
      })();
    }
    await audioReady;
    if (context.state !== 'running') await context.resume();
  }
  function sound(voice, at = context.currentTime, velocity = 100) {
    if (!buffers[voice] || context.state !== 'running') return;
    const source = context.createBufferSource(), gain = context.createGain();
    source.buffer = buffers[voice]; gain.gain.value = velocity / 100;
    source.connect(gain); gain.connect(master); sources.add(source);
    source.onended = () => { sources.delete(source); source.disconnect(); gain.disconnect(); };
    source.start(Math.max(at, context.currentTime + 0.001));
  }
  function abort(message = '') {
    if (!api) return;
    api.be_abort(); syncEpoch();
    for (const set of held) set.clear();
    feedbackUntil = 0; warn(message);
  }
  async function start() {
    if (!api || busy) return;
    if (active()) { abort(); return; }
    busy = true;
    try {
      await ensureAudio();
      if (document.hidden) return;
      warn(''); feedbackUntil = 0;
      // Start on the audio timeline with sufficient scheduling lead, including slow output devices.
      api.be_select(Math.max(audibleTimeMs(), context.currentTime * 1000));
      syncEpoch();
    } catch (error) { warn(error.message); }
    finally { busy = false; }
  }
  async function strike(key, stamp) {
    if (!api) return;
    try {
      await ensureAudio();
      const phase = value(0);
      const grade = api.be_press(key, audibleTimeMs(stamp));
      if (phase === 0 || phase >= 5 || grade !== 6) sound(key);
      flashUntil[key] = performance.now() + 110;
      if (grade !== 6) {
        const names = ['', 'PERFECT', 'GOOD', 'LATE / EARLY', 'MISS', 'EXTRA'];
        text('judge', names[grade] || 'ECHO');
        $('feedback-light').className = 'feedback-light ' + (grade <= 2 ? 'good' : 'bad');
        feedbackUntil = performance.now() + 350;
      }
    } catch (error) { warn(error.message); }
  }
  function down(key, token, stamp) {
    if (held[key].has(token)) return;
    const alreadyDown = held[key].size > 0;
    held[key].add(token);
    if (!alreadyDown) void strike(key, stamp);
  }
  function up(key, token) { held[key].delete(token); }
  pads.forEach((pad, key) => {
    pad.addEventListener('pointerdown', event => {
      event.preventDefault(); pad.setPointerCapture(event.pointerId);
      down(key, 'p' + event.pointerId, event.timeStamp);
    });
    for (const name of ['pointerup','pointercancel','lostpointercapture']) {
      pad.addEventListener(name, event => up(key, 'p' + event.pointerId));
    }
  });
  document.addEventListener('keydown', event => {
    if (['INPUT','SELECT','TEXTAREA'].includes(document.activeElement.tagName)) return;
    if (event.code === 'Escape') { event.preventDefault(); abort(); return; }
    if (event.code === 'Space') { event.preventDefault(); if (!event.repeat) void start(); return; }
    const key = codes.indexOf(event.code);
    if (key >= 0) { event.preventDefault(); if (!event.repeat) down(key, event.code, event.timeStamp); }
  });
  document.addEventListener('keyup', event => {
    const key = codes.indexOf(event.code); if (key >= 0) up(key, event.code);
  });
  window.addEventListener('blur', () => { if (api && active()) abort('窗口失去焦点，本次挑战已停止。回到页面后重新开始。'); else for (const set of held) set.clear(); });
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) { if (api && active()) abort('页面切到后台，本次挑战已停止，避免计时误判。'); else stopSounds(); }
  });
  $('start').addEventListener('click', () => void start());
  $('reset').addEventListener('click', () => abort());
  function settingsChanged() {
    if (!api || !editable()) return;
    const bpm = value(1);
    const difficulty = Math.max(0, Math.min(2, Number($('difficulty').value) || 0));
    const offset = Math.max(-200, Math.min(200, Number($('offset').value) || 0));
    const seed = Math.max(1, Math.min(4294967295, Math.trunc(Number($('seed').value) || 42)));
    $('seed').value = String(seed); $('offset').value = String(offset);
    api.be_init(bpm, difficulty, offset, seed); cueEpoch = -1; syncEpoch();
    text('session-label', `CHALLENGE #${seed}`);
  }
  for (const id of ['difficulty','offset','seed']) $(id).addEventListener('change', settingsChanged);
  function tempo(step) { if (api && editable()) api.be_configure(value(1) + step, value(2), value(26)); }
  $('tempo-down').addEventListener('click', () => tempo(-5));
  $('tempo-up').addEventListener('click', () => tempo(5));
  $('volume').addEventListener('input', () => { if (master) master.gain.setTargetAtTime(Number($('volume').value) / 100, context.currentTime, 0.015); });

  function scheduleAudio() {
    if (!api || !context || !buffers.length || context.state !== 'running') return;
    syncEpoch();
    const horizon = context.currentTime + 0.15;
    while (nextCue < value(30)) {
      const when = api.be_cue(nextCue, 0) / 1e6;
      if (when > horizon) break;
      if (context.currentTime - when > 0.04) {
        abort('音频调度出现卡顿，本次挑战已停止，成绩未继续累计。请关闭高负载页面后重试。'); return;
      }
      sound(api.be_cue(nextCue, 1), when, api.be_cue(nextCue, 2)); ++nextCue;
    }
  }
  function draw() {
    if (!api) { requestAnimationFrame(draw); return; }
    const now = audibleTimeMs(); api.be_tick(now);
    const phase = value(0), editableNow = editable();
    if (phase !== oldPhase) {
      oldPhase = phase;
      if (phase === 5 || phase === 6) {
        if (context && context.state === 'running') sound(value(11) ? 9 : 10);
        best = Math.max(best, value(25));
        try { localStorage.setItem('beat-echo.best.v1', String(best)); } catch (_) {}
      }
    }
    const beat = api.be_beat(now);
    let label, title, hint;
    if (phase === 0) { label='READY WHEN YOU ARE'; title='轮到节奏说话。'; hint='点击下方鼓垫试听，再按「开始挑战」。'; }
    else if (phase === 1) { label='COUNT IN'; title=beat<0?'准备好了吗？':`预备 · ${4-beat}`; hint='蓝灯走完四拍，就开始示范。先听，不用按键。'; }
    else if (phase === 2) { label='LISTEN & REMEMBER'; title='记住这段节奏。'; hint='紫灯是示范。记住用到的鼓垫，也记住中间的休止。'; }
    else if (phase === 3) { label='GET READY'; title=`准备 · ${beat<0?'—':4-beat}`; hint='保持心里的节拍。黄灯四拍结束后，用相同鼓垫复现。'; }
    else if (phase === 4) { label='YOUR TURN'; title='现在，打回来。'; hint='跟着节拍击打。按住不会重复触发，每个鼓点只需按一下。'; }
    else if (phase === 5) { label=value(11)?'PHRASE COMPLETE':'TRY THE SAME PHRASE'; title=value(11)?'这一段，接住了。':'再听一遍，你会的。'; hint=value(11)?'按「下一关」继续，节奏会逐步增加变化。':'本轮没有过关，扣一条生命。重试会播放完全相同的节奏。'; }
    else { label=value(12)?'ALL 12 ROUNDS CLEARED':'SESSION COMPLETE'; title=value(12)?'十二关，全部拿下。':'今天的节奏，先到这里。'; hint='可以调整难度或 BPM，再来一次。最高分保存在当前浏览器。'; }
    text('phase-label',label); text('phase-title',title); text('phase-hint',hint);
    text('round-label',`ROUND ${String(value(3)).padStart(2,'0')} / 12`);
    text('lives',Array.from({length:3},(_,i)=>i<value(4)?'●':'○').join(' '));
    $('lives').setAttribute('aria-label',`剩余 ${value(4)} 条生命`);
    text('score',String(value(5)).padStart(6,'0')); text('combo',value(8)||'—');
    text('accuracy',phase===0?'—':(value(7)/10).toFixed(1)+'%');
    text('best',String(best).padStart(6,'0')); text('bpm-value',value(1));
    $('difficulty').value=String(value(2));
    for (const id of ['tempo-down','tempo-up','difficulty','offset','seed']) $(id).disabled=!editableNow;
    text('start',phase===0?'开始挑战':phase===5?(value(11)?'下一关 →':'重试本关') : phase===6?'重新开始':'返回菜单');
    $('start').disabled=busy;
    document.querySelector('.beat-lights').className='beat-lights '+(phase===2?'listen':phase===3?'ready':phase===4?'repeat':'');
    lamps.forEach((lamp,i)=>lamp.classList.toggle('on',i===beat));
    const hintMask=$('hints').checked?api.be_demo_mask(now):0;
    pads.forEach((pad,i)=>{
      pad.classList.toggle('active',held[i].size>0||performance.now()<flashUntil[i]);
      pad.classList.toggle('hint',!!(hintMask&(1<<i)));
    });
    if(performance.now()>feedbackUntil) {
      text('judge',phase===5?(value(11)?'PASSED':'RETRY'):'ECHO');
      $('feedback-light').className='feedback-light'+(phase===5?' '+(value(11)?'good':'bad'):'');
    }
    let fraction=0;
    if(phase>=1&&phase<=4) {
      const startField={1:17,2:15,3:16,4:14}[phase];
      const duration=(phase===1||phase===3)?value(13)*4:value(18);
      fraction=Math.max(0,Math.min(1,(now*1000-value(startField))/duration));
    } else if(phase>=5) fraction=1;
    $('progress-fill').style.width=(fraction*100)+'%';
    $('result-detail').hidden=phase<5;
    if(phase>=5) {
      const mean=value(28)?value(27)/value(28)/1000:0;
      text('result-detail',`Perfect ${value(20)} · Good ${value(21)} · Bad ${value(22)} · Miss ${value(23)} · 多按 ${value(24)}\n本轮 ${value(6)} 分 · 最大连击 ${value(9)} · 平均时差 ${mean>=0?'+':''}${mean.toFixed(1)} ms`);
    }
    requestAnimationFrame(draw);
  }
  try {
    const bytes=Uint8Array.from(atob(BE_WASM_BASE64),c=>c.charCodeAt(0));
    const {instance}=await WebAssembly.instantiate(bytes,{}); api=instance.exports;
    api.be_init(90,0,0,42);
    // Explicit local diagnostics; this offline simulator is not an anti-cheat client.
    window.beatEcho=Object.freeze({engine:api,now:audibleTimeMs,version:'0.1.0'});
    text('engine-status','C++ 引擎已就绪'); $('start').disabled=false;
    setInterval(scheduleAudio,20); requestAnimationFrame(draw);
  } catch(error) {
    text('engine-status','引擎载入失败'); text('start','无法开始');
    warn(`无法载入 WebAssembly：${error.message}`);
  }
})();
