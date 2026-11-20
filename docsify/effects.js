/* Keyboard Volume App documentation: motion and reading aids.
   Loaded after app.js (needs window.KVA / window.$docsify). Provides: page
   transitions (View Transitions API with a keyframe fallback), route and
   reading progress bars, back-to-top button, TOC scrollspy, GitHub-style
   callouts and the smooth theme switch. Visual rules live in motion.css. */
(function () {
  'use strict';

  const doc = document;
  const root = doc.documentElement;
  const KVA = window.KVA;
  const header = doc.querySelector('.site-header');
  const reduceMotion = matchMedia('(prefers-reduced-motion: reduce)');
  // Tests can force the fallback with ?no-vt in the URL.
  const supportsVT = typeof doc.startViewTransition === 'function' && !/[?&]no-vt\b/.test(location.search);

  const STRINGS = {
    en: { top: 'Back to top', note: 'Note', tip: 'Tip', important: 'Important', warning: 'Warning', caution: 'Caution' },
    pl: { top: 'Wróć na górę', note: 'Uwaga', tip: 'Wskazówka', important: 'Ważne', warning: 'Ostrzeżenie', caution: 'Przestroga' }
  };
  function t() { return STRINGS[KVA.lang()] || STRINGS.en; }

  // Starts a view transition. A transition that is skipped (a newer one starts, the tab
  // is hidden) rejects ready/updateCallbackDone with an AbortError; that is expected,
  // so swallow it instead of logging "Uncaught (in promise)".
  function startTransition(update) {
    const transition = doc.startViewTransition(update);
    const ignore = function () { /* skipped transitions are fine */ };
    transition.ready.catch(ignore);
    transition.updateCallbackDone.catch(ignore);
    return transition;
  }

  // Route part of a "#/path.md?id=x" hash, normalised for comparisons.
  function pathOf(hash) {
    return String(hash || '').replace(/^#/, '').split('?')[0].replace(/\.md$/, '') || '/';
  }

  // ---- Smooth theme switch -----------------------------------------------
  // The viewport is cross-faded as two GPU snapshots. Transitioning the colours of
  // every element instead stutters on long pages (thousands of nodes); without the
  // View Transitions API (or with reduced motion) the theme simply switches at once.
  const applyTheme = KVA.changeTheme;
  KVA.changeTheme = function (name) {
    if (!supportsVT || reduceMotion.matches || name === root.dataset.theme) {
      applyTheme(name);
      return;
    }
    root.classList.add('vt-theme');
    const cleanup = function () { root.classList.remove('vt-theme'); };
    try {
      startTransition(function () { applyTheme(name); }).finished.then(cleanup, cleanup);
    } catch (_) {
      cleanup();
      applyTheme(name);
    }
  };

  // ---- Route progress bar --------------------------------------------------
  const routeBar = doc.querySelector('.route-progress');
  let routeTimer = 0;
  function routeStart() {
    clearTimeout(routeTimer);
    routeBar.classList.remove('running', 'done');
    void routeBar.offsetWidth; // restart the transition
    routeBar.classList.add('running');
    routeTimer = setTimeout(routeDone, 8000);
  }
  function routeDone() {
    clearTimeout(routeTimer);
    if (!routeBar.classList.contains('running')) return;
    routeBar.classList.remove('running');
    routeBar.classList.add('done');
    routeTimer = setTimeout(function () { routeBar.classList.remove('done'); }, 700);
  }
  let lastPath = pathOf(location.hash);
  window.addEventListener('hashchange', function () {
    const path = pathOf(location.hash);
    if (path === lastPath) return; // same page, only the section anchor changed
    lastPath = path;
    routeStart();
  });
  routeStart();

  // ---- Page transitions ----------------------------------------------------
  // The transition callback waits until Docsify has rendered the new page
  // (doneEach), so the old page stays visible while the markdown is fetched.
  let navDone = true;
  let pendingResolve = null;
  let navTimer = 0;
  function finishNav() {
    navDone = true;
    clearTimeout(navTimer);
    if (pendingResolve) {
      const resolve = pendingResolve;
      pendingResolve = null;
      resolve();
    }
  }
  function beginTransition(targetHash) {
    if (!supportsVT || reduceMotion.matches) return;
    if (pathOf(targetHash) === pathOf(location.hash)) return;
    if (root.classList.contains('vt-active')) return;
    navDone = false;
    root.classList.add('vt-active'); // names the fixed chrome so only the article animates
    let transition;
    try {
      transition = startTransition(function () {
        if (navDone) return undefined; // Docsify was faster than the capture
        return new Promise(function (resolve) {
          pendingResolve = resolve;
          navTimer = setTimeout(finishNav, 1200);
        });
      });
    } catch (_) {
      root.classList.remove('vt-active');
      navDone = true;
      return;
    }
    const cleanup = function () { root.classList.remove('vt-active'); };
    transition.finished.then(cleanup, cleanup);
  }
  doc.addEventListener('click', function (event) {
    if (event.defaultPrevented || event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
    const link = event.target.closest && event.target.closest('a[href^="#/"]');
    if (!link || (link.target && link.target !== '_self')) return;
    beginTransition(link.getAttribute('href'));
  }, true);
  const plainGo = KVA.go;
  KVA.go = function (hash) {
    beginTransition(hash);
    plainGo(hash);
  };

  // Fallback entrance (first load, back/forward, browsers without View Transitions).
  let enterTimer = 0;
  function pageEnter() {
    Array.prototype.slice.call(doc.querySelectorAll('.markdown-section > *'), 0, 8).forEach(function (el, i) {
      el.classList.add('rise');
      el.style.setProperty('--i', i);
    });
    root.classList.remove('page-enter');
    void root.offsetWidth;
    root.classList.add('page-enter');
    clearTimeout(enterTimer);
    enterTimer = setTimeout(function () { root.classList.remove('page-enter'); }, 900);
  }

  // ---- Reading progress, back to top, header shadow, scrollspy -------------
  const progress = doc.querySelector('.read-progress');
  const progressBar = progress.firstElementChild;
  const toTop = doc.createElement('button');
  toTop.type = 'button';
  toTop.className = 'to-top';
  toTop.inert = true;
  toTop.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 19V5M5 12l7-7 7 7"/></svg>';
  toTop.addEventListener('click', function () {
    window.scrollTo({ top: 0, behavior: reduceMotion.matches ? 'auto' : 'smooth' });
  });
  doc.body.appendChild(toTop);

  let spyLinks = [];
  let spyHeadings = [];
  let spyIndicator = null;
  let spyActive = -2;

  function spyVisible() {
    const toc = doc.querySelector('.page-toc');
    return !toc.hidden && getComputedStyle(toc).display !== 'none';
  }

  function updateSpy(force) {
    if (!spyHeadings.length || !spyVisible()) return;
    // A heading counts as "reached" a bit below the header, not only at its very edge.
    const line = header.offsetHeight + Math.min(160, window.innerHeight * 0.2);
    let index = -1;
    for (let i = 0; i < spyHeadings.length; i += 1) {
      if (spyHeadings[i].getBoundingClientRect().top <= line) index = i; else break;
    }
    if (window.innerHeight + window.scrollY >= root.scrollHeight - 2) index = spyHeadings.length - 1;
    if (index === spyActive && !force) return;
    spyActive = index;
    spyLinks.forEach(function (link, i) {
      if (i === index) link.setAttribute('aria-current', 'location'); else link.removeAttribute('aria-current');
    });
    if (index < 0) {
      spyIndicator.style.opacity = '0';
      return;
    }
    const link = spyLinks[index];
    if (force) spyIndicator.style.transition = 'none'; // place without animating on first show
    spyIndicator.style.height = link.offsetHeight + 'px';
    spyIndicator.style.transform = 'translateY(' + link.offsetTop + 'px)';
    spyIndicator.style.opacity = '1';
    if (force) { void spyIndicator.offsetWidth; spyIndicator.style.transition = ''; }
    // Keep the active entry visible inside the scrollable TOC.
    const toc = doc.querySelector('.page-toc');
    const top = link.offsetTop;
    if (top < toc.scrollTop + 8 || top + link.offsetHeight > toc.scrollTop + toc.clientHeight - 8) {
      toc.scrollTop = Math.max(0, top - toc.clientHeight / 2);
    }
  }

  function setupSpy() {
    const nav = doc.querySelector('.page-toc nav');
    spyLinks = [];
    spyHeadings = [];
    Array.prototype.forEach.call(nav.querySelectorAll('a'), function (link) {
      const match = /[?&]id=([^&]+)/.exec(link.getAttribute('href') || '');
      const heading = match && doc.getElementById(decodeURIComponent(match[1]));
      if (!heading) return;
      spyLinks.push(link);
      spyHeadings.push(heading);
    });
    spyIndicator = doc.createElement('span');
    spyIndicator.className = 'toc-indicator';
    nav.appendChild(spyIndicator);
    spyActive = -2;
    updateSpy(true);
  }

  let ticking = false;
  function update() {
    ticking = false;
    const y = window.scrollY;
    header.classList.toggle('scrolled', y > 4);
    const max = root.scrollHeight - window.innerHeight;
    const ratio = max > 0 ? Math.min(1, Math.max(0, y / max)) : 0;
    progressBar.style.transform = 'scaleX(' + ratio + ')';
    progress.classList.toggle('visible', max > 200 && y > 4);
    const showTop = y > 600;
    toTop.classList.toggle('visible', showTop);
    toTop.inert = !showTop;
    updateSpy(false);
  }
  function onScroll() {
    if (ticking) return;
    ticking = true;
    requestAnimationFrame(update);
  }
  window.addEventListener('scroll', onScroll, { passive: true });
  window.addEventListener('resize', function () { onScroll(); updateSpy(true); });

  // ---- Callouts (GitHub alert syntax) ---------------------------------------
  const ICONS = {
    note: '<circle cx="12" cy="12" r="9"/><path d="M12 11v5M12 8h.01"/>',
    tip: '<path d="M9 18h6M10 21h4M12 3a6 6 0 0 0-4 10.5c.7.7 1 1.5 1 2.5h6c0-1 .3-1.8 1-2.5A6 6 0 0 0 12 3z"/>',
    important: '<circle cx="12" cy="12" r="9"/><path d="M12 7v6M12 16.5h.01"/>',
    warning: '<path d="M12 3l10 18H2L12 3zM12 10v5M12 18h.01"/>',
    caution: '<path d="M8 3h8l5 5v8l-5 5H8l-5-5V8l5-5zM12 8v5M12 16h.01"/>'
  };
  const MARKER = /^\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\][ \t]*\r?\n?/i;

  function enhanceCallouts() {
    Array.prototype.forEach.call(doc.querySelectorAll('.markdown-section blockquote'), function (quote) {
      if (quote.classList.contains('callout')) return;
      const paragraph = quote.querySelector(':scope > p');
      const first = paragraph && paragraph.firstChild;
      if (!first || first.nodeType !== Node.TEXT_NODE) return;
      const match = MARKER.exec(first.textContent);
      if (!match) return;
      const type = match[1].toLowerCase();
      first.textContent = first.textContent.slice(match[0].length);
      if (!first.textContent) first.remove();
      if (paragraph.firstChild && paragraph.firstChild.nodeName === 'BR') paragraph.firstChild.remove();
      if (!paragraph.childNodes.length) paragraph.remove();
      const title = doc.createElement('p');
      title.className = 'callout-title';
      title.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true">' + ICONS[type] + '</svg>';
      const label = doc.createElement('span');
      label.textContent = t()[type];
      title.appendChild(label);
      quote.insertBefore(title, quote.firstChild);
      quote.classList.add('callout', 'callout-' + type);
      quote.setAttribute('role', 'note');
    });
  }

  // ---- Docsify hook ---------------------------------------------------------
  window.$docsify.plugins.push(function (hook) {
    hook.doneEach(function () {
      const viaTransition = root.classList.contains('vt-active');
      enhanceCallouts();
      toTop.setAttribute('aria-label', t().top);
      setupSpy();
      update();
      if (!viaTransition) pageEnter();
      routeDone();
      finishNav(); // last: lets the transition capture the finished DOM
    });
  });
}());
