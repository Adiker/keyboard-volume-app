/* Keyboard Volume App documentation: Docsify configuration and UI behaviour. */
(function () {
  'use strict';

  const REPO_URL = 'https://github.com/Adiker/keyboard-volume-app';
  const EDIT_BASE = REPO_URL + '/edit/main/';

  const STRINGS = {
    en: {
      theme: 'Theme', light: 'Light', dark: 'Dark', oled: 'OLED',
      toc: 'On this page', menu: 'Open navigation', menuClose: 'Close navigation',
      search: 'Search', edit: 'Edit this page', prev: 'Previous', next: 'Next',
      copy: 'Copy', copied: 'Copied', copyFailed: 'Failed', copyLabel: 'Copy code',
      switchTo: 'PL', switchTitle: 'Polski'
    },
    pl: {
      theme: 'Motyw', light: 'Jasny', dark: 'Ciemny', oled: 'OLED',
      toc: 'Na tej stronie', menu: 'Otwórz nawigację', menuClose: 'Zamknij nawigację',
      search: 'Szukaj', edit: 'Edytuj tę stronę', prev: 'Poprzednia', next: 'Następna',
      copy: 'Kopiuj', copied: 'Skopiowano', copyFailed: 'Błąd', copyLabel: 'Kopiuj kod',
      switchTo: 'EN', switchTitle: 'English'
    }
  };

  const doc = document;
  const body = doc.body;
  const menuButton = doc.querySelector('.mobile-menu');
  const backdrop = doc.querySelector('.nav-backdrop');
  const themeSelect = doc.getElementById('theme-select');
  const langSwitch = doc.getElementById('lang-switch');
  const mobileQuery = matchMedia('(max-width: 768px)');
  let currentLang = 'en';
  let appliedGroupLang = null;

  // ---- Theme -------------------------------------------------------------
  themeSelect.value = doc.documentElement.dataset.theme;
  themeSelect.addEventListener('change', function () {
    doc.documentElement.dataset.theme = themeSelect.value;
    try { localStorage.setItem('kva-docs-theme', themeSelect.value); } catch (_) { /* Optional persistence. */ }
  });

  // ---- Mobile navigation -------------------------------------------------
  function syncSidebarInert() {
    const sidebar = doc.querySelector('.sidebar');
    if (!sidebar) return;
    // While the off-canvas sidebar is hidden it must not be reachable by keyboard.
    sidebar.inert = mobileQuery.matches && !body.classList.contains('mobile-nav-open');
  }

  function setNavOpen(open, options) {
    body.classList.toggle('mobile-nav-open', open);
    menuButton.setAttribute('aria-expanded', String(open));
    menuButton.setAttribute('aria-label', STRINGS[currentLang][open ? 'menuClose' : 'menu']);
    syncSidebarInert();
    if (open) {
      const target = doc.querySelector('.sidebar input, .sidebar a');
      if (target) target.focus();
    } else if (options && options.restoreFocus) {
      menuButton.focus();
    }
  }

  menuButton.addEventListener('click', function () {
    setNavOpen(!body.classList.contains('mobile-nav-open'), { restoreFocus: true });
  });
  backdrop.addEventListener('click', function () { setNavOpen(false, { restoreFocus: true }); });
  doc.addEventListener('keydown', function (event) {
    if (event.key === 'Escape' && body.classList.contains('mobile-nav-open')) {
      setNavOpen(false, { restoreFocus: true });
    }
  });
  mobileQuery.addEventListener('change', function () {
    if (!mobileQuery.matches) setNavOpen(false);
    syncSidebarInert();
  });

  // ---- Language ----------------------------------------------------------
  function stripMd(path) { return path.replace(/\.md$/, ''); }

  function detectLanguage(path) {
    const p = stripMd(path);
    return p === '/README.pl' || p.indexOf('/docs/pl/') === 0 ? 'pl' : 'en';
  }

  // Route of the page in the other language (home page when there is no translation).
  function counterpartRoute(path) {
    const p = stripMd(path);
    if (p === '/README.pl') return '/';
    if (p.indexOf('/docs/pl/') === 0) return '/docs/en/' + p.slice('/docs/pl/'.length) + '.md';
    if (p.indexOf('/docs/en/') === 0) return '/docs/pl/' + p.slice('/docs/en/'.length) + '.md';
    return '/README.pl.md';
  }

  function applyLanguage(path) {
    currentLang = detectLanguage(path);
    const t = STRINGS[currentLang];
    doc.documentElement.lang = currentLang;
    langSwitch.textContent = t.switchTo;
    langSwitch.title = t.switchTitle;
    langSwitch.hreflang = currentLang === 'pl' ? 'en' : 'pl';
    langSwitch.lang = langSwitch.hreflang;
    langSwitch.setAttribute('aria-label', t.switchTitle);
    langSwitch.href = '#' + counterpartRoute(path);
    doc.querySelector('label[for="theme-select"]').textContent = t.theme;
    themeSelect.setAttribute('aria-label', t.theme);
    ['light', 'dark', 'oled'].forEach(function (name) {
      themeSelect.querySelector('option[value="' + name + '"]').textContent = t[name];
    });
    doc.querySelector('.toc-heading').textContent = t.toc;
    doc.querySelector('.page-toc').setAttribute('aria-label', t.toc);
    menuButton.setAttribute('aria-label', t[body.classList.contains('mobile-nav-open') ? 'menuClose' : 'menu']);
    const search = doc.querySelector('.sidebar .search input[type="search"]');
    if (search) search.placeholder = t.search;
  }

  // ---- Sidebar groups ----------------------------------------------------
  // Docsify re-renders the sidebar on route changes, which drops our buttons and
  // classes. Collapsed state is therefore kept here (by group index) and re-applied.
  let groupOverride = {};

  function setupSidebarGroups() {
    const groups = doc.querySelectorAll('.sidebar-nav > ul > li');
    groups.forEach(function (group, index) {
      if (group.querySelector(':scope > .nav-group-toggle')) return;
      const label = Array.prototype.find.call(group.childNodes, function (node) {
        return node.nodeType === Node.TEXT_NODE && node.textContent.trim();
      });
      if (!label) return;
      const toggle = doc.createElement('button');
      toggle.type = 'button';
      toggle.className = 'nav-group-toggle';
      toggle.textContent = label.textContent.trim();
      group.replaceChild(toggle, label);
      toggle.addEventListener('click', function () {
        const collapsed = group.classList.toggle('collapsed');
        groupOverride[index] = collapsed;
        toggle.setAttribute('aria-expanded', String(!collapsed));
      });
    });
    // A language change forgets manual choices; by default the other language is collapsed.
    if (appliedGroupLang !== currentLang) {
      appliedGroupLang = currentLang;
      groupOverride = {};
    }
    groups.forEach(function (group, index) {
      const toggle = group.querySelector(':scope > .nav-group-toggle');
      if (!toggle) return;
      let collapsed;
      if (Object.prototype.hasOwnProperty.call(groupOverride, index)) {
        collapsed = groupOverride[index];
      } else {
        const polish = !!group.querySelector('a[href*="/docs/pl/"], a[href*="README.pl"]');
        const english = !!group.querySelector('a[href*="/docs/en/"]');
        const other = (polish && currentLang === 'en') || (english && currentLang === 'pl');
        collapsed = other && !group.querySelector('li.active');
      }
      group.classList.toggle('collapsed', collapsed);
      toggle.setAttribute('aria-expanded', String(!collapsed));
    });
  }

  // ---- Code blocks: toolbar with copy button -----------------------------
  function copyText(text) {
    if (navigator.clipboard && window.isSecureContext) return navigator.clipboard.writeText(text);
    return new Promise(function (resolve, reject) {
      const area = doc.createElement('textarea');
      area.value = text;
      area.setAttribute('readonly', '');
      area.style.position = 'fixed';
      area.style.opacity = '0';
      doc.body.appendChild(area);
      area.select();
      const ok = doc.execCommand && doc.execCommand('copy');
      doc.body.removeChild(area);
      ok ? resolve() : reject(new Error('copy failed'));
    });
  }

  function enhanceCodeBlocks() {
    const t = STRINGS[currentLang];
    doc.querySelectorAll('.markdown-section pre').forEach(function (pre) {
      const code = pre.querySelector(':scope > code');
      if (!code || pre.querySelector(':scope > .code-toolbar')) return;
      const toolbar = doc.createElement('div');
      toolbar.className = 'code-toolbar';
      const lang = doc.createElement('span');
      lang.className = 'code-lang';
      lang.textContent = pre.getAttribute('data-lang') || '';
      const button = doc.createElement('button');
      button.type = 'button';
      button.className = 'copy-btn';
      button.textContent = t.copy;
      button.setAttribute('aria-label', t.copyLabel);
      let timer = 0;
      button.addEventListener('click', function () {
        copyText(code.textContent).then(function () { return t.copied; }, function () { return t.copyFailed; })
          .then(function (label) {
            button.textContent = label;
            button.classList.toggle('copied', label === t.copied);
            clearTimeout(timer);
            timer = setTimeout(function () {
              button.textContent = t.copy;
              button.classList.remove('copied');
            }, 1600);
          });
      });
      toolbar.append(lang, button);
      pre.insertBefore(toolbar, code);
    });
  }

  // ---- Page footer: edit link and previous/next --------------------------
  function buildPager(footer) {
    const active = doc.querySelector('.sidebar-nav li.active');
    if (!active || !active.parentElement) return;
    const siblings = Array.prototype.map.call(active.parentElement.children, function (li) {
      return li.querySelector(':scope > a');
    }).filter(function (link) {
      return link && !/^[a-z][a-z0-9+.-]*:/i.test(link.getAttribute('href') || '');
    });
    const index = siblings.indexOf(active.querySelector(':scope > a'));
    if (index < 0) return;
    const t = STRINGS[currentLang];
    const pager = doc.createElement('nav');
    pager.className = 'pager';
    pager.setAttribute('aria-label', t.prev + ' / ' + t.next);
    [['prev', index - 1, t.prev, '← '], ['next', index + 1, t.next, '']].forEach(function (entry) {
      const link = siblings[entry[1]];
      if (!link) return;
      const a = doc.createElement('a');
      a.className = entry[0];
      a.rel = entry[0];
      a.href = link.getAttribute('href');
      const small = doc.createElement('small');
      small.textContent = entry[3] + entry[2] + (entry[0] === 'next' ? ' →' : '');
      a.append(small, doc.createTextNode(link.textContent.trim()));
      pager.appendChild(a);
    });
    if (pager.childElementCount) footer.appendChild(pager);
  }

  function buildPageFooter(routeFile) {
    const file = (routeFile || '').replace(/^\/+/, '');
    const section = doc.querySelector('.markdown-section');
    if (!section) return;
    const old = section.querySelector('.page-footer');
    if (old) old.remove();
    const footer = doc.createElement('footer');
    footer.className = 'page-footer';
    if (file && file !== 'LICENSE.md' && file !== '_404.md') {
      const edit = doc.createElement('a');
      edit.className = 'edit-link';
      edit.href = EDIT_BASE + file;
      edit.rel = 'noopener';
      edit.textContent = STRINGS[currentLang].edit + ' ↗';
      footer.appendChild(edit);
    }
    buildPager(footer);
    if (footer.childElementCount) section.appendChild(footer);
  }

  // ---- Page TOC ----------------------------------------------------------
  function buildPageToc() {
    const toc = doc.querySelector('.page-toc');
    const nav = toc.querySelector('nav');
    nav.replaceChildren();
    doc.querySelectorAll('.markdown-section h2, .markdown-section h3').forEach(function (heading) {
      const anchor = heading.querySelector('a.anchor');
      if (!anchor) return;
      const link = doc.createElement('a');
      link.href = anchor.getAttribute('href');
      link.textContent = heading.textContent.trim();
      if (heading.tagName === 'H3') link.className = 'toc-subheading';
      nav.appendChild(link);
    });
    toc.hidden = !nav.childElementCount;
  }

  // ---- Shared API for effects.js and palette.js ---------------------------
  window.KVA = {
    lang: function () { return currentLang; },
    counterpartRoute: counterpartRoute,
    // effects.js wraps this to run a view transition before the route changes.
    go: function (hash) { location.hash = hash; },
    theme: function () { return themeSelect.value; },
    setTheme: function (name) {
      themeSelect.value = name;
      themeSelect.dispatchEvent(new Event('change'));
    }
  };

  // ---- Docsify ------------------------------------------------------------
  window.$docsify = {
    name: '',
    loadSidebar: true,
    subMaxLevel: 0,
    auto2top: true,
    // Docsify scrolls section links itself, so CSS scroll margins are not enough.
    topMargin: doc.querySelector('.site-header').offsetHeight + 16,
    relativePath: true,
    notFoundPage: true,
    plugins: [function (hook, vm) {
      // GitHub-style "page.md#anchor" links must become Docsify's "page.md?id=anchor".
      hook.beforeEach(function (markdown) {
        return markdown.replace(/\]\(((?![a-z][a-z0-9+.-]*:)[^)\s#]+\.md)#([^)\s]+)\)/gi, ']($1?id=$2)');
      });
      hook.doneEach(function () {
        applyLanguage(vm.route.path || '/');
        const sidebar = doc.querySelector('.sidebar');
        if (sidebar) sidebar.id = 'docs-navigation';
        setupSidebarGroups();
        setNavOpen(false);
        enhanceCodeBlocks();
        buildPageFooter(vm.route.file);
        buildPageToc();
      });
    }],
    search: {
      paths: 'auto',
      placeholder: 'Search',
      noData: 'No results / Brak wyników',
      depth: 3
    }
  };
}());
