(function (W) {
  var EXT = __WDX_EXT__;
  var TOKENS = __WDX_TOKENS__;
  var host = '';
  try { host = String(location.hostname || ''); } catch (x) {}
  if (/\.ext\.windroid$/.test(host)) return;
  if (host === 'chromewebstore.google.com') return;
  var href = '';
  try { href = String(location.href); } catch (x) {}
  if (/^https:\/\/chrome\.google\.com\/webstore/.test(href)) return;

  var R = W.__wdxReg;
  if (!R) {
    var PORT = null, NATIVE = null;
    try { PORT = W.__wdxPort || null; } catch (x) {}
    try { NATIVE = W.__wdxNative || null; } catch (x) {}
    if (PORT) {
      try { delete W.__wdxPort; } catch (x) {}
    }
    var sinks = [];
    var deliver = function (s) {
      for (var i = 0; i < sinks.length; i++) {
        try { sinks[i](s); } catch (x) {}
      }
    };
    var T = {
      post: function (s) {
        if (PORT) PORT.postMessage(s);
        else if (NATIVE) NATIVE.post(s);
      },
      listen: function (f) { sinks.push(f); }
    };
    if (PORT) {
      try {
        PORT.addEventListener('message', function (e) { deliver(e.data); });
      } catch (x) {
        PORT.onmessage = function (e) { deliver(e.data); };
      }
    } else {
      try { Object.defineProperty(W, '__wdxIn', { value: deliver }); } catch (x) {}
    }
    var entries = {};
    var stack = [];
    var push = function (a) {
      var s = {};
      ['chrome', 'browser'].forEach(function (k) {
        s[k] = Object.getOwnPropertyDescriptor(W, k);
        try { Object.defineProperty(W, k, { value: a, configurable: true, writable: true }); } catch (x) {}
      });
      stack.push(s);
    };
    var pop = function () {
      var s = stack.pop();
      if (!s) return;
      ['chrome', 'browser'].forEach(function (k) {
        try {
          if (s[k]) Object.defineProperty(W, k, s[k]);
          else delete W[k];
        } catch (x) {}
      });
    };
    var known = {};
    Object.keys(TOKENS).forEach(function (k) { known[k] = TOKENS[k]; });
    var ok = function (id, tok) { return !!id && known[id] === tok; };
    R = Object.freeze({
      has: function (id) { return !!entries[id]; },
      extend: function (next) {
        var trusted = Object.keys(next || {}).some(function (k) { return known[k] && known[k] === next[k]; });
        if (!trusted) return;
        Object.keys(next).forEach(function (k) { if (!known[k]) known[k] = next[k]; });
      },
      t: function (id, tok) { return ok(id, tok) ? T : null; },
      add: function (id, tok, e) { if (ok(id, tok)) entries[id] = e; },
      push: push,
      pop: pop,
      run: function (tok, id, xid, f) {
        var e = entries[id];
        if (!e || !ok(id, tok)) return '__wdx_none';
        var a = e.api();
        var reply = function (v, err) {
          var o = { o: 'xres', x: xid };
          if (v !== undefined) o.v = v;
          if (err) o.err = err;
          a.__wdx.send(o);
        };
        var r;
        push(a);
        try {
          r = f.call(W, a, a);
        } catch (err) {
          pop();
          reply(undefined, String(err && err.message || err));
          return '__wdx_async';
        }
        pop();
        if (r && typeof r.then === 'function') {
          r.then(function (v) { reply(v); }, function (err) { reply(undefined, String(err && err.message || err)); });
        } else {
          reply(r);
        }
        return '__wdx_async';
      },
      enter: function (tok, id) {
        var e = entries[id];
        if (!e || !ok(id, tok)) return false;
        push(e.api());
        return true;
      }
    });
    try {
      Object.defineProperty(W, '__wdxReg', { value: R });
    } catch (x) {
      return;
    }
    W.addEventListener('pagehide', function () {
      Object.keys(entries).forEach(function (k) { entries[k].bye(); });
    });
    W.addEventListener('pageshow', function (e) {
      if (!e.persisted) return;
      Object.keys(entries).forEach(function (k) { entries[k].hello(); });
    });
  } else {
    R.extend(TOKENS);
  }

  if (R.has(EXT.id)) return;
  var transport = R.t(EXT.id, EXT.token);
  if (!transport) return;
  var RUNTIME = __WDX_RUNTIME__;
  var FN = __WDX_SCRIPTS__;
  var apiObj = null;
  var api = function () {
    if (!apiObj) {
      apiObj = RUNTIME({
        id: EXT.id,
        token: EXT.token,
        base: EXT.base,
        kind: 'content',
        messages: EXT.messages,
        manifest: EXT.manifest,
        lang: EXT.lang
      }, transport);
    }
    return apiObj;
  };
  R.add(EXT.id, EXT.token, {
    api: api,
    bye: function () { if (apiObj) apiObj.__wdx.bye(); },
    hello: function () { if (apiObj) apiObj.__wdx.hello(); }
  });

  var esc = function (s) { return s.replace(/[.+?^${}()|[\]\\\/]/g, '\\$&'); };
  var pat = function (p) {
    if (p === '<all_urls>') return /^(https?|wss?|file|ftp):\/\//;
    var m = /^(\*|https?|wss?|file|ftp|urn):\/\/([^\/]*)(\/.*)?$/.exec(p);
    if (!m) return null;
    var sc = m[1] === '*' ? '(https?|wss?)' : esc(m[1]);
    var h = m[2], hr;
    if (h === '*') hr = '[^/]*';
    else if (h.indexOf('*.') === 0) hr = '([^/]*\\.)?' + esc(h.slice(2)) + '(:\\d+)?';
    else hr = esc(h) + (h.indexOf(':') >= 0 ? '' : '(:\\d+)?');
    var path = (m[3] || '/').split('*').map(esc).join('.*');
    return new RegExp('^' + sc + ':\\/\\/' + hr + path + '$');
  };
  var glob = function (g) {
    return new RegExp('^' + String(g).split('').map(function (c) {
      return c === '*' ? '.*' : c === '?' ? '.' : esc(c);
    }).join('') + '$');
  };
  var any = function (list, conv, u) {
    for (var i = 0; i < list.length; i++) {
      var r = null;
      try { r = conv(list[i]); } catch (x) {}
      if (r && r.test(u)) return true;
    }
    return false;
  };
  var test = function (cs, u) {
    if (!any(cs.m || [], pat, u)) return false;
    if (any(cs.x || [], pat, u)) return false;
    if (cs.ig && cs.ig.length && !any(cs.ig, glob, u)) return false;
    if (cs.xg && any(cs.xg, glob, u)) return false;
    return true;
  };
  var css = function (text) {
    try {
      var s = new CSSStyleSheet();
      s.replaceSync(text);
      document.adoptedStyleSheets = document.adoptedStyleSheets.concat([s]);
      return;
    } catch (x) {}
    var add = function () {
      var root = document.head || document.documentElement;
      if (!root) return setTimeout(add, 10);
      var st = document.createElement('style');
      st.textContent = text;
      root.appendChild(st);
    };
    add();
  };
  var at = function (r, f) {
    if (r === 'document_start') return f();
    var rs = document.readyState;
    if (r === 'document_end') {
      if (rs === 'loading') document.addEventListener('DOMContentLoaded', f, { once: true });
      else f();
      return;
    }
    if (rs === 'loading') document.addEventListener('DOMContentLoaded', function () { setTimeout(f, 0); }, { once: true });
    else setTimeout(f, 0);
  };
  var run = function (f) {
    var a = api();
    R.push(a);
    try {
      f.call(W, a, a);
    } catch (e) {
      try { console.error(e); } catch (x) {}
    } finally {
      R.pop();
    }
  };

  var top = true;
  try { top = W.top === W; } catch (x) { top = false; }
  var u = href.split('#')[0];
  (EXT.cs || []).forEach(function (cs) {
    if (!cs.af && !top) return;
    var target = u;
    if (/^(about:|data:|blob:)/.test(u)) {
      if (!cs.ab) return;
      try { target = String(W.parent.location.href).split('#')[0]; } catch (x) { return; }
    }
    if (!test(cs, target)) return;
    if (cs.css) css(cs.css);
    if (cs.f >= 0 && FN[cs.f]) at(cs.r, function () { run(FN[cs.f]); });
  });
})(window);
