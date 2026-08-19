// Privacy engine. Injected via AddScriptToExecuteOnDocumentCreated, so this runs
// BEFORE any WhatsApp code on every navigation -- including the SPA's internal
// route changes, which never re-run page load.
//
// Native bridge protocol (both directions, newline-free JSON):
//   C++  -> JS : {"type":"state", "on":bool, "names":bool, "messages":bool,
//                 "pictures":bool, "previews":bool, "hoverReveal":bool}
//   C++  -> JS : {"type":"ping","id":"..."}
//   JS   -> C++: {"type":"pong","id":"...","url":"..."}
//   JS   -> C++: {"type":"ready"}

(() => {
  'use strict';

  // Guard: WhatsApp creates iframes; only run in the top document.
  if (window.top !== window) return;

  const CSS = window.__wapPrivacyCss || '';
  const SEL = () => window.__wapSelectors || {};

  const CATEGORIES = ['names', 'messages', 'pictures', 'previews'];
  const CLASS_FOR = {
    names: 'wap-t-name',
    messages: 'wap-t-message',
    pictures: 'wap-t-picture',
    previews: 'wap-t-preview',
  };

  let state = {
    on: true, names: true, messages: true,
    pictures: true, previews: true, hoverReveal: false,
  };

  // --- boot fail-safe -------------------------------------------------------
  // Blur everything immediately. Nothing has rendered yet, but WhatsApp paints
  // fast; this class is what guarantees no flash of plaintext.
  //
  // This script runs at document-created time, when the DOM can be completely
  // empty -- documentElement itself may be null. Everything below must tolerate
  // that and retry, or the engine dies before it defines anything and the page
  // renders in the clear.
  // Latch: once boot() has handed over to the real per-category rules, the
  // fail-safe must never be re-applied, or the retry loop below would race
  // boot() and leave the page permanently blurred.
  let booted = false;

  function markBoot() {
    if (!document.documentElement) return false;
    if (!booted) document.documentElement.classList.add('wap-boot');
    return true;
  }

  function injectStyle() {
    const root = document.head || document.documentElement;
    if (!root || document.getElementById('wap-style')) return !!root;
    const style = document.createElement('style');
    style.id = 'wap-style';
    style.textContent = CSS;
    root.appendChild(style);
    return true;
  }

  // Keep retrying until the document exists; both are idempotent.
  if (!markBoot() || !injectStyle()) {
    const t = setInterval(() => {
      if (markBoot() && injectStyle()) clearInterval(t);
    }, 0);
  }

  // --- tagging --------------------------------------------------------------
  // Cost of the tagging passes, readable via __wapStats(), so a slow session can
  // be attributed to this engine or ruled out. Must be declared BEFORE
  // tagWithin: a `const` referenced above its declaration throws a TDZ
  // ReferenceError on every call, which silently kills all tagging.
  const stats = { passes: 0, ms: 0 };

  // Mark elements with our own stable classes. WhatsApp's generated class names
  // live only in selectors.js; the CSS keys off wap-t-* exclusively.
  function tagWithin(root) {
    const t0 = performance.now();
    stats.passes++;
    const sel = SEL();
    for (const cat of CATEGORIES) {
      const list = sel[cat];
      if (!list) continue;
      const cls = CLASS_FOR[cat];
      const query = list.join(',');
      let matches;
      try {
        matches = root.querySelectorAll(query);
      } catch (e) {
        continue; // a malformed selector must not take the whole pass down
      }
      for (const el of matches)
        if (!el.classList.contains(cls)) el.classList.add(cls);
      // querySelectorAll only looks at descendants, but when the observer hands
      // us a newly added node the node itself can be a target.
      try {
        if (root.nodeType === 1 && root.matches(query)) root.classList.add(cls);
      } catch (e) { /* ignore */ }
    }

    // Categories blur containers, so mark the chrome that must stay readable
    // (timestamps, unread badges, delivery ticks) inside them.
    const exempt = window.__wapExempt;
    if (exempt && exempt.length) {
      try {
        for (const el of root.querySelectorAll(exempt.join(',')))
          el.classList.add('wap-t-exempt');
      } catch (e) { /* ignore a bad selector */ }
    }

    stats.ms += performance.now() - t0;
  }

  function retagAll() {
    if (!document.body) return;
    tagWithin(document.body);
  }

  window.__wapStats = () => ({ ...stats, avgMs: +(stats.ms / (stats.passes || 1)).toFixed(3) });

  // --- state application ----------------------------------------------------
  function apply() {
    const b = document.body;
    if (!b) return;

    b.classList.toggle('wap-privacy-on', state.on);
    for (const cat of CATEGORIES) {
      // Master toggle gates every category.
      b.classList.toggle('wap-hide-' + cat, state.on && !!state[cat]);
    }
    b.classList.toggle('wap-hover-reveal', !!state.hoverReveal);
  }

  // --- observer -------------------------------------------------------------
  // WhatsApp mutates the DOM constantly (typing indicators, timestamps, the
  // virtualized list recycling rows). Tagging synchronously per mutation pins
  // the CPU, so coalesce into one pass per animation frame.
  // rAF does not fire while the window is hidden or minimized, which would stall
  // tagging exactly when content must stay blurred. Race it against a timer so
  // whichever fires first wins.
  //
  // Only the subtrees the observer reported are re-tagged. Re-scanning the whole
  // document on every mutation is what made this pin the CPU on a busy chat.
  let pending = false;
  let dirty = [];
  function scheduleRetag(roots) {
    if (roots && roots.length) dirty.push(...roots);
    else dirty = null;                     // null => full sweep
    if (pending) return;
    pending = true;
    const flush = () => {
      if (!pending) return;
      pending = false;
      const scope = dirty;
      dirty = [];
      if (!scope) return retagAll();
      for (const el of scope)
        if (el.isConnected) tagWithin(el);
    };
    requestAnimationFrame(flush);
    setTimeout(flush, 32);
  }

  function startObserver() {
    if (!document.body) return false;
    new MutationObserver((records) => {
      // Collect only the element subtrees that actually changed.
      const roots = [];
      for (const r of records) {
        if (r.type === 'attributes') {
          if (r.target.nodeType === 1) roots.push(r.target);
          continue;
        }
        for (const n of r.addedNodes)
          if (n.nodeType === 1) roots.push(n);
        // A text-only change still needs its container re-checked.
        if (!r.addedNodes.length && r.target.nodeType === 1) roots.push(r.target);
      }
      if (roots.length) scheduleRetag(roots);
    }).observe(document.body, {
      childList: true,
      subtree: true,
      // Re-tag when an existing node is restyled/reused: the virtualized chat
      // list swaps content into recycled rows without adding nodes.
      attributes: true,
      attributeFilter: ['class', 'src', 'title'],
    });
    return true;
  }

  // --- native bridge --------------------------------------------------------
  function send(msg) {
    if (window.chrome && chrome.webview) chrome.webview.postMessage(JSON.stringify(msg));
  }

  if (window.chrome && chrome.webview) {
    chrome.webview.addEventListener('message', (ev) => {
      let msg;
      try {
        msg = typeof ev.data === 'string' ? JSON.parse(ev.data) : ev.data;
      } catch (e) {
        return;
      }
      if (!msg) return;

      if (msg.type === 'state') {
        window.__wapApplyState(msg);
      } else if (msg.type === 'ping') {
        send({ type: 'pong', id: msg.id, url: location.href });
      }
    });
  }

  // --- startup --------------------------------------------------------------
  function boot() {
    // DOMContentLoaded may already have passed, or <body> may still be missing;
    // wait for it rather than throwing.
    if (!document.body) {
      setTimeout(boot, 0);   // not rAF: must also work in a hidden window
      return;
    }
    injectStyle();
    apply();
    retagAll();
    startObserver();

    // Only now is it safe to lift the fail-safe: styles are in, categories are
    // applied, and the observer is live to catch whatever renders next.
    booted = true;
    document.documentElement.classList.remove('wap-boot');
    send({ type: 'ready' });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot, { once: true });
  } else {
    boot();
  }

  // WhatsApp swaps its whole shell on login and on some route changes. The
  // observer covers incremental updates, so this only needs to catch a wholesale
  // pane replacement -- a slow, cheap backstop rather than a hot poll.
  // ponytail: 10s backstop; remove if the observer proves sufficient.
  setInterval(retagAll, 10000);

  // Applies a partial state patch. Used by the native bridge path above and by
  // the self-check harness (web/test_privacy.js).
  window.__wapApplyState = (patch) => {
    for (const k of ['on', ...CATEGORIES, 'hoverReveal'])
      if (patch && k in patch) state[k] = !!patch[k];
    apply();
    retagAll();
  };

  window.__wapPrivacy = { apply, retagAll, get state() { return state; } };
})();
