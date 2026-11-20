/* Keyboard Volume App documentation: user-adjustable sidebar and TOC widths.
   Drag the thin handle on the sidebar's right edge or the TOC's left edge; use
   the arrow keys (Shift = bigger step), Home/End for min/max, double-click to
   reset. Widths are stored in localStorage and exposed as the CSS variables
   --sidebar-width and --toc-width (layout rules live in theme.css). */
(function () {
  'use strict';

  const doc = document;
  const root = doc.documentElement;
  const body = doc.body;
  const KVA = window.KVA;
  const MIN_CONTENT = 600; // keep a readable article column (text plus padding)
  const TOC_RIGHT = 26;    // gap between the TOC and the window edge (see theme.css)
  const TOC_GAP = 35;      // space reserved between article and TOC column

  const CONFIG = {
    sidebar: { key: 'kva-docs-sidebar-width', prop: '--sidebar-width', def: 280, min: 200, max: 480 },
    toc: { key: 'kva-docs-toc-width', prop: '--toc-width', def: 205, min: 160, max: 420 }
  };
  const LABELS = {
    en: { sidebar: 'Resize sidebar', toc: 'Resize page contents' },
    pl: { sidebar: 'Zmień szerokość panelu bocznego', toc: 'Zmień szerokość spisu treści' }
  };

  const preferred = { sidebar: CONFIG.sidebar.def, toc: CONFIG.toc.def }; // what the user chose
  const effective = { sidebar: CONFIG.sidebar.def, toc: CONFIG.toc.def }; // after fitting the window
  Object.keys(CONFIG).forEach(function (kind) {
    try {
      const stored = parseInt(localStorage.getItem(CONFIG[kind].key), 10);
      if (stored >= CONFIG[kind].min && stored <= CONFIG[kind].max) preferred[kind] = stored;
    } catch (_) { /* storage unavailable */ }
  });

  function clamp(value, min, max) { return Math.min(max, Math.max(min, value)); }
  function viewportWidth() { return root.clientWidth; }
  function sidebarClosed() { return body.classList.contains('close'); }
  function tocElement() { return doc.querySelector('.page-toc'); }
  function tocVisible() {
    const toc = tocElement();
    return !!toc && !toc.hidden && getComputedStyle(toc).display !== 'none';
  }

  // Largest width that still leaves MIN_CONTENT for the article.
  function maxFor(kind) {
    const vw = viewportWidth();
    const limit = kind === 'sidebar'
      ? vw - (tocVisible() ? effective.toc + TOC_GAP : 0) - MIN_CONTENT
      : vw - (sidebarClosed() ? 0 : effective.sidebar) - TOC_GAP - MIN_CONTENT;
    return Math.max(CONFIG[kind].min, Math.min(CONFIG[kind].max, limit));
  }

  const handles = {};
  function makeHandle(kind) {
    const el = doc.createElement('div');
    el.className = 'resizer resizer-' + kind;
    el.setAttribute('role', 'separator');
    el.setAttribute('aria-orientation', 'vertical');
    el.tabIndex = 0;
    el.title = '';
    body.appendChild(el);
    handles[kind] = el;
    return el;
  }

  function apply() {
    ['sidebar', 'toc'].forEach(function (kind) {
      effective[kind] = clamp(preferred[kind], CONFIG[kind].min, maxFor(kind));
      root.style.setProperty(CONFIG[kind].prop, effective[kind] + 'px');
    });
    const labels = LABELS[KVA.lang()] || LABELS.en;
    Object.keys(handles).forEach(function (kind) {
      const el = handles[kind];
      el.setAttribute('aria-label', labels[kind]);
      el.setAttribute('aria-valuemin', String(CONFIG[kind].min));
      el.setAttribute('aria-valuemax', String(maxFor(kind)));
      el.setAttribute('aria-valuenow', String(effective[kind]));
    });
    handles.toc.hidden = !tocVisible();
  }

  function setWidth(kind, value, persist) {
    preferred[kind] = clamp(Math.round(value), CONFIG[kind].min, maxFor(kind));
    apply();
    if (!persist) return;
    try { localStorage.setItem(CONFIG[kind].key, String(preferred[kind])); } catch (_) { /* optional */ }
  }

  function reset(kind) {
    preferred[kind] = CONFIG[kind].def;
    try { localStorage.removeItem(CONFIG[kind].key); } catch (_) { /* optional */ }
    apply();
  }

  function attach(kind) {
    const el = makeHandle(kind);
    let dragging = false;
    let frame = 0;
    let pending = 0;

    function widthAt(clientX) {
      return kind === 'sidebar' ? clientX : viewportWidth() - TOC_RIGHT - clientX;
    }
    function finish() {
      if (!dragging) return;
      dragging = false;
      cancelAnimationFrame(frame);
      frame = 0;
      body.classList.remove('resizing');
      el.classList.remove('active');
      setWidth(kind, preferred[kind], true); // persist the final value
    }

    el.addEventListener('pointerdown', function (event) {
      if (event.button !== 0) return;
      event.preventDefault();
      dragging = true;
      try { el.setPointerCapture(event.pointerId); } catch (_) { /* the pointer may already be gone */ }
      body.classList.add('resizing');
      el.classList.add('active');
    });
    el.addEventListener('pointermove', function (event) {
      if (!dragging) return;
      pending = widthAt(event.clientX);
      if (frame) return;
      frame = requestAnimationFrame(function () {
        frame = 0;
        setWidth(kind, pending, false);
      });
    });
    el.addEventListener('pointerup', finish);
    el.addEventListener('pointercancel', finish);
    el.addEventListener('lostpointercapture', finish);
    el.addEventListener('dblclick', function () { reset(kind); });
    el.addEventListener('keydown', function (event) {
      const step = event.shiftKey ? 48 : 16;
      // For the TOC, moving left widens the column; for the sidebar, moving right does.
      const direction = kind === 'sidebar' ? 1 : -1;
      let next = null;
      if (event.key === 'ArrowRight') next = effective[kind] + direction * step;
      else if (event.key === 'ArrowLeft') next = effective[kind] - direction * step;
      else if (event.key === 'Home') next = CONFIG[kind].min;
      else if (event.key === 'End') next = maxFor(kind);
      else if (event.key === 'Enter' || event.key === 'Delete' || event.key === 'Backspace') { reset(kind); event.preventDefault(); return; }
      if (next === null) return;
      event.preventDefault();
      setWidth(kind, next, true);
    });
  }

  attach('sidebar');
  attach('toc');
  apply();

  window.addEventListener('resize', apply);
  // The sidebar can be collapsed and the TOC shown or hidden by other scripts.
  new MutationObserver(apply).observe(body, { attributes: true, attributeFilter: ['class'] });
  window.$docsify.plugins.push(function (hook) {
    hook.doneEach(apply);
  });
}());
