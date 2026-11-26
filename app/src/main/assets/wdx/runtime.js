function (cfg, T) {
  var W = typeof window !== 'undefined' ? window : self;
  var ctx = 0, tabId = -1, frameId = 0, queue = [], seq = 0, cbs = {}, ports = {}, lastErr = null, EV = {};
  var hid = '';
  var content = cfg.kind === 'content';
  var base = cfg.base;
  var mf = cfg.manifest || {};

  function report(e) {
    try { console.error(e); } catch (x) {}
  }

  function raw(o) {
    o.k = cfg.token;
    o.e = cfg.id;
    o.c = ctx;
    var s;
    try {
      s = JSON.stringify(o);
    } catch (e) {
      if ('v' in o) o.v = null;
      if ('m' in o) o.m = null;
      try { s = JSON.stringify(o); } catch (x) { return; }
    }
    try { T.post(s); } catch (e) { report(e); }
  }

  function send(o) {
    if (ctx) raw(o);
    else queue.push(o);
  }

  function hello() {
    ctx = 0;
    hid = Math.random().toString(36).slice(2) + Date.now().toString(36);
    var main = true;
    try { main = W.top === W; } catch (e) { main = false; }
    raw({ o: 'hello', hid: hid, kind: cfg.kind, url: String(location.href), main: main });
  }

  function bye() {
    if (ctx) raw({ o: 'bye' });
    ctx = 0;
  }

  function withErr(msg, fn) {
    var prev = lastErr;
    lastErr = msg ? { message: msg } : null;
    try { fn(); } catch (e) { report(e); } finally { lastErr = prev; }
  }

  function call(o, cb, map) {
    return new Promise(function (res, rej) {
      var id = ++seq;
      cbs[id] = function (r) {
        var v = r.v;
        if (map && !r.err) {
          try { v = map(v); } catch (e) { report(e); }
        }
        if (r.err) {
          if (cb) {
            withErr(r.err, function () { cb(v); });
            res(v);
          } else {
            rej(new Error(r.err));
          }
          return;
        }
        if (cb) {
          try { cb(v); } catch (e) { report(e); }
        }
        res(v);
      };
      o.id = id;
      send(o);
    });
  }

  function M(fn) {
    return function () {
      var a = [].slice.call(arguments);
      var cb = typeof a[a.length - 1] === 'function' ? a.pop() : null;
      return fn(a, cb);
    };
  }

  function done(v, cb) {
    return new Promise(function (res) {
      setTimeout(function () {
        if (cb) {
          try { cb(v); } catch (e) { report(e); }
        }
        res(v);
      }, 0);
    });
  }

  function fail(msg, cb) {
    if (cb) {
      setTimeout(function () { withErr(msg, function () { cb(); }); }, 0);
      return Promise.resolve();
    }
    return Promise.reject(new Error(msg));
  }

  function Ev() {
    var l = [];
    return {
      addListener: function (f) { if (typeof f === 'function' && l.indexOf(f) < 0) l.push(f); },
      removeListener: function (f) { var i = l.indexOf(f); if (i >= 0) l.splice(i, 1); },
      hasListener: function (f) { return l.indexOf(f) >= 0; },
      hasListeners: function () { return l.length > 0; },
      addRules: function (r, cb) { if (cb) setTimeout(function () { cb(r || []); }, 0); },
      removeRules: function (i, cb) { if (cb) setTimeout(cb, 0); },
      getRules: function (i, cb) { if (typeof i === 'function') cb = i; if (cb) setTimeout(function () { cb([]); }, 0); },
      _l: l,
      _fire: function (args) {
        l.slice().forEach(function (f) {
          try { f.apply(null, args); } catch (e) { report(e); }
        });
      }
    };
  }

  function ev(name) {
    return EV[name] || (EV[name] = Ev());
  }

  function fire(name, args) {
    ev(name)._fire(args);
    if (name === 'storage.onChanged') ev('storage.' + args[1] + '.onChanged')._fire([args[0]]);
  }

  function stub(methods, events) {
    var o = {};
    Object.keys(methods || {}).forEach(function (k) {
      var v = methods[k];
      o[k] = M(function (a, cb) {
        var r;
        try { r = typeof v === 'function' ? v.apply(null, a) : v; } catch (e) { return fail(String(e && e.message || e), cb); }
        return Promise.resolve(r).then(function (x) {
          if (cb) {
            try { cb(x); } catch (e) { report(e); }
          }
          return x;
        });
      });
    });
    (events || []).forEach(function (n) { o[n] = Ev(); });
    return o;
  }

  function enumOf(list) {
    var o = {};
    list.forEach(function (v) {
      o[String(v).replace(/([a-z])([A-Z])/g, '$1_$2').replace(/[^A-Za-z0-9]+/g, '_').toUpperCase()] = v;
    });
    return o;
  }

  function getURL(p) {
    p = String(p == null ? '' : p);
    if (/^[a-z][a-z0-9+.-]*:/i.test(p)) return p;
    return base + p.replace(/^\/+/, '');
  }

  function getMessage(name, subs) {
    if (name == null) return '';
    var n = String(name).toLowerCase();
    var lang = String(cfg.lang || 'en');
    if (n === '@@extension_id') return cfg.id;
    if (n === '@@ui_locale') return lang.replace('-', '_');
    if (n === '@@bidi_dir') return 'ltr';
    if (n === '@@bidi_reversed_dir') return 'rtl';
    if (n === '@@bidi_start_edge') return 'left';
    if (n === '@@bidi_end_edge') return 'right';
    var m = cfg.messages && cfg.messages[n];
    if (!m) return '';
    if (subs == null) subs = [];
    else if (!Array.isArray(subs)) subs = [subs];
    subs = subs.map(function (s) { return String(s); });
    var ph = {};
    Object.keys(m.placeholders || {}).forEach(function (k) { ph[k.toLowerCase()] = m.placeholders[k]; });
    function sub(s) {
      return s.replace(/\$\$|\$(\d)/g, function (all, d) {
        if (all === '$$') return '$';
        var i = +d - 1;
        return i >= 0 && i < subs.length ? subs[i] : '';
      });
    }
    return String(m.message || '').replace(/\$\$|\$([A-Za-z0-9_@]+)\$|\$(\d)/g, function (all, p, d) {
      if (all === '$$') return '$';
      if (p) {
        var x = ph[p.toLowerCase()];
        return x ? sub(String(x.content == null ? '' : x.content)) : '';
      }
      var i = +d - 1;
      return i >= 0 && i < subs.length ? subs[i] : '';
    });
  }

  function mkPort(id, name, sender) {
    var p = {
      name: name || '',
      sender: sender || undefined,
      onMessage: Ev(),
      onDisconnect: Ev(),
      postMessage: function (m) {
        if (p._dead) throw new Error('Attempting to use a disconnected port object');
        var o = { o: 'pmsg', p: id };
        if (m !== undefined) o.m = m;
        send(o);
      },
      disconnect: function () {
        if (p._dead) return;
        p._dead = 1;
        delete ports[id];
        send({ o: 'pdis', p: id });
      }
    };
    ports[id] = p;
    return p;
  }

  function onMsg(o) {
    var l = ev('runtime.onMessage')._l.slice();
    var fin = false, keep = false;
    function respond(v) {
      if (fin) return;
      fin = true;
      var r = { o: 'reply', r: o.r };
      if (v !== undefined) r.v = v;
      send(r);
    }
    function none() {
      if (fin) return;
      fin = true;
      send({ o: 'reply', r: o.r, n: 1 });
    }
    l.forEach(function (f) {
      try {
        var ret = f(o.m, o.s, respond);
        if (ret === true) {
          keep = true;
        } else if (ret && typeof ret.then === 'function') {
          keep = true;
          ret.then(function (v) {
            if (v !== undefined) respond(v);
            else setTimeout(none, 0);
          }, function (e) {
            if (fin) return;
            fin = true;
            send({ o: 'reply', r: o.r, err: String(e && e.message || e) });
          });
        }
      } catch (e) {
        report(e);
      }
    });
    if (!keep) none();
  }

  function handle(o) {
    var p;
    switch (o.o) {
      case 'cb':
        var f = cbs[o.id];
        if (f) {
          delete cbs[o.id];
          f(o);
        }
        break;
      case 'msg':
        onMsg(o);
        break;
      case 'conn':
        p = mkPort(o.p, o.name, o.s);
        ev('runtime.onConnect')._fire([p]);
        break;
      case 'pmsg':
        p = ports[o.p];
        if (p) p.onMessage._fire([o.m, p]);
        break;
      case 'pdis':
        p = ports[o.p];
        if (p) {
          delete ports[o.p];
          p._dead = 1;
          withErr(o.err, function () { p.onDisconnect._fire([p]); });
        }
        break;
      case 'ev':
        fire(o.n, o.a || []);
        break;
    }
  }

  T.listen(function (s) {
    var o;
    try { o = typeof s === 'string' ? JSON.parse(s) : s; } catch (e) { return; }
    if (!o) return;
    if (o.o === 'hi') {
      if (o.hid !== hid || ctx) return;
      ctx = o.c;
      tabId = o.tab;
      frameId = o.f || 0;
      var q = queue;
      queue = [];
      q.forEach(raw);
      return;
    }
    if (!ctx || o.c !== ctx) return;
    handle(o);
  });

  function Area(name) {
    function keysArg(a) { return a.length && a[0] !== undefined ? a[0] : null; }
    return {
      QUOTA_BYTES: name === 'sync' ? 102400 : 10485760,
      QUOTA_BYTES_PER_ITEM: 8192,
      MAX_ITEMS: 512,
      MAX_WRITE_OPERATIONS_PER_HOUR: 1800,
      MAX_WRITE_OPERATIONS_PER_MINUTE: 120,
      get: M(function (a, cb) { return call({ o: 'st', a: name, x: 'get', d: keysArg(a) }, cb); }),
      getKeys: M(function (a, cb) { return call({ o: 'st', a: name, x: 'get', d: null }, cb, function (v) { return Object.keys(v || {}); }); }),
      set: M(function (a, cb) { return call({ o: 'st', a: name, x: 'set', d: a[0] || {} }, cb, function () { return undefined; }); }),
      remove: M(function (a, cb) { return call({ o: 'st', a: name, x: 'remove', d: a[0] == null ? [] : a[0] }, cb, function () { return undefined; }); }),
      clear: M(function (a, cb) { return call({ o: 'st', a: name, x: 'clear' }, cb, function () { return undefined; }); }),
      getBytesInUse: M(function (a, cb) { return call({ o: 'st', a: name, x: 'bytes', d: keysArg(a) }, cb); }),
      setAccessLevel: M(function (a, cb) { return done(undefined, cb); }),
      onChanged: ev('storage.' + name + '.onChanged')
    };
  }

  var storage = {
    local: Area('local'),
    sync: Area('sync'),
    session: Area('session'),
    managed: Area('managed'),
    onChanged: ev('storage.onChanged'),
    AccessLevel: { TRUSTED_CONTEXTS: 'TRUSTED_CONTEXTS', TRUSTED_AND_UNTRUSTED_CONTEXTS: 'TRUSTED_AND_UNTRUSTED_CONTEXTS' }
  };

  function sendMessage(a, cb) {
    if (a.length > 1 && (a[0] == null || (typeof a[0] === 'string' && /^[a-p]{32}$/.test(a[0])))) {
      var target = a.shift();
      if (target != null && target !== cfg.id) return fail('Could not establish connection. Receiving end does not exist.', cb);
    }
    var o = { o: 'send' };
    if (a[0] !== undefined) o.m = a[0];
    return call(o, cb);
  }

  function connect(a, tab) {
    if (typeof a[0] === 'string' || a[0] === null) a.shift();
    var info = a[0] || {};
    var id = hid + '.' + (++seq);
    var p = mkPort(id, info.name || '');
    var o = { o: 'conn', p: id, name: info.name || '' };
    if (tab != null) {
      o.t = tab;
      if (info.frameId != null) o.f = info.frameId;
    }
    send(o);
    return p;
  }

  var runtime = {
    id: cfg.id,
    getURL: getURL,
    getManifest: function () { return JSON.parse(JSON.stringify(mf)); },
    sendMessage: M(sendMessage),
    connect: M(function (a) { return connect(a, null); }),
    onMessage: ev('runtime.onMessage'),
    onConnect: ev('runtime.onConnect'),
    onMessageExternal: ev('runtime.onMessageExternal'),
    onConnectExternal: ev('runtime.onConnectExternal'),
    OnInstalledReason: { INSTALL: 'install', UPDATE: 'update', CHROME_UPDATE: 'chrome_update', SHARED_MODULE_UPDATE: 'shared_module_update' },
    PlatformOs: { MAC: 'mac', WIN: 'win', ANDROID: 'android', CROS: 'cros', LINUX: 'linux', OPENBSD: 'openbsd', FUCHSIA: 'fuchsia' },
    PlatformArch: { ARM: 'arm', ARM64: 'arm64', X86_32: 'x86-32', X86_64: 'x86-64', MIPS: 'mips', MIPS64: 'mips64' },
    ContextType: { TAB: 'TAB', POPUP: 'POPUP', BACKGROUND: 'BACKGROUND', OFFSCREEN_DOCUMENT: 'OFFSCREEN_DOCUMENT', SIDE_PANEL: 'SIDE_PANEL' }
  };
  Object.defineProperty(runtime, 'lastError', { get: function () { return lastErr || undefined; }, enumerable: true });

  var i18n = {
    getMessage: getMessage,
    getUILanguage: function () { return String(cfg.lang || 'en').replace('_', '-'); },
    getAcceptLanguages: M(function (a, cb) { return done([String(cfg.lang || 'en').replace('_', '-')], cb); }),
    detectLanguage: M(function (a, cb) { return done({ isReliable: false, languages: [] }, cb); })
  };

  var extension = {
    getURL: getURL,
    inIncognitoContext: false,
    sendMessage: runtime.sendMessage,
    sendRequest: runtime.sendMessage,
    onMessage: runtime.onMessage,
    onRequest: ev('extension.onRequest'),
    ViewType: { TAB: 'tab', POPUP: 'popup' }
  };
  Object.defineProperty(extension, 'lastError', { get: function () { return lastErr || undefined; }, enumerable: true });

  var api = {
    runtime: runtime,
    storage: storage,
    i18n: i18n,
    extension: extension,
    dom: {
      openOrClosedShadowRoot: function (e) { return e ? e.openOrClosedShadowRoot || e.shadowRoot || null : null; }
    }
  };

  Object.defineProperty(api, '__wdx', {
    value: {
      call: call,
      send: send,
      bye: bye,
      hello: hello,
      tab: function () { return tabId; },
      frame: function () { return frameId; }
    }
  });

  if (content) {
    hello();
    return api;
  }

  runtime.getPlatformInfo = M(function (a, cb) { return done({ os: 'android', arch: 'arm64', nacl_arch: 'arm' }, cb); });
  runtime.openOptionsPage = M(function (a, cb) { return call({ o: 'rt', x: 'options' }, cb, function () { return undefined; }); });
  runtime.reload = function () { send({ o: 'rt', x: 'reload' }); };
  runtime.setUninstallURL = M(function (a, cb) { return done(undefined, cb); });
  runtime.requestUpdateCheck = M(function (a, cb) { return done({ status: 'no_update' }, cb); });
  runtime.getBackgroundPage = M(function (a, cb) { return done(null, cb); });
  runtime.getContexts = M(function (a, cb) { return done([], cb); });
  runtime.getPackageDirectoryEntry = M(function (a, cb) { return fail('Not supported in WinDroid', cb); });
  runtime.sendNativeMessage = M(function (a, cb) { return fail('Native messaging is not supported in WinDroid', cb); });
  runtime.connectNative = function () { throw new Error('Native messaging is not supported in WinDroid'); };
  runtime.restart = function () {};
  runtime.restartAfterDelay = M(function (a, cb) { return done(undefined, cb); });
  ['onInstalled', 'onStartup', 'onSuspend', 'onSuspendCanceled', 'onUpdateAvailable', 'onRestartRequired', 'onUserScriptMessage', 'onBrowserUpdateAvailable'].forEach(function (n) {
    runtime[n] = ev('runtime.' + n);
  });

  extension.getBackgroundPage = function () { return null; };
  extension.getViews = function () { return [W]; };
  extension.getExtensionTabs = function () { return []; };
  extension.isAllowedIncognitoAccess = M(function (a, cb) { return done(false, cb); });
  extension.isAllowedFileSchemeAccess = M(function (a, cb) { return done(false, cb); });
  extension.setUpdateUrlData = function () {};

  function tabArg(a) {
    if (a.length && (typeof a[0] === 'number' || a[0] === null || a[0] === undefined)) return a.shift();
    return undefined;
  }

  function tabsCall(x, d, cb, map) {
    return call({ o: 'tabs', x: x, d: d || {} }, cb, map);
  }

  function execJs(a, cb, mv2) {
    var t = tabArg(a);
    var d = a[0] || {};
    if (mv2) {
      return call({ o: 'exec', d: { tab: t, kind: d.code != null ? 'code' : 'files', code: d.code, files: d.file ? [d.file] : [], all: !!d.allFrames } }, cb, function (v) {
        return (v || []).map(function (r) { return r.result; });
      });
    }
    var tg = d.target || {};
    var o = { tab: tg.tabId, all: !!tg.allFrames, args: d.args || [] };
    if (typeof d.func === 'function' || typeof d.function === 'function') {
      o.kind = 'func';
      o.func = String(d.func || d.function);
    } else {
      o.kind = 'files';
      o.files = d.files || [];
    }
    return call({ o: 'exec', d: o }, cb);
  }

  function execCss(a, cb, mv2, remove) {
    var t;
    var d;
    if (mv2) {
      t = tabArg(a);
      d = a[0] || {};
      return call({ o: 'exec', d: { tab: t, kind: remove ? 'rcss' : 'css', css: d.code, files: d.file ? [d.file] : [] } }, cb, function () { return undefined; });
    }
    d = a[0] || {};
    t = (d.target || {}).tabId;
    return call({ o: 'exec', d: { tab: t, kind: remove ? 'rcss' : 'css', css: d.css, files: d.files || [] } }, cb, function () { return undefined; });
  }

  var tabs = {
    TAB_ID_NONE: -1,
    TAB_INDEX_NONE: -1,
    MAX_CAPTURE_VISIBLE_TAB_CALLS_PER_SECOND: 2,
    query: M(function (a, cb) { return tabsCall('query', a[0] || {}, cb); }),
    get: M(function (a, cb) { return tabsCall('get', { tab: a[0] }, cb); }),
    getCurrent: M(function (a, cb) { return tabsCall('current', {}, cb); }),
    getSelected: M(function (a, cb) { return tabsCall('query', { active: true }, cb, function (v) { return v && v[0]; }); }),
    getAllInWindow: M(function (a, cb) { return tabsCall('query', {}, cb); }),
    create: M(function (a, cb) { return tabsCall('create', a[0] || {}, cb); }),
    duplicate: M(function (a, cb) { return tabsCall('duplicate', { tab: a[0] }, cb); }),
    update: M(function (a, cb) { var t = tabArg(a); return tabsCall('update', { tab: t, props: a[0] || {} }, cb); }),
    reload: M(function (a, cb) { var t = tabArg(a); return tabsCall('reload', { tab: t }, cb, function () { return undefined; }); }),
    remove: M(function (a, cb) { return tabsCall('remove', { tabs: [].concat(a[0]) }, cb, function () { return undefined; }); }),
    goBack: M(function (a, cb) { var t = tabArg(a); return tabsCall('back', { tab: t }, cb, function () { return undefined; }); }),
    goForward: M(function (a, cb) { var t = tabArg(a); return tabsCall('forward', { tab: t }, cb, function () { return undefined; }); }),
    sendMessage: M(function (a, cb) {
      var opts = a[2] && typeof a[2] === 'object' ? a[2] : {};
      var o = { o: 'tsend', t: a[0] };
      if (a[1] !== undefined) o.m = a[1];
      if (opts.frameId != null) o.f = opts.frameId;
      return call(o, cb);
    }),
    sendRequest: M(function (a, cb) {
      var o = { o: 'tsend', t: a[0] };
      if (a[1] !== undefined) o.m = a[1];
      return call(o, cb);
    }),
    connect: M(function (a) { var t = a.shift(); return connect([null].concat(a), t); }),
    executeScript: M(function (a, cb) { return execJs(a, cb, true); }),
    insertCSS: M(function (a, cb) { return execCss(a, cb, true, false); }),
    removeCSS: M(function (a, cb) { return execCss(a, cb, true, true); }),
    detectLanguage: M(function (a, cb) { return done('und', cb); }),
    captureVisibleTab: M(function (a, cb) { return fail('Capturing tabs is not supported in WinDroid', cb); }),
    getZoom: M(function (a, cb) { return done(1, cb); }),
    setZoom: M(function (a, cb) { return done(undefined, cb); }),
    getZoomSettings: M(function (a, cb) { return done({ mode: 'automatic', scope: 'per-origin', defaultZoomFactor: 1 }, cb); }),
    setZoomSettings: M(function (a, cb) { return done(undefined, cb); }),
    discard: M(function (a, cb) { return done(undefined, cb); }),
    highlight: M(function (a, cb) { return done(undefined, cb); }),
    move: M(function (a, cb) { return done(undefined, cb); }),
    group: M(function (a, cb) { return done(-1, cb); }),
    ungroup: M(function (a, cb) { return done(undefined, cb); }),
    TabStatus: { UNLOADED: 'unloaded', LOADING: 'loading', COMPLETE: 'complete' }
  };
  ['onCreated', 'onUpdated', 'onActivated', 'onRemoved', 'onReplaced', 'onHighlighted', 'onMoved', 'onAttached', 'onDetached', 'onZoomChange', 'onActiveChanged', 'onSelectionChanged', 'onHighlightChanged'].forEach(function (n) {
    tabs[n] = ev('tabs.' + n);
  });

  function win() {
    var w = 1280, h = 720;
    try { w = W.screen.width; h = W.screen.height; } catch (e) {}
    return { id: 1, focused: true, top: 0, left: 0, width: w, height: h, incognito: false, type: 'normal', state: 'maximized', alwaysOnTop: false };
  }

  function winGet(a, cb) {
    var opts = a.length && typeof a[a.length - 1] === 'object' && a[a.length - 1] ? a[a.length - 1] : {};
    var w = win();
    if (!opts.populate) return done(w, cb);
    return tabsCall('query', {}, null).then(function (t) {
      w.tabs = t;
      if (cb) {
        try { cb(w); } catch (e) { report(e); }
      }
      return w;
    });
  }

  var windows = {
    WINDOW_ID_NONE: -1,
    WINDOW_ID_CURRENT: -2,
    get: M(winGet),
    getCurrent: M(winGet),
    getLastFocused: M(winGet),
    getAll: M(function (a, cb) {
      return winGet(a, null).then(function (w) {
        if (cb) {
          try { cb([w]); } catch (e) { report(e); }
        }
        return [w];
      });
    }),
    create: M(function (a, cb) {
      var d = a[0] || {};
      [].concat(d.url || []).forEach(function (u) { tabsCall('create', { url: u }, null); });
      return done(win(), cb);
    }),
    update: M(function (a, cb) { return done(win(), cb); }),
    remove: M(function (a, cb) { return done(undefined, cb); }),
    WindowType: { NORMAL: 'normal', POPUP: 'popup', PANEL: 'panel', APP: 'app', DEVTOOLS: 'devtools' },
    WindowState: { NORMAL: 'normal', MINIMIZED: 'minimized', MAXIMIZED: 'maximized', FULLSCREEN: 'fullscreen', LOCKED_FULLSCREEN: 'locked-fullscreen' }
  };
  ['onCreated', 'onRemoved', 'onFocusChanged', 'onBoundsChanged'].forEach(function (n) { windows[n] = ev('windows.' + n); });

  var scripting = {
    executeScript: M(function (a, cb) { return execJs(a, cb, false); }),
    insertCSS: M(function (a, cb) { return execCss(a, cb, false, false); }),
    removeCSS: M(function (a, cb) { return execCss(a, cb, false, true); }),
    registerContentScripts: M(function (a, cb) { return done(undefined, cb); }),
    unregisterContentScripts: M(function (a, cb) { return done(undefined, cb); }),
    updateContentScripts: M(function (a, cb) { return done(undefined, cb); }),
    getRegisteredContentScripts: M(function (a, cb) { return done([], cb); }),
    ExecutionWorld: { ISOLATED: 'ISOLATED', MAIN: 'MAIN' },
    StyleOrigin: { AUTHOR: 'AUTHOR', USER: 'USER' }
  };

  function color(c) {
    if (Array.isArray(c)) return 'rgba(' + c[0] + ',' + c[1] + ',' + c[2] + ',' + ((c[3] == null ? 255 : c[3]) / 255) + ')';
    return String(c || '');
  }

  function actCall(x, d, cb, map) {
    return call({ o: 'act', x: x, d: d || {} }, cb, map || function (v) { return v; });
  }

  var action = {
    setBadgeText: M(function (a, cb) { var d = a[0] || {}; return actCall('badge', { text: d.text == null ? '' : String(d.text), tab: d.tabId }, cb, function () { return undefined; }); }),
    getBadgeText: M(function (a, cb) { return actCall('getBadge', a[0], cb); }),
    setBadgeBackgroundColor: M(function (a, cb) { var d = a[0] || {}; return actCall('color', { color: color(d.color) }, cb, function () { return undefined; }); }),
    getBadgeBackgroundColor: M(function (a, cb) { return actCall('getColor', a[0], cb); }),
    setBadgeTextColor: M(function (a, cb) { return done(undefined, cb); }),
    getBadgeTextColor: M(function (a, cb) { return done([255, 255, 255, 255], cb); }),
    setTitle: M(function (a, cb) { var d = a[0] || {}; return actCall('title', { title: d.title == null ? '' : String(d.title) }, cb, function () { return undefined; }); }),
    getTitle: M(function (a, cb) { return actCall('getTitle', a[0], cb); }),
    setIcon: M(function (a, cb) { var d = a[0] || {}; return actCall('icon', { path: d.path || null }, cb, function () { return undefined; }); }),
    setPopup: M(function (a, cb) { var d = a[0] || {}; return actCall('popup', { popup: d.popup == null ? '' : String(d.popup) }, cb, function () { return undefined; }); }),
    getPopup: M(function (a, cb) { return actCall('getPopup', a[0], cb); }),
    enable: M(function (a, cb) { return actCall('enable', {}, cb, function () { return undefined; }); }),
    disable: M(function (a, cb) { return actCall('disable', {}, cb, function () { return undefined; }); }),
    isEnabled: M(function (a, cb) { return actCall('isEnabled', {}, cb); }),
    openPopup: M(function (a, cb) { return actCall('open', {}, cb, function () { return undefined; }); }),
    getUserSettings: M(function (a, cb) { return done({ isOnToolbar: true }, cb); }),
    show: M(function (a, cb) { return done(undefined, cb); }),
    hide: M(function (a, cb) { return done(undefined, cb); }),
    onClicked: ev('action.onClicked'),
    onUserSettingsChanged: ev('action.onUserSettingsChanged')
  };

  var alarms = {
    create: M(function (a, cb) {
      var name = typeof a[0] === 'string' ? a.shift() : '';
      return call({ o: 'al', x: 'create', d: { name: name, info: a[0] || {} } }, cb, function () { return undefined; });
    }),
    get: M(function (a, cb) { return call({ o: 'al', x: 'get', d: { name: typeof a[0] === 'string' ? a[0] : '' } }, cb); }),
    getAll: M(function (a, cb) { return call({ o: 'al', x: 'getAll', d: {} }, cb); }),
    clear: M(function (a, cb) { return call({ o: 'al', x: 'clear', d: { name: typeof a[0] === 'string' ? a[0] : '' } }, cb); }),
    clearAll: M(function (a, cb) { return call({ o: 'al', x: 'clearAll', d: {} }, cb); }),
    onAlarm: ev('alarms.onAlarm')
  };

  function hostLike(p) { return p === '<all_urls>' || /^[a-z*]+:\/\//.test(p); }
  var permissions = {
    contains: M(function (a, cb) { return done(true, cb); }),
    request: M(function (a, cb) { return done(true, cb); }),
    remove: M(function (a, cb) { return done(false, cb); }),
    getAll: M(function (a, cb) {
      var p = (mf.permissions || []).filter(function (x) { return typeof x === 'string'; });
      return done({
        permissions: p.filter(function (x) { return !hostLike(x); }),
        origins: (mf.host_permissions || []).concat(p.filter(hostLike))
      }, cb);
    }),
    addHostAccessRequest: M(function (a, cb) { return done(undefined, cb); }),
    removeHostAccessRequest: M(function (a, cb) { return done(undefined, cb); }),
    onAdded: ev('permissions.onAdded'),
    onRemoved: ev('permissions.onRemoved')
  };

  var menuSeq = 0;
  var contextMenus = {
    create: function (props, cb) {
      var id = props && props.id != null ? props.id : ++menuSeq;
      if (typeof cb === 'function') setTimeout(cb, 0);
      return id;
    },
    update: M(function (a, cb) { return done(undefined, cb); }),
    remove: M(function (a, cb) { return done(undefined, cb); }),
    removeAll: M(function (a, cb) { return done(undefined, cb); }),
    refresh: function () {},
    ACTION_MENU_TOP_LEVEL_LIMIT: 6,
    ContextType: enumOf(['all', 'page', 'frame', 'selection', 'link', 'editable', 'image', 'video', 'audio', 'launcher', 'browser_action', 'page_action', 'action']),
    ItemType: enumOf(['normal', 'checkbox', 'radio', 'separator']),
    onClicked: ev('contextMenus.onClicked'),
    onShown: ev('contextMenus.onShown'),
    onHidden: ev('contextMenus.onHidden')
  };

  var notifySeq = 0;
  var notifications = {
    create: M(function (a, cb) {
      var id = typeof a[0] === 'string' ? a.shift() : '';
      if (!id) id = 'wdx-' + (++notifySeq);
      return call({ o: 'notify', d: { id: id, opts: a[0] || {} } }, cb, function () { return id; });
    }),
    update: M(function (a, cb) { return done(true, cb); }),
    clear: M(function (a, cb) { return done(true, cb); }),
    getAll: M(function (a, cb) { return done({}, cb); }),
    getPermissionLevel: M(function (a, cb) { return done('granted', cb); }),
    TemplateType: enumOf(['basic', 'image', 'list', 'progress']),
    PermissionLevel: enumOf(['granted', 'denied'])
  };
  ['onClosed', 'onClicked', 'onButtonClicked', 'onPermissionLevelChanged', 'onShowSettings'].forEach(function (n) { notifications[n] = ev('notifications.' + n); });

  var commands = {
    getAll: M(function (a, cb) {
      var c = mf.commands || {};
      return done(Object.keys(c).map(function (k) { return { name: k, description: c[k].description || '', shortcut: '' }; }), cb);
    }),
    onCommand: ev('commands.onCommand')
  };

  var webNavigation = {
    getFrame: M(function (a, cb) {
      var d = a[0] || {};
      return tabsCall('get', { tab: d.tabId }, cb, function (t) {
        return t && (!d.frameId) ? { url: t.url, frameId: 0, parentFrameId: -1, errorOccurred: false, documentLifecycle: 'active', frameType: 'outermost_frame' } : null;
      });
    }),
    getAllFrames: M(function (a, cb) {
      var d = a[0] || {};
      return tabsCall('get', { tab: d.tabId }, cb, function (t) {
        return t ? [{ url: t.url, frameId: 0, parentFrameId: -1, errorOccurred: false, processId: 1, documentLifecycle: 'active', frameType: 'outermost_frame' }] : null;
      });
    })
  };
  ['onBeforeNavigate', 'onCommitted', 'onDOMContentLoaded', 'onCompleted', 'onErrorOccurred', 'onCreatedNavigationTarget', 'onReferenceFragmentUpdated', 'onTabReplaced', 'onHistoryStateUpdated'].forEach(function (n) {
    webNavigation[n] = ev('webNavigation.' + n);
  });

  var webRequest = stub({ handlerBehaviorChanged: undefined }, ['onBeforeRequest', 'onBeforeSendHeaders', 'onSendHeaders', 'onHeadersReceived', 'onAuthRequired', 'onResponseStarted', 'onBeforeRedirect', 'onCompleted', 'onErrorOccurred', 'onActionIgnored']);
  webRequest.MAX_HANDLER_BEHAVIOR_CHANGED_CALLS_PER_10_MINUTES = 20;
  webRequest.ResourceType = enumOf(['main_frame', 'sub_frame', 'stylesheet', 'script', 'image', 'font', 'object', 'xmlhttprequest', 'ping', 'csp_report', 'media', 'websocket', 'webbundle', 'other']);
  webRequest.OnBeforeRequestOptions = enumOf(['blocking', 'requestBody', 'extraHeaders']);
  webRequest.OnBeforeSendHeadersOptions = enumOf(['requestHeaders', 'blocking', 'extraHeaders']);
  webRequest.OnSendHeadersOptions = enumOf(['requestHeaders', 'extraHeaders']);
  webRequest.OnHeadersReceivedOptions = enumOf(['blocking', 'responseHeaders', 'extraHeaders']);
  webRequest.OnResponseStartedOptions = enumOf(['responseHeaders', 'extraHeaders']);
  webRequest.OnCompletedOptions = enumOf(['responseHeaders', 'extraHeaders']);

  var declarativeNetRequest = stub({
    updateDynamicRules: undefined,
    updateSessionRules: undefined,
    getDynamicRules: function () { return []; },
    getSessionRules: function () { return []; },
    updateEnabledRulesets: undefined,
    getEnabledRulesets: function () { return []; },
    updateStaticRules: undefined,
    getDisabledRuleIds: function () { return []; },
    isRegexSupported: function () { return { isSupported: true }; },
    getAvailableStaticRuleCount: 30000,
    setExtensionActionOptions: undefined,
    testMatchOutcome: function () { return { matchedRules: [] }; },
    getMatchedRules: function () { return { rulesMatchedInfo: [] }; }
  }, ['onRuleMatchedDebug']);
  declarativeNetRequest.MAX_NUMBER_OF_DYNAMIC_RULES = 30000;
  declarativeNetRequest.MAX_NUMBER_OF_SESSION_RULES = 5000;
  declarativeNetRequest.MAX_NUMBER_OF_UNSAFE_DYNAMIC_RULES = 5000;
  declarativeNetRequest.MAX_NUMBER_OF_UNSAFE_SESSION_RULES = 5000;
  declarativeNetRequest.MAX_NUMBER_OF_REGEX_RULES = 1000;
  declarativeNetRequest.MAX_NUMBER_OF_STATIC_RULESETS = 100;
  declarativeNetRequest.MAX_NUMBER_OF_ENABLED_STATIC_RULESETS = 50;
  declarativeNetRequest.GUARANTEED_MINIMUM_STATIC_RULES = 30000;
  declarativeNetRequest.DYNAMIC_RULESET_ID = '_dynamic';
  declarativeNetRequest.SESSION_RULESET_ID = '_session';
  declarativeNetRequest.RuleActionType = enumOf(['block', 'redirect', 'allow', 'upgradeScheme', 'modifyHeaders', 'allowAllRequests']);
  declarativeNetRequest.ResourceType = webRequest.ResourceType;
  declarativeNetRequest.HeaderOperation = enumOf(['append', 'set', 'remove']);
  declarativeNetRequest.DomainType = enumOf(['firstParty', 'thirdParty']);
  declarativeNetRequest.RequestMethod = enumOf(['connect', 'delete', 'get', 'head', 'options', 'patch', 'post', 'put', 'other']);
  declarativeNetRequest.UnsupportedRegexReason = enumOf(['syntaxError', 'memoryLimitExceeded']);

  function cookieCall(x, d, cb) { return call({ o: 'cookies', x: x, d: d || {} }, cb); }
  var cookies = {
    get: M(function (a, cb) { return cookieCall('get', a[0], cb); }),
    getAll: M(function (a, cb) { return cookieCall('getAll', a[0], cb); }),
    set: M(function (a, cb) { return cookieCall('set', a[0], cb); }),
    remove: M(function (a, cb) { return cookieCall('remove', a[0], cb); }),
    getAllCookieStores: M(function (a, cb) { return cookieCall('stores', {}, cb); }),
    getPartitionKey: M(function (a, cb) { return done({ partitionKey: {} }, cb); }),
    onChanged: ev('cookies.onChanged'),
    SameSiteStatus: enumOf(['no_restriction', 'lax', 'strict', 'unspecified']),
    OnChangedCause: enumOf(['evicted', 'expired', 'explicit', 'expired_overwrite', 'overwrite'])
  };

  var dlSeq = 0;
  var downloads = stub({
    search: function () { return []; },
    pause: undefined,
    resume: undefined,
    cancel: undefined,
    erase: function () { return []; },
    removeFile: undefined,
    acceptDanger: undefined,
    getFileIcon: '',
    setUiOptions: undefined
  }, ['onCreated', 'onChanged', 'onErased', 'onDeterminingFilename']);
  downloads.download = M(function (a, cb) {
    var d = a[0] || {};
    var id = ++dlSeq;
    return call({ o: 'dl', d: { url: d.url, filename: d.filename || '' } }, cb, function () { return id; });
  });
  downloads.open = function () {};
  downloads.show = function () {};
  downloads.showDefaultFolder = function () {};
  downloads.setShelfEnabled = function () {};

  var offscreen = {
    createDocument: M(function (a, cb) { var d = a[0] || {}; return call({ o: 'off', x: 'create', d: { url: d.url } }, cb, function () { return undefined; }); }),
    closeDocument: M(function (a, cb) { return call({ o: 'off', x: 'close', d: {} }, cb, function () { return undefined; }); }),
    hasDocument: M(function (a, cb) { return call({ o: 'off', x: 'has', d: {} }, cb); }),
    Reason: enumOf(['TESTING', 'AUDIO_PLAYBACK', 'IFRAME_SCRIPTING', 'DOM_SCRAPING', 'BLOBS', 'DOM_PARSER', 'USER_MEDIA', 'DISPLAY_MEDIA', 'WEB_RTC', 'CLIPBOARD', 'LOCAL_STORAGE', 'WORKERS', 'BATTERY_STATUS', 'MATCH_MEDIA', 'GEOLOCATION'])
  };

  function self_() {
    return {
      id: cfg.id,
      name: getMessageName(mf.name),
      shortName: getMessageName(mf.short_name || mf.name),
      description: getMessageName(mf.description || ''),
      version: mf.version || '',
      versionName: mf.version_name || mf.version || '',
      mayDisable: true,
      enabled: true,
      type: 'extension',
      installType: 'normal',
      offlineEnabled: false,
      optionsUrl: mf.options_ui && mf.options_ui.page ? getURL(mf.options_ui.page) : mf.options_page ? getURL(mf.options_page) : '',
      permissions: (mf.permissions || []).filter(function (x) { return typeof x === 'string' && !hostLike(x); }),
      hostPermissions: mf.host_permissions || [],
      homepageUrl: mf.homepage_url || '',
      icons: [],
      isApp: false
    };
  }

  function getMessageName(s) {
    s = String(s || '');
    var m = /^__MSG_(.+)__$/.exec(s);
    return m ? getMessage(m[1]) : s;
  }

  var management = stub({
    getSelf: self_,
    get: self_,
    getAll: function () { return [self_()]; },
    getPermissionWarningsById: function () { return []; },
    getPermissionWarningsByManifest: function () { return []; },
    setEnabled: undefined,
    uninstallSelf: undefined,
    uninstall: undefined,
    launchApp: undefined,
    createAppShortcut: undefined
  }, ['onInstalled', 'onUninstalled', 'onEnabled', 'onDisabled']);

  var identity = stub({
    getAuthToken: function () { throw new Error('Signing in with a Google account is not supported in WinDroid'); },
    launchWebAuthFlow: function () { throw new Error('Web auth flows are not supported in WinDroid'); },
    removeCachedAuthToken: undefined,
    clearAllCachedAuthTokens: undefined,
    getProfileUserInfo: function () { return { email: '', id: '' }; },
    getAccounts: function () { return []; }
  }, ['onSignInChanged']);
  identity.getRedirectURL = function (p) { return 'https://' + cfg.id + '.chromiumapp.org/' + (p || ''); };

  var history = stub({ search: function () { return []; }, getVisits: function () { return []; }, addUrl: undefined, deleteUrl: undefined, deleteRange: undefined, deleteAll: undefined }, ['onVisited', 'onVisitRemoved']);
  var bookmarks = stub({
    get: function () { return []; },
    getChildren: function () { return []; },
    getRecent: function () { return []; },
    getTree: function () { return [{ id: '0', title: '', children: [{ id: '1', parentId: '0', title: 'Bookmarks bar', children: [] }, { id: '2', parentId: '0', title: 'Other bookmarks', children: [] }] }]; },
    getSubTree: function () { return []; },
    search: function () { return []; },
    create: function (d) { return { id: String(Date.now()), parentId: (d && d.parentId) || '1', title: (d && d.title) || '', url: d && d.url }; },
    move: undefined,
    update: undefined,
    remove: undefined,
    removeTree: undefined
  }, ['onCreated', 'onRemoved', 'onChanged', 'onMoved', 'onChildrenReordered', 'onImportBegan', 'onImportEnded']);
  var idle = stub({ queryState: 'active', setDetectionInterval: undefined, getAutoLockDelay: 0 }, ['onStateChanged']);
  idle.IdleState = enumOf(['active', 'idle', 'locked']);
  var sidePanel = stub({ setOptions: undefined, getOptions: function () { return { enabled: false }; }, setPanelBehavior: undefined, getPanelBehavior: function () { return { openPanelOnActionClick: false }; }, open: undefined, close: undefined }, []);
  var tts = stub({ speak: undefined, stop: undefined, pause: undefined, resume: undefined, isSpeaking: false, getVoices: function () { return []; } }, ['onVoicesChanged']);
  var topSites = stub({ get: function () { return []; } }, []);
  var sessions = stub({ getRecentlyClosed: function () { return []; }, getDevices: function () { return []; }, restore: undefined }, ['onChanged']);
  sessions.MAX_SESSION_RESULTS = 25;
  var fontSettings = stub({ getFontList: function () { return []; }, getFont: function () { return { fontId: '' }; }, setFont: undefined, clearFont: undefined }, ['onFontChanged']);

  api.tabs = tabs;
  api.windows = windows;
  api.scripting = scripting;
  api.action = action;
  api.browserAction = action;
  api.pageAction = action;
  api.alarms = alarms;
  api.permissions = permissions;
  api.contextMenus = contextMenus;
  api.menus = contextMenus;
  api.notifications = notifications;
  api.commands = commands;
  api.webNavigation = webNavigation;
  api.webRequest = webRequest;
  api.declarativeNetRequest = declarativeNetRequest;
  api.cookies = cookies;
  api.downloads = downloads;
  api.offscreen = offscreen;
  api.management = management;
  api.identity = identity;
  api.history = history;
  api.bookmarks = bookmarks;
  api.idle = idle;
  api.sidePanel = sidePanel;
  api.tts = tts;
  api.topSites = topSites;
  api.sessions = sessions;
  api.fontSettings = fontSettings;

  hello();
  return api;
}
