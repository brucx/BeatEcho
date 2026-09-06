#!/usr/bin/env python3
"""Playwright smoke/E2E, with no external server or resource fetch.

Install Playwright separately and use a locally available Chromium. Does not
claim file:// navigation, real audio hardware, mobile browsers, or physical PCB testing.
BE_CHROME_PATH can name an installed Chromium; otherwise use Playwright's binary.
"""
import json
import os
from pathlib import Path
import shutil
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'build-qa'
OUTPUT.mkdir(exist_ok=True)
with sync_playwright() as p:
    executable = os.environ.get('BE_CHROME_PATH') or shutil.which('chromium')
    options = {'headless': True, 'args': ['--autoplay-policy=no-user-gesture-required']}
    if executable:
        options['executable_path'] = executable
    browser = p.chromium.launch(**options)
    page = browser.new_page(viewport={'width':1440, 'height':1080})
    errors = []
    page.on('pageerror', lambda error: errors.append(str(error)))
    page.set_content((ROOT/'web/index.html').read_text(), wait_until='load')
    page.wait_for_function('window.beatEcho && !document.querySelector("#start").disabled')
    assert page.locator('#engine-status').inner_text() == 'C++ 引擎已就绪'
    assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
    page.screenshot(path=str(OUTPUT/'desktop.png'), full_page=True)
    # Move the actual UI to 160 BPM, then play a complete round through keyboard
    # events against the audio presentation clock. This is software input, not HIL.
    for _ in range(14):
        page.locator('#tempo-up').click()
    page.locator('#start').click()
    page.wait_for_function('window.beatEcho.engine.be_value(0) === 1')
    page.locator('#start').blur()
    result = page.evaluate('''async () => {
      const {engine:g,now}=window.beatEcho;
      const codes=['KeyA','KeyS','KeyD','KeyF','KeyJ','KeyK','KeyL','Semicolon'];
      const keys=['a','s','d','f','j','k','l',';'];
      const notes=Array.from({length:g.be_value(10)},(_,i)=>({
        at:(g.be_value(14)+g.be_note(i,0))/1000,key:g.be_note(i,1)}));
      await new Promise((resolve,reject)=>{
        let next=0;const began=performance.now();
        const timer=setInterval(()=>{
          if(performance.now()-began>15000){clearInterval(timer);reject(new Error('Round timed out'));return;}
          if(g.be_value(0)===0){clearInterval(timer);reject(new Error('Unexpected abort: '+document.querySelector('#warning').textContent));return;}
          while(next<notes.length && now()>=notes[next].at){
            const key=notes[next++].key;
            document.dispatchEvent(new KeyboardEvent('keydown',{code:codes[key],key:keys[key],bubbles:true}));
            document.dispatchEvent(new KeyboardEvent('keydown',{code:codes[key],key:keys[key],repeat:true,bubbles:true}));
            document.dispatchEvent(new KeyboardEvent('keyup',{code:codes[key],key:keys[key],bubbles:true}));
          }
          if(g.be_value(0)>=5){clearInterval(timer);resolve();}
        },2);
      });
      return {accuracy:g.be_value(7),passed:g.be_value(11),extra:g.be_value(24),notes:notes.length};
    }''')
    assert result['accuracy'] == 1000, result
    assert result['passed'] == 1 and result['extra'] == 0, result
    page.locator('#start').click()
    page.wait_for_function('window.beatEcho.engine.be_value(3) === 2')
    page.evaluate('window.dispatchEvent(new Event("blur"))')
    assert page.evaluate('window.beatEcho.engine.be_value(0)') == 0
    page.set_viewport_size({'width':390,'height':844})
    page.wait_for_timeout(100)
    assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
    page.screenshot(path=str(OUTPUT/'mobile.png'),full_page=True)
    assert not errors, errors
    report={'status':'PASS','browser':browser.version,'desktop':[1440,1080],
            'mobile_viewport':[390,844],'round':result,'console_errors':errors,
            'scope':'Headless Chromium + set_content + synthetic keyboard; not file URL / physical audio / PCB HIL'}
    (OUTPUT/'browser.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(report,ensure_ascii=False))
    browser.close()
