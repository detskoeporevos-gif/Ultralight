/* ============================================================
   EscFR — browser chrome logic
   Talks to the native shell (UI.cpp) through bound JS callbacks.
   ============================================================ */

var EscFRUI = (function () {
  'use strict';

  var DEFAULT_SETTINGS = {
    searchEngine: 'duckduckgo',
    homePage: 'esc://newtab',
    scanlines: true,
    glitch: true,
    compact: false,
    shield: true
  };

  var SEARCH_URLS = {
    duckduckgo: 'https://duckduckgo.com/?q=',
    google: 'https://www.google.com/search?q=',
    bing: 'https://www.bing.com/search?q=',
    yandex: 'https://yandex.com/search/?text='
  };

  var state = {
    tabs: {},          /* id -> { id, title, url, loading } */
    order: [],         /* ids in strip order */
    activeId: null,
    settings: JSON.parse(JSON.stringify(DEFAULT_SETTINGS)),
    menuOpen: false,
    addressFocused: false,
    selectOnMouseUp: false
  };

  function $(sel) { return document.querySelector(sel); }
  function byId(id) { return document.getElementById(id); }

  /* Native callbacks exposed by the shell are host functions: some WebKit
     builds do not give them Function.prototype.apply, so call them directly. */
  function call(name, args) {
    var fn = window[name];
    if (typeof fn !== 'function') return;
    try {
      if (!args || args.length === 0) fn();
      else if (args.length === 1) fn(args[0]);
      else if (args.length === 2) fn(args[0], args[1]);
      else if (args.length === 3) fn(args[0], args[1], args[2]);
      else if (typeof fn.apply === 'function') fn.apply(null, args);
    } catch (e) {
      console.log('native call failed: ' + name + ' — ' + e);
    }
  }

  /* ----------------------------------------------------------
     URL helpers
     ---------------------------------------------------------- */
  function prettyURL(url) {
    if (!url) return '';
    if (url.indexOf('file:///') === 0) {
      var tail = url.substring(8);
      var names = {
        'new_tab_page.html': 'esc://newtab',
        'settings.html': 'esc://settings',
        'about.html': 'esc://about',
        'release_notes.html': 'esc://releasenotes'
      };
      if (names[tail]) return names[tail];
      return 'esc://' + tail;
    }
    return url;
  }

  function isInternal(url) {
    var p = prettyURL(url);
    return p.indexOf('esc://') === 0 || (url || '').indexOf('file:///') === 0;
  }

  function hostOf(url) {
    var m = /^(?:[a-z]+:)?\/\/([^\/:?#]+)/i.exec(url || '');
    if (!m) return '';
    return m[1].replace(/^www\./, '');
  }

  function faviconText(url, title) {
    var host = hostOf(url);
    if (host) return host.charAt(0);
    if (isInternal(url)) return 'e';
    if (title) return title.charAt(0);
    return '·';
  }

  function searchURL(query) {
    var engine = SEARCH_URLS[state.settings.searchEngine] || SEARCH_URLS.duckduckgo;
    return engine + encodeURIComponent(query);
  }

  function normaliseInput(raw) {
    var url = (raw || '').trim();
    if (!url) return '';

    if (/^(esc|file|https?|data|about):/i.test(url)) {
      if (url.toLowerCase() === 'about:home') return 'esc://newtab';
      if (url.toLowerCase() === 'about:settings') return 'esc://settings';
      if (url.toLowerCase().indexOf('file://') === 0 && url.toLowerCase().indexOf('file:///') !== 0)
        return 'file:///' + url.substring(7);
      return url;
    }

    if (window.anchorme && (anchorme.validate.url(url) || anchorme.validate.ip(url)))
      return 'http://' + url;

    if (/^[a-z0-9][a-z0-9\-\.]*\.[a-z]{2,}([\/?#].*)?$/i.test(url))
      return 'http://' + url;

    if (/^localhost(:\d+)?(\/.*)?$/i.test(url)) return 'http://' + url;

    return searchURL(url);
  }

  /* ----------------------------------------------------------
     Tabs
     ---------------------------------------------------------- */
  function renderTab(tab) {
    var el = document.createElement('div');
    el.className = 'tab';
    el.setAttribute('data-tab-id', tab.id);

    var fav = document.createElement('span');
    fav.className = 'fav';
    el.appendChild(fav);

    var title = document.createElement('span');
    title.className = 'title';
    el.appendChild(title);

    var close = document.createElement('span');
    close.className = 'close';
    close.textContent = '×';
    close.title = 'Закрыть вкладку';
    el.appendChild(close);

    byId('tabs').appendChild(el);
    paintTab(el, tab);
    return el;
  }

  function paintTab(el, tab) {
    var internal = isInternal(tab.url);
    el.className = 'tab' + (tab.id === state.activeId ? ' active' : '') + (tab.loading ? ' loading' : '');

    var fav = el.querySelector('.fav');
    fav.className = 'fav' + (internal ? ' internal' : '');
    fav.textContent = faviconText(tab.url, tab.title);

    var title = el.querySelector('.title');
    title.textContent = tab.title || 'Новая вкладка';
    title.title = tab.title || '';

    el.title = tab.title + '\n' + prettyURL(tab.url);
  }

  function activeTab() {
    return state.activeId === null ? null : state.tabs[state.activeId];
  }

  function updateStripMark() {
    var n = state.order.length;
    byId('clock').textContent = (n < 10 ? '0' : '') + n;
  }

  function updateNavigation(tab) {
    if (!tab || tab.id !== state.activeId) return;
    setLoading(!!tab.loading);
    setAddress(prettyURL(tab.url));
    updateShield(tab.url);
  }

  /* Functions called from C++ */
  function addTab(id, title, url, loading) {
    id = String(id);
    if (!state.tabs[id]) state.order.push(id);
    state.tabs[id] = { id: id, title: title || '', url: url || '', loading: !!loading };

    var el = document.querySelector('[data-tab-id="' + id + '"]');
    if (!el) el = renderTab(state.tabs[id]);
    else paintTab(el, state.tabs[id]);

    if (state.activeId === null) setActiveTab(id);
    updateStripMark();
    trimTabs();
    return true;
  }

  function updateTab(id, title, url, loading) {
    id = String(id);
    var tab = state.tabs[id];
    if (!tab) return false;
    if (title) tab.title = title;
    if (typeof url === 'string' && url) tab.url = url;
    tab.loading = !!loading;

    var el = document.querySelector('[data-tab-id="' + id + '"]');
    if (el) paintTab(el, tab);
    updateStripMark();
    updateNavigation(tab);
    return true;
  }

  function updateTabURL(id, url) {
    id = String(id);
    var tab = state.tabs[id];
    if (!tab) return false;
    if (typeof url === 'string' && url) tab.url = url;
    var el = document.querySelector('[data-tab-id="' + id + '"]');
    if (el) paintTab(el, tab);
    updateNavigation(tab);
    return true;
  }

  function closeTab(id) {
    id = String(id);
    var el = document.querySelector('[data-tab-id="' + id + '"]');
    if (el && el.parentNode) el.parentNode.removeChild(el);
    delete state.tabs[id];
    state.order = state.order.filter(function (x) { return x !== id; });
    updateStripMark();
    trimTabs();
    return true;
  }

  function setActiveTab(id) {
    id = String(id);
    var tab = state.tabs[id];
    if (!tab) return false;
    state.activeId = id;

    state.order.forEach(function (other) {
      var el = document.querySelector('[data-tab-id="' + other + '"]');
      if (el) paintTab(el, state.tabs[other]);
    });

    updateNavigation(tab);
    return true;
  }

  function updateLoading(isLoading) {
    setLoading(!!isLoading);
    var tab = activeTab();
    if (tab) {
      tab.loading = !!isLoading;
      var el = document.querySelector('[data-tab-id="' + tab.id + '"]');
      if (el) paintTab(el, tab);
    }
    return true;
  }

  function setLoading(isLoading) {
    byId('progress').className = isLoading ? 'on' : '';
    byId('reload').hidden = isLoading;
    byId('stop').hidden = !isLoading;
  }

  function updateURL(url) {
    setAddress(prettyURL(url));
    updateShield(url);
    var tab = activeTab();
    if (tab) {
      if (typeof url === 'string' && url) tab.url = url;
      var el = document.querySelector('[data-tab-id="' + tab.id + '"]');
      if (el) paintTab(el, tab);
    }
  }

  function setAddress(text) {
    var input = byId('address');
    if (state.addressFocused) return;
    input.value = text;
  }

  function updateShield(url) {
    var wrap = byId('urlwrap');
    var shield = byId('shield');
    var plain = prettyURL(url);
    var insecure = /^http:\/\//i.test(plain) || plain.indexOf('file://') === 0;
    wrap.className = insecure ? 'insecure' : '';
    if (state.addressFocused) wrap.className += ' focused';
    shield.title = insecure ? 'Соединение не защищено' : 'EscFR Protect: соединение защищено';
  }

  function setNavigationState(canBack, canForward) {
    byId('back').disabled = !canBack;
    byId('forward').disabled = !canForward;
    return true;
  }

  function focusAddress(selectAllText) {
    var input = byId('address');
    state.addressFocused = true;
    byId('urlwrap').className = byId('urlwrap').className.indexOf('insecure') >= 0
      ? 'insecure focused' : 'focused';
    input.focus();
    if (selectAllText !== false) input.select();
    return true;
  }

  function blurAddress() {
    state.addressFocused = false;
    byId('address').blur();
    var tab = activeTab();
    updateShield(tab ? tab.url : '');
    call('OnFocusContent');
  }

  function showToast(message, tone) {
    var toast = byId('toast');
    if (!toast) return;
    toast.textContent = message;
    toast.className = 'show' + (tone ? ' ' + tone : '');
    clearTimeout(showToast._timer);
    showToast._timer = setTimeout(function () { toast.className = ''; }, 2600);
  }

  /* Keep tabs from squeezing each other out of the strip. */
  function trimTabs() {
    var strip = byId('tabs');
    var count = state.order.length;
    var available = strip.parentNode.clientWidth - 8 - 158;
    var width = Math.min(232, Math.max(48, Math.floor(available / Math.max(1, count)) - 3));
    state.order.forEach(function (id) {
      var el = document.querySelector('[data-tab-id="' + id + '"]');
      if (el) {
        el.style.width = Math.min(232, width) + 'px';
        el.style.maxWidth = Math.min(232, width) + 'px';
      }
    });
  }

  /* ----------------------------------------------------------
     Settings
     ---------------------------------------------------------- */
  function applySettings(json) {
    var incoming = {};
    try {
      incoming = typeof json === 'string' ? JSON.parse(json) : (json || {});
    } catch (e) {
      incoming = {};
    }
    for (var k in DEFAULT_SETTINGS) {
      if (Object.prototype.hasOwnProperty.call(DEFAULT_SETTINGS, k) &&
          Object.prototype.hasOwnProperty.call(incoming, k))
        state.settings[k] = incoming[k];
    }

    var body = document.body;
    body.className = '';
    if (state.settings.compact) body.className += ' compact';
    if (!state.settings.glitch) body.className += ' no-glitch';
    if (!state.settings.scanlines) body.className += ' no-scan';

    var tab = activeTab();
    if (tab) updateShield(tab.url);
    return true;
  }

  function getSettings() {
    return JSON.stringify(state.settings);
  }

  /* ----------------------------------------------------------
     Menu
     ---------------------------------------------------------- */
  function openMenu() {
    if (state.menuOpen) return closeMenu();
    state.menuOpen = true;
    byId('menu').hidden = false;
    byId('scrim').hidden = false;
    byId('menu').className = 'open';
  }

  function closeMenu() {
    if (!state.menuOpen) return;
    state.menuOpen = false;
    byId('menu').className = '';
    byId('scrim').hidden = true;
    var menu = byId('menu');
    setTimeout(function () {
      if (!state.menuOpen) menu.hidden = true;
    }, 140);
  }

  function runMenuAction(action) {
    closeMenu();
    switch (action) {
      case 'newtab': call('OnRequestNewTab'); break;
      case 'settings': call('OnRequestChangeURL', ['esc://settings']); break;
      case 'about': call('OnRequestChangeURL', ['esc://about']); break;
      case 'keys': call('OnRequestChangeURL', ['esc://about#keys']); break;
      case 'tools': call('OnToggleTools'); break;
      case 'quit': call('OnQuit'); break;
      default: break;
    }
  }

  /* ----------------------------------------------------------
     Wiring
     ---------------------------------------------------------- */
  function bindTabs() {
    var strip = byId('tabs');

    strip.addEventListener('click', function (ev) {
      var tabEl = ev.target.closest ? ev.target.closest('.tab') : null;
      if (!tabEl) return;
      var id = tabEl.getAttribute('data-tab-id');

      if (ev.target.classList.contains('close')) {
        ev.stopPropagation();
        call('OnRequestTabClose', [id]);
        return;
      }
      call('OnActiveTabChange', [id]);
    });

    strip.addEventListener('auxclick', function (ev) {
      var tabEl = ev.target.closest ? ev.target.closest('.tab') : null;
      if (!tabEl || ev.button !== 1) return;
      call('OnRequestTabClose', [tabEl.getAttribute('data-tab-id')]);
    });

    byId('new-tab').addEventListener('click', function () { call('OnRequestNewTab'); });

    window.addEventListener('resize', trimTabs);
  }

  function bindToolbar() {
    function press(id, fn) { byId(id).addEventListener('click', fn); }
    press('back', function () { call('OnBack'); });
    press('forward', function () { call('OnForward'); });
    press('reload', function () { call('OnRefresh'); });
    press('stop', function () { call('OnStop'); });
    press('home', function () { call('OnHome'); });
    press('toggle-tools', function () { call('OnToggleTools'); });
    press('menu-btn', openMenu);

    byId('star').addEventListener('click', function () {
      this.classList.toggle('on');
      showToast(this.classList.contains('on')
        ? 'Закладка добавлена (на время сеанса)'
        : 'Закладка удалена');
    });

    var address = byId('address');

    address.addEventListener('focus', function () {
      state.addressFocused = true;
      byId('urlwrap').className += ' focused';
      address.select();
    });

    address.addEventListener('blur', function () {
      state.addressFocused = false;
      var tab = activeTab();
      updateShield(tab ? tab.url : '');
    });

    address.addEventListener('keydown', function (ev) {
      if (ev.key === 'Escape' || ev.keyCode === 27) {
        address.blur();
        call('OnFocusContent');
        ev.preventDefault();
        return;
      }

      if (ev.key === 'Enter' || ev.keyCode === 13) {
        var url = normaliseInput(address.value);
        address.blur();
        if (url) call('OnRequestChangeURL', [url]);
        ev.preventDefault();
      }
    });

    byId('address').addEventListener('mouseup', function () {
      if (state.selectOnMouseUp) this.select();
      state.selectOnMouseUp = false;
    });
    byId('address').addEventListener('focus', function () { state.selectOnMouseUp = true; });

    byId('scrim').addEventListener('click', closeMenu);

    Array.prototype.forEach.call(document.querySelectorAll('.mi'), function (item) {
      item.addEventListener('click', function () {
        runMenuAction(item.getAttribute('data-act'));
      });
    });

    window.addEventListener('keydown', function (ev) {
      if (ev.key === 'Escape' && state.menuOpen) {
        closeMenu();
        ev.preventDefault();
      }
    });
  }

  /* WebKit occasionally "scrolls" an overflow:hidden document when the
     address bar is focused. The chrome must stay pinned at 0,0. */
  function pinViewport() {
    function reset() {
      var doc = document.documentElement;
      if (doc.scrollLeft !== 0) doc.scrollLeft = 0;
      if (doc.scrollTop !== 0) doc.scrollTop = 0;
      if (document.body.scrollLeft !== 0) document.body.scrollLeft = 0;
      if (document.body.scrollTop !== 0) document.body.scrollTop = 0;
    }
    window.addEventListener('scroll', reset, true);
    document.addEventListener('scroll', reset, true);
    setInterval(reset, 200);
  }

  function init() {
    bindTabs();
    bindToolbar();
    pinViewport();


    var clock = new Date();
    var mark = document.querySelector('.strip-mark');
    if (mark) mark.title = 'Wired link ' + clock.toISOString().replace('T', ' ').substring(0, 16) + ' UTC';

    /* Settings are pushed in by the shell right after DOM ready; ask once
       more in case the shell was faster than us. */
    call('OnRequestSettings');

    setTimeout(function () { trimTabs(); }, 0);
  }

  /* Public surface used by C++ (also mirrored onto `window` so the shell
     can grab them with JSGlobalObject). */
  var api = {
    init: init,
    addTab: addTab,
    updateTab: updateTab,
    updateTabURL: updateTabURL,
    closeTab: closeTab,
    setActiveTab: setActiveTab,
    updateLoading: updateLoading,
    updateURL: updateURL,
    setNavigationState: setNavigationState,
    focusAddress: focusAddress,
    blurAddress: blurAddress,
    applySettings: applySettings,
    showToast: showToast,
    openMenu: openMenu,
    closeMenu: closeMenu
  };

  for (var member in api) {
    if (Object.prototype.hasOwnProperty.call(api, member))
      window[member] = api[member];
  }

  return api;
})();
