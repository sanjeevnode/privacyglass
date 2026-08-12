// Self-check for the privacy engine. Runs headless in the WebView2 page context
// against a mock WhatsApp DOM -- no login, no network, window hidden.
//
// Assertions check CLASS STATE, not getComputedStyle(): a hidden window does not
// render, so computed styles are stale and rAF never fires. Classes are what the
// engine actually controls; the mapping from class to blur lives in privacy.css
// and is verified once, below, by parsing the stylesheet.

(() => {
  const fail = (stage, err) => {
    try {
      window.chrome.webview.postMessage(JSON.stringify({
        type: 'selfcheck', passed: 0, total: 1,
        lines: `[FAIL] ${stage}: ${err && err.message ? err.message : err}`,
      }));
    } catch (_) {}
  };
  window.addEventListener('error', (e) => fail('uncaught', e.error || e.message));

  let waited = 0;
  const startWhenReady = () => {
    if (window.__wapPrivacy && window.__wapApplyState) return run();
    if ((waited += 16) > 5000)
      return fail('bootstrap', 'engine never appeared: ' + (window.__wapBootError || 'no error'));
    setTimeout(startWhenReady, 16);
  };

  function run() {
    try {
      const results = [];
      const check = (name, cond) => results.push({ name, ok: !!cond });

      document.body.innerHTML = `
        <div id="pane-side">
          <div role="listitem">
            <span title="Alice">Alice</span>
            <div role="gridcell"><span dir="ltr">see you at 6</span></div>
            <img src="blob:fake-avatar">
          </div>
        </div>
        <div id="main">
          <header><span title="Alice">Alice</span></header>
          <div class="message-in"><span class="selectable-text">secret text</span></div>
        </div>`;

      window.__wapPrivacy.retagAll();

      const name    = document.querySelector('#pane-side span[title]');
      const preview = document.querySelector('#pane-side span[dir="ltr"]');
      const pic     = document.querySelector('#pane-side img');
      const message = document.querySelector('#main .selectable-text');

      // --- selectors.js still matches the DOM it targets --------------------
      check('name tagged',    name.classList.contains('wap-t-name'));
      check('preview tagged', preview.classList.contains('wap-t-preview'));
      check('picture tagged', pic.classList.contains('wap-t-picture'));
      check('message tagged', message.classList.contains('wap-t-message'));

      // --- the CSS contract: each hide-class must blur its target ----------
      // Verified by reading the stylesheet, so a rule deleted from privacy.css
      // fails here even though nothing is rendered.
      const rules = [...document.getElementById('wap-style').sheet.cssRules]
        .filter(r => r.selectorText && r.style && r.style.filter);
      const blursVia = (hideCls, targetCls) => rules.some(r =>
        r.selectorText.includes('.' + hideCls) &&
        r.selectorText.includes('.' + targetCls) &&
        r.style.filter.includes('blur'));

      check('css blurs names',    blursVia('wap-hide-names',    'wap-t-name'));
      check('css blurs messages', blursVia('wap-hide-messages', 'wap-t-message'));
      check('css blurs pictures', blursVia('wap-hide-pictures', 'wap-t-picture'));
      check('css blurs previews', blursVia('wap-hide-previews', 'wap-t-preview'));

      const on = (cls) => document.body.classList.contains(cls);

      // --- master ON applies every category --------------------------------
      window.__wapApplyState({ on: true, names: true, messages: true, pictures: true, previews: true });
      check('ON hides names',    on('wap-hide-names'));
      check('ON hides messages', on('wap-hide-messages'));
      check('ON hides pictures', on('wap-hide-pictures'));
      check('ON hides previews', on('wap-hide-previews'));

      // --- master OFF gates every category, whatever the per-category flags -
      window.__wapApplyState({ on: false, names: true, messages: true, pictures: true, previews: true });
      check('OFF reveals names',    !on('wap-hide-names'));
      check('OFF reveals messages', !on('wap-hide-messages'));
      check('OFF reveals pictures', !on('wap-hide-pictures'));

      // --- categories are independent (Phase 5 requirement) ----------------
      window.__wapApplyState({ on: true, names: true, messages: false, pictures: false, previews: false });
      check('names-only hides names',     on('wap-hide-names'));
      check('names-only frees messages', !on('wap-hide-messages'));
      check('names-only frees pictures', !on('wap-hide-pictures'));

      // --- MutationObserver: a message arriving while ON must self-tag ------
      window.__wapApplyState({ on: true, names: true, messages: true, pictures: true, previews: true });
      const incoming = document.createElement('div');
      incoming.className = 'message-in';
      incoming.innerHTML = '<span class="selectable-text">brand new message</span>';
      document.querySelector('#main').appendChild(incoming);

      // scheduleRetag() races rAF against a 32ms timer, so a timer wait is
      // enough even with nothing rendering.
      setTimeout(() => {
        try {
          const fresh = incoming.querySelector('.selectable-text');
          check('new message auto-tagged', fresh.classList.contains('wap-t-message'));

          // --- boot fail-safe -------------------------------------------------
          check('boot rule exists', [...document.getElementById('wap-style').sheet.cssRules]
            .some(r => r.selectorText === 'html.wap-boot body' &&
                       r.style.filter.includes('blur')));

          const failed = results.filter(r => !r.ok);
          window.chrome.webview.postMessage(JSON.stringify({
            type: 'selfcheck',
            passed: results.length - failed.length,
            total: results.length,
            lines: results.map(r => `${r.ok ? '[PASS]' : '[FAIL]'} ${r.name}`).join('\n'),
          }));
        } catch (e) { fail('async', e); }
      }, 200);
    } catch (e) { fail('sync', e); }
  }

  startWhenReady();
})();
