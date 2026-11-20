/* Keyboard Volume App documentation: command palette (Ctrl/Cmd+K or "/").
   Searches sidebar pages, section headings and body text from the index that
   Docsify's search plugin keeps in localStorage ("docsify.search.index"), plus
   a few actions and recently opened pages. Styling lives in motion.css. */
(function () {
  'use strict';

  const doc = document;
  const KVA = window.KVA;
  const RECENT_KEY = 'kva-docs-recent';
  const INDEX_KEY = 'docsify.search.index';
  const GITHUB_URL = 'https://github.com/Adiker/keyboard-volume-app';
  const reduceMotion = matchMedia('(prefers-reduced-motion: reduce)');
  const isMac = /Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent);

  const STRINGS = {
    en: {
      button: 'Search', placeholder: 'Search the documentation…', empty: 'No results',
      recent: 'Recent', pages: 'Pages', sections: 'Sections', actions: 'Actions',
      theme: 'Theme', themes: { light: 'Light', dark: 'Dark', oled: 'OLED' },
      switchLang: 'Switch to Polski', github: 'Open GitHub repository',
      keywords: { theme: 'theme dark light oled color', lang: 'language polski polish', github: 'github source repository code' },
      navigate: 'navigate', open: 'open', close: 'close'
    },
    pl: {
      button: 'Szukaj', placeholder: 'Szukaj w dokumentacji…', empty: 'Brak wyników',
      recent: 'Ostatnio', pages: 'Strony', sections: 'Sekcje', actions: 'Akcje',
      theme: 'Motyw', themes: { light: 'Jasny', dark: 'Ciemny', oled: 'OLED' },
      switchLang: 'Przełącz na English', github: 'Otwórz repozytorium GitHub',
      keywords: { theme: 'motyw ciemny jasny oled kolor theme dark light', lang: 'jezyk angielski english language', github: 'github zrodlo repozytorium kod' },
      navigate: 'nawigacja', open: 'otwórz', close: 'zamknij'
    }
  };
  function t() { return STRINGS[KVA.lang()] || STRINGS.en; }

  // ---- Text helpers --------------------------------------------------------
  // Lowercase and strip diacritics one UTF-16 unit at a time so that indices in
  // the normalised string still point into the original (needed for <mark>).
  function norm(text) {
    let out = '';
    for (let i = 0; i < text.length; i += 1) {
      const ch = text.charAt(i).toLowerCase();
      out += ch === 'ł' ? 'l' : ch.normalize('NFD').charAt(0);
    }
    return out;
  }
  function pathOf(hash) {
    return String(hash || '').replace(/^#/, '').split('?')[0].replace(/\.md$/, '') || '/';
  }
  function cleanTitle(title) {
    return String(title || '').replace(/[`*_]/g, '').replace(/&amp;/g, '&').replace(/&lt;/g, '<')
      .replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'").trim();
  }
  function langOfHref(href) {
    return href.indexOf('/docs/pl/') >= 0 || href.indexOf('README.pl') >= 0 ? 'pl' : 'en';
  }

  // ---- Data sources --------------------------------------------------------
  function collectPages() {
    const pages = [];
    Array.prototype.forEach.call(doc.querySelectorAll('.sidebar-nav > ul > li'), function (group) {
      const toggle = group.querySelector(':scope > .nav-group-toggle');
      const groupName = toggle ? toggle.textContent.trim() : '';
      Array.prototype.forEach.call(group.querySelectorAll('a'), function (link) {
        const href = link.getAttribute('href') || '';
        if (href.indexOf('#/') !== 0) return;
        pages.push({ kind: 'page', title: link.textContent.trim(), sub: groupName, href: href, lang: langOfHref(href) });
      });
    });
    return pages;
  }

  let cachedRaw = null;
  let cachedSections = [];
  let cachedBodies = {};
  function loadIndex(pages) {
    let raw = null;
    try { raw = localStorage.getItem(INDEX_KEY); } catch (_) { /* storage unavailable */ }
    if (raw === cachedRaw && cachedRaw !== null) return;
    cachedRaw = raw;
    cachedSections = [];
    cachedBodies = {};
    let data = null;
    try { data = raw ? JSON.parse(raw) : null; } catch (_) { data = null; }
    if (!data) return;
    const titles = {};
    pages.forEach(function (page) { titles[pathOf(page.href)] = page.title; });
    Object.keys(data).forEach(function (file) {
      const entries = data[file];
      if (!entries || typeof entries !== 'object') return;
      Object.keys(entries).forEach(function (key) {
        const entry = entries[key];
        const slug = (entry && entry.slug) || key;
        if (!entry || typeof slug !== 'string') return;
        const path = pathOf(slug);
        // Strip Markdown noise (links, table pipes, emphasis) so snippets read as prose.
        const body = String(entry.body || '')
          .replace(/!?\[([^\]]*)\]\([^)]*\)/g, '$1').replace(/[`*>|#]/g, ' ').replace(/\s+/g, ' ').trim();
        if (slug.indexOf('?id=') === -1) { // page-level entry: only its body text is useful
          cachedBodies[path] = (cachedBodies[path] || '') + ' ' + body;
          return;
        }
        const title = cleanTitle(entry.title);
        if (!title) return;
        cachedSections.push({
          kind: 'section', title: title, href: slug, body: body, page: titles[path] || path.replace(/^\//, ''),
          lang: langOfHref(slug), nt: norm(title), nb: norm(body)
        });
      });
    });
  }

  function currentPageSections() {
    const found = [];
    Array.prototype.forEach.call(doc.querySelectorAll('.markdown-section h2, .markdown-section h3'), function (heading) {
      const anchor = heading.querySelector('a.anchor');
      if (!anchor) return;
      const title = heading.textContent.replace(/#$/, '').trim();
      found.push({ kind: 'section', title: title, href: anchor.getAttribute('href'), body: '', page: doc.title, lang: KVA.lang(), nt: norm(title), nb: '' });
    });
    return found;
  }

  function readRecent() {
    try {
      const list = JSON.parse(localStorage.getItem(RECENT_KEY) || '[]');
      return Array.isArray(list) ? list.filter(function (r) { return r && r.href && r.title; }) : [];
    } catch (_) { return []; }
  }
  function rememberPage(path, title) {
    if (!title || path === '/_404') return;
    const href = '#' + path;
    const list = readRecent().filter(function (r) { return pathOf(r.href) !== pathOf(href); });
    list.unshift({ href: href, title: title });
    try { localStorage.setItem(RECENT_KEY, JSON.stringify(list.slice(0, 6))); } catch (_) { /* optional */ }
  }

  function actionItems() {
    const s = t();
    const order = ['light', 'dark', 'oled'];
    const next = order[(order.indexOf(KVA.theme()) + 1) % order.length];
    const other = KVA.lang() === 'pl' ? 'en' : 'pl';
    return [
      { kind: 'action', title: s.theme + ': ' + s.themes[next], sub: '', kw: s.keywords.theme, run: function () { KVA.setTheme(next); } },
      { kind: 'action', title: s.switchLang, sub: '', kw: s.keywords.lang, run: function () {
        const target = KVA.counterpartRoute(location.hash.replace(/^#/, '').split('?')[0] || '/');
        KVA.go('#' + target);
      }, lang: other },
      { kind: 'action', title: s.github, sub: GITHUB_URL.replace('https://', ''), kw: s.keywords.github, run: function () { window.open(GITHUB_URL, '_blank', 'noopener'); } }
    ].map(function (item) { item.nt = norm(item.title); item.nb = norm(item.kw || ''); return item; });
  }

  // ---- Matching ------------------------------------------------------------
  function scoreItem(item, tokens) {
    let total = 0;
    for (let i = 0; i < tokens.length; i += 1) {
      const token = tokens[i];
      const at = item.nt.indexOf(token);
      let s;
      if (at === 0) s = 100;
      else if (at > 0) s = /[a-z0-9]/.test(item.nt.charAt(at - 1)) ? 40 : 70;
      else if (item.ns && item.ns.indexOf(token) >= 0) s = 20;
      else if (item.nb && item.nb.indexOf(token) >= 0) s = 8;
      else return -1;
      total += s;
    }
    if (item.kind === 'page') total += 20;
    if (item.lang === KVA.lang()) total += 10;
    return total;
  }

  function buildResults(query) {
    const pages = collectPages();
    loadIndex(pages);
    pages.forEach(function (page) {
      page.nt = norm(page.title);
      page.ns = norm(page.sub);
      page.nb = norm(cachedBodies[pathOf(page.href)] || '');
    });
    const sections = cachedSections.length ? cachedSections : currentPageSections();
    const actions = actionItems();
    const s = t();
    const tokens = norm(query).split(/\s+/).filter(Boolean);

    if (!tokens.length) {
      const here = pathOf(location.hash);
      const recent = readRecent().filter(function (r) { return pathOf(r.href) !== here; }).slice(0, 5)
        .map(function (r) { return { kind: 'recent', title: r.title, href: r.href, sub: '' }; });
      const lang = KVA.lang();
      const sorted = pages.slice().sort(function (a, b) { return (b.lang === lang) - (a.lang === lang); });
      return [
        { label: s.recent, items: recent },
        { label: s.pages, items: sorted.slice(0, 10) },
        { label: s.actions, items: actions }
      ].filter(function (g) { return g.items.length; });
    }
    function pick(list, limit) {
      return list.map(function (item) { return { item: item, score: scoreItem(item, tokens) }; })
        .filter(function (r) { return r.score >= 0; })
        .sort(function (a, b) { return b.score - a.score; })
        .slice(0, limit).map(function (r) { return r.item; });
    }
    return [
      { label: s.pages, items: pick(pages, 8) },
      { label: s.sections, items: pick(sections, 20) },
      { label: s.actions, items: pick(actions, 3) }
    ].filter(function (g) { return g.items.length; });
  }

  // ---- Rendering -----------------------------------------------------------
  function highlight(target, text, tokens) {
    const normalized = norm(text);
    const ranges = [];
    tokens.forEach(function (token) {
      const at = normalized.indexOf(token);
      if (at >= 0) ranges.push([at, at + token.length]);
    });
    ranges.sort(function (a, b) { return a[0] - b[0]; });
    let cursor = 0;
    ranges.forEach(function (range) {
      if (range[0] < cursor) range[0] = cursor;
      if (range[1] <= range[0]) return;
      if (range[0] > cursor) target.appendChild(doc.createTextNode(text.slice(cursor, range[0])));
      const mark = doc.createElement('mark');
      mark.textContent = text.slice(range[0], range[1]);
      target.appendChild(mark);
      cursor = range[1];
    });
    if (cursor < text.length) target.appendChild(doc.createTextNode(text.slice(cursor)));
  }

  function snippet(item, tokens) {
    if (!item.body) return null;
    const nb = item.nb;
    let at = -1;
    for (let i = 0; i < tokens.length && at < 0; i += 1) at = nb.indexOf(tokens[i]);
    if (at < 0) return null;
    const start = Math.max(0, at - 30);
    return (start > 0 ? '… ' : '') + item.body.slice(start, start + 110);
  }

  const dialog = doc.createElement('dialog');
  dialog.className = 'palette';
  dialog.innerHTML =
    '<div class="palette-input-row">' +
    '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="11" cy="11" r="7"/><path d="M20 20l-3.5-3.5"/></svg>' +
    '<input class="palette-input" type="text" role="combobox" aria-expanded="true" aria-controls="palette-list" aria-autocomplete="list" autocomplete="off" autocapitalize="off" spellcheck="false">' +
    '</div>' +
    '<ul class="palette-list" id="palette-list" role="listbox"></ul>' +
    '<div class="palette-foot"><span><kbd>↑</kbd> <kbd>↓</kbd> <span data-hint="navigate"></span></span><span><kbd>↵</kbd> <span data-hint="open"></span></span><span><kbd>Esc</kbd> <span data-hint="close"></span></span></div>';
  doc.body.appendChild(dialog);
  const input = dialog.querySelector('.palette-input');
  const list = dialog.querySelector('.palette-list');
  const openButton = doc.getElementById('palette-open');
  let options = [];
  let active = -1;
  let closing = 0;

  function applyLanguage() {
    const s = t();
    dialog.setAttribute('aria-label', s.button);
    input.placeholder = s.placeholder;
    input.setAttribute('aria-label', s.placeholder);
    openButton.setAttribute('aria-label', s.button);
    openButton.querySelector('span').textContent = s.button;
    openButton.querySelector('kbd').textContent = isMac ? '⌘ K' : 'Ctrl K';
    ['navigate', 'open', 'close'].forEach(function (key) {
      dialog.querySelector('[data-hint="' + key + '"]').textContent = s[key];
    });
  }

  function setActive(index, scroll) {
    if (!options.length) { active = -1; input.removeAttribute('aria-activedescendant'); return; }
    active = (index + options.length) % options.length;
    options.forEach(function (option, i) { option.el.setAttribute('aria-selected', String(i === active)); });
    input.setAttribute('aria-activedescendant', options[active].el.id);
    if (scroll) options[active].el.scrollIntoView({ block: 'nearest' });
  }

  function render() {
    const query = input.value;
    const tokens = norm(query).split(/\s+/).filter(Boolean);
    const groups = buildResults(query);
    list.replaceChildren();
    options = [];
    if (!groups.length) {
      const empty = doc.createElement('li');
      empty.className = 'palette-empty';
      empty.setAttribute('role', 'presentation');
      empty.textContent = t().empty;
      list.appendChild(empty);
      setActive(-1);
      return;
    }
    groups.forEach(function (group) {
      const heading = doc.createElement('li');
      heading.className = 'palette-group';
      heading.setAttribute('role', 'presentation');
      heading.textContent = group.label;
      list.appendChild(heading);
      group.items.forEach(function (item) {
        const el = doc.createElement('li');
        el.className = 'palette-item';
        el.id = 'palette-option-' + options.length;
        el.setAttribute('role', 'option');
        el.setAttribute('aria-selected', 'false');
        const title = doc.createElement('span');
        title.className = 'palette-title';
        highlight(title, item.title, tokens);
        el.appendChild(title);
        let sub = item.kind === 'section' ? item.page : item.sub;
        let subTokens = [];
        if (item.kind === 'section') {
          const found = snippet(item, tokens);
          if (found && item.nt.indexOf(tokens[0] || '') < 0) { sub = found; subTokens = tokens; }
        }
        if (sub) {
          const line = doc.createElement('span');
          line.className = 'palette-sub';
          highlight(line, sub, subTokens);
          el.appendChild(line);
        }
        el.addEventListener('mousemove', function () {
          const index = options.findIndex(function (o) { return o.el === el; });
          if (index !== active) setActive(index, false);
        });
        el.addEventListener('click', function () { activate(item); });
        list.appendChild(el);
        options.push({ el: el, item: item });
      });
    });
    setActive(0, false);
    list.scrollTop = 0;
  }

  // ---- Open, close, activate -----------------------------------------------
  function isOpen() { return dialog.open && !closing; }

  function openPalette() {
    if (dialog.open) return;
    applyLanguage();
    input.value = '';
    render();
    dialog.showModal();
    requestAnimationFrame(function () { dialog.classList.add('is-open'); });
    input.focus();
  }

  function closeNow() {
    clearTimeout(closing);
    closing = 0;
    dialog.classList.remove('is-open');
    if (dialog.open) dialog.close();
  }

  function closePalette() {
    if (!dialog.open || closing) return;
    if (reduceMotion.matches) { closeNow(); return; }
    dialog.classList.remove('is-open');
    closing = setTimeout(closeNow, 190);
  }

  function activate(item) {
    closeNow(); // close synchronously so a page transition does not capture the dialog
    if (item.run) item.run(); else if (item.href) KVA.go(item.href);
  }

  input.addEventListener('input', render);
  input.addEventListener('keydown', function (event) {
    if (event.isComposing) return;
    switch (event.key) {
      case 'ArrowDown': event.preventDefault(); setActive(active + 1, true); break;
      case 'ArrowUp': event.preventDefault(); setActive(active - 1, true); break;
      case 'Home': if (!input.value) { event.preventDefault(); setActive(0, true); } break;
      case 'End': if (!input.value) { event.preventDefault(); setActive(options.length - 1, true); } break;
      case 'Enter': event.preventDefault(); if (options[active]) activate(options[active].item); break;
      default: break;
    }
  });
  dialog.addEventListener('cancel', function (event) { event.preventDefault(); closePalette(); });
  dialog.addEventListener('click', function (event) { if (event.target === dialog) closePalette(); });
  dialog.addEventListener('close', function () { dialog.classList.remove('is-open'); });
  openButton.addEventListener('click', openPalette);

  function isEditable(target) {
    return !!(target && target.closest && target.closest('input, textarea, select, [contenteditable=""], [contenteditable="true"]'));
  }
  doc.addEventListener('keydown', function (event) {
    if ((event.ctrlKey || event.metaKey) && !event.altKey && !event.shiftKey && event.key.toLowerCase() === 'k') {
      event.preventDefault();
      if (isOpen()) closePalette(); else openPalette();
    } else if (event.key === '/' && !event.ctrlKey && !event.metaKey && !event.altKey && !isEditable(event.target) && !dialog.open) {
      event.preventDefault();
      openPalette();
    }
  });

  // ---- Docsify hook ---------------------------------------------------------
  window.$docsify.plugins.push(function (hook, vm) {
    hook.doneEach(function () {
      applyLanguage();
      const heading = doc.querySelector('.markdown-section h1');
      rememberPage(vm.route.path || '/', heading ? heading.textContent.replace(/#$/, '').trim() : '');
    });
  });
  applyLanguage();
}());
