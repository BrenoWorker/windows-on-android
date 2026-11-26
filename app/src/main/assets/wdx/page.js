(function (W) {
  if (W.__wdxPage) return;
  try { Object.defineProperty(W, '__wdxPage', { value: true }); } catch (x) {}
  var CFG = __WDX_CFG__;
  var RUNTIME = __WDX_RUNTIME__;
  var PORT = null, NATIVE = null;
  try { PORT = W.__wdxPort || null; } catch (x) {}
  try { NATIVE = W.__wdxNative || null; } catch (x) {}
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
    W.__wdxIn = deliver;
  }

  var RX = W.XMLHttpRequest;
  var RF = W.fetch ? W.fetch.bind(W) : null;
  var chrome = RUNTIME(CFG, T);
  var X = chrome.__wdx;
  ['chrome', 'browser'].forEach(function (k) {
    try { Object.defineProperty(W, k, { value: chrome, configurable: true, writable: true }); } catch (x) { W[k] = chrome; }
  });
  W.addEventListener('pagehide', function () { X.bye(); });
  W.addEventListener('pageshow', function (e) { if (e.persisted) X.hello(); });

  var toB64 = function (bytes) {
    var s = '';
    for (var i = 0; i < bytes.length; i += 32768) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 32768));
    return btoa(s);
  };
  var fromB64 = function (b) {
    var s = atob(b || '');
    var out = new Uint8Array(s.length);
    for (var i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
    return out;
  };
  var cross = function (u) {
    try {
      var x = new URL(u, location.href);
      return (x.protocol === 'http:' || x.protocol === 'https:') && x.origin !== location.origin;
    } catch (e) {
      return false;
    }
  };
  var proxy = function (d) { return X.call({ o: 'fetch', d: d }); };
  var headersOf = function (list) {
    var h = new Headers();
    (list || []).forEach(function (p) {
      try { h.append(p[0], p[1]); } catch (x) {}
    });
    return h;
  };

  if (RF) {
    W.fetch = function (input, init) {
      var req;
      try { req = new Request(input, init); } catch (e) { return RF(input, init); }
      if (!cross(req.url)) return RF(input, init);
      var hs = {};
      req.headers.forEach(function (v, k) { hs[k] = v; });
      var body = req.method === 'GET' || req.method === 'HEAD' ? Promise.resolve(null) : req.arrayBuffer();
      return body.then(function (buf) {
        return proxy({ url: req.url, method: req.method, headers: hs, body: buf && buf.byteLength ? toB64(new Uint8Array(buf)) : null, cred: req.credentials !== 'omit' });
      }).then(function (r) {
        if (!r || r.error) throw new TypeError('Failed to fetch');
        var empty = [101, 204, 205, 304].indexOf(r.status) >= 0 || req.method === 'HEAD';
        var st = r.status >= 200 && r.status <= 599 ? r.status : 200;
        var resp = new Response(empty ? null : fromB64(r.body), { status: st, statusText: r.statusText || '', headers: headersOf(r.headers) });
        try { Object.defineProperty(resp, 'url', { value: r.url || req.url }); } catch (x) {}
        try { Object.defineProperty(resp, 'redirected', { value: !!r.redirected }); } catch (x) {}
        return resp;
      });
    };
  }

  var EVTS = ['readystatechange', 'loadstart', 'progress', 'abort', 'error', 'load', 'timeout', 'loadend'];
  var FX = function () {
    this._l = {};
    this._h = {};
    this._x = null;
    this._m = 'GET';
    this._u = '';
    this._rh = [];
    this._ab = false;
    this._mime = null;
    this.readyState = 0;
    this.status = 0;
    this.statusText = '';
    this.response = null;
    this.responseText = '';
    this.responseXML = null;
    this.responseURL = '';
    this.responseType = '';
    this.timeout = 0;
    this.withCredentials = false;
    this.upload = { addEventListener: function () {}, removeEventListener: function () {}, dispatchEvent: function () { return true; } };
    var self = this;
    EVTS.forEach(function (t) { self['on' + t] = null; });
  };
  FX.UNSENT = 0;
  FX.OPENED = 1;
  FX.HEADERS_RECEIVED = 2;
  FX.LOADING = 3;
  FX.DONE = 4;
  FX.prototype.UNSENT = 0;
  FX.prototype.OPENED = 1;
  FX.prototype.HEADERS_RECEIVED = 2;
  FX.prototype.LOADING = 3;
  FX.prototype.DONE = 4;
  FX.prototype.addEventListener = function (t, f) {
    if (typeof f !== 'function' && !(f && f.handleEvent)) return;
    (this._l[t] = this._l[t] || []).push(f);
  };
  FX.prototype.removeEventListener = function (t, f) {
    var l = this._l[t];
    if (!l) return;
    var i = l.indexOf(f);
    if (i >= 0) l.splice(i, 1);
  };
  FX.prototype.dispatchEvent = function (e) {
    var t = e.type;
    var self = this;
    var h = this['on' + t];
    if (typeof h === 'function') {
      try { h.call(self, e); } catch (x) { console.error(x); }
    }
    (this._l[t] || []).slice().forEach(function (f) {
      try {
        if (typeof f === 'function') f.call(self, e);
        else f.handleEvent(e);
      } catch (x) {
        console.error(x);
      }
    });
    return true;
  };
  FX.prototype._emit = function (t, loaded, total) {
    var e;
    try {
      e = t === 'readystatechange' ? new Event(t) : new ProgressEvent(t, { lengthComputable: total > 0, loaded: loaded || 0, total: total || 0 });
    } catch (x) {
      e = { type: t };
    }
    try { Object.defineProperty(e, 'target', { value: this }); Object.defineProperty(e, 'currentTarget', { value: this }); } catch (x) {}
    this.dispatchEvent(e);
  };
  FX.prototype._sync = function () {
    var x = this._x;
    this.readyState = x.readyState;
    try { this.status = x.status; this.statusText = x.statusText; } catch (e) {}
    try { this.responseURL = x.responseURL; } catch (e) {}
    try { this.response = x.response; } catch (e) {}
    try { this.responseText = x.responseText; } catch (e) {}
    try { this.responseXML = x.responseXML; } catch (e) {}
  };
  FX.prototype.open = function (method, url, async, user, pass) {
    this._m = String(method || 'GET').toUpperCase();
    this._u = new URL(String(url), location.href).href;
    this._h = {};
    this._ab = false;
    this._x = null;
    if (async === false || !cross(this._u)) {
      var self = this;
      var x = new RX();
      this._x = x;
      EVTS.forEach(function (t) {
        x['on' + t] = function (e) {
          self._sync();
          self._emit(t, e && e.loaded, e && e.total);
        };
      });
      x.open(method, url, async !== false, user, pass);
      this._sync();
      return;
    }
    this.readyState = 1;
    this._emit('readystatechange');
  };
  FX.prototype.setRequestHeader = function (k, v) {
    if (this._x) return this._x.setRequestHeader(k, v);
    var n = String(k).toLowerCase();
    this._h[n] = this._h[n] != null ? this._h[n] + ', ' + v : String(v);
  };
  FX.prototype.overrideMimeType = function (m) {
    if (this._x) return this._x.overrideMimeType(m);
    this._mime = m;
  };
  FX.prototype.getResponseHeader = function (k) {
    if (this._x) return this._x.getResponseHeader(k);
    var n = String(k).toLowerCase(), out = null;
    this._rh.forEach(function (p) {
      if (p[0].toLowerCase() === n) out = out == null ? p[1] : out + ', ' + p[1];
    });
    return out;
  };
  FX.prototype.getAllResponseHeaders = function () {
    if (this._x) return this._x.getAllResponseHeaders();
    return this._rh.map(function (p) { return p[0].toLowerCase() + ': ' + p[1]; }).join('\r\n') + (this._rh.length ? '\r\n' : '');
  };
  FX.prototype.abort = function () {
    if (this._x) return this._x.abort();
    if (this.readyState === 0 || this.readyState === 4) return;
    this._ab = true;
    this.readyState = 4;
    this.status = 0;
    this._emit('readystatechange');
    this._emit('abort');
    this._emit('loadend');
    this.readyState = 0;
  };
  FX.prototype.send = function (body) {
    var self = this;
    if (this._x) {
      var x = this._x;
      try { x.responseType = this.responseType; } catch (e) {}
      try { x.timeout = this.timeout; } catch (e) {}
      try { x.withCredentials = this.withCredentials; } catch (e) {}
      x.send(body);
      this._sync();
      return;
    }
    var init = { method: this._m, headers: this._h };
    if (body != null && this._m !== 'GET' && this._m !== 'HEAD') init.body = body;
    var req;
    try {
      req = new Request(this._u, init);
    } catch (e) {
      setTimeout(function () { self._fail('error'); }, 0);
      return;
    }
    var hs = {};
    req.headers.forEach(function (v, k) { hs[k] = v; });
    var timer = null;
    if (this.timeout > 0) {
      timer = setTimeout(function () {
        if (self.readyState === 4) return;
        self._ab = true;
        self._fail('timeout');
      }, this.timeout);
    }
    this._emit('loadstart');
    var pb = init.body != null ? req.arrayBuffer() : Promise.resolve(null);
    pb.then(function (buf) {
      return proxy({ url: req.url, method: req.method, headers: hs, body: buf && buf.byteLength ? toB64(new Uint8Array(buf)) : null, cred: true });
    }).then(function (r) {
      if (timer) clearTimeout(timer);
      if (self._ab) return;
      if (!r || r.error) return self._fail('error');
      self._finish(r);
    }, function () {
      if (timer) clearTimeout(timer);
      if (!self._ab) self._fail('error');
    });
  };
  FX.prototype._fail = function (kind) {
    this.readyState = 4;
    this.status = 0;
    this.statusText = '';
    this.response = this.responseType === '' || this.responseType === 'text' ? '' : null;
    this.responseText = '';
    this._emit('readystatechange');
    this._emit(kind);
    this._emit('loadend');
  };
  FX.prototype._finish = function (r) {
    var bytes = fromB64(r.body);
    this._rh = r.headers || [];
    this.status = r.status;
    this.statusText = r.statusText || '';
    this.responseURL = r.url || this._u;
    this.readyState = 2;
    this._emit('readystatechange');
    var ct = this._mime || this.getResponseHeader('content-type') || '';
    var cs = /charset=([^;]+)/i.exec(ct);
    var text = '';
    try { text = new TextDecoder(cs ? cs[1].trim() : 'utf-8').decode(bytes); } catch (e) { text = new TextDecoder().decode(bytes); }
    this.readyState = 3;
    this._emit('readystatechange');
    this._emit('progress', bytes.length, bytes.length);
    var t = this.responseType;
    if (t === '' || t === 'text') {
      this.response = text;
      this.responseText = text;
    } else if (t === 'json') {
      try { this.response = JSON.parse(text); } catch (e) { this.response = null; }
    } else if (t === 'arraybuffer') {
      this.response = bytes.buffer;
    } else if (t === 'blob') {
      this.response = new Blob([bytes], { type: ct.split(';')[0] });
    } else if (t === 'document') {
      try { this.response = new DOMParser().parseFromString(text, /xml/i.test(ct) ? 'application/xml' : 'text/html'); } catch (e) { this.response = null; }
      this.responseXML = this.response;
    }
    if ((t === '' || t === 'document') && /xml/i.test(ct) && !this.responseXML) {
      try { this.responseXML = new DOMParser().parseFromString(text, 'application/xml'); } catch (e) {}
    }
    this.readyState = 4;
    this._emit('readystatechange');
    this._emit('load', bytes.length, bytes.length);
    this._emit('loadend', bytes.length, bytes.length);
  };
  W.XMLHttpRequest = FX;

  if (CFG.kind === 'popup') {
    W.close = function () { X.send({ o: 'close' }); };
    var style = document.createElement('style');
    style.textContent = 'html{width:max-content;height:max-content;max-width:800px;max-height:600px;min-width:25px;min-height:25px}';
    (document.head || document.documentElement).appendChild(style);
    var lw = 0, lh = 0;
    var measure = function () {
      var de = document.documentElement;
      if (!de) return;
      var r = de.getBoundingClientRect();
      var w = Math.ceil(Math.max(r.width, document.body ? document.body.scrollWidth : 0));
      var h = Math.ceil(Math.max(r.height, de.scrollHeight));
      if (w === lw && h === lh) return;
      lw = w;
      lh = h;
      X.send({ o: 'size', w: w, h: h });
    };
    var watch = function () {
      measure();
      try {
        var ro = new ResizeObserver(measure);
        ro.observe(document.documentElement);
        if (document.body) ro.observe(document.body);
      } catch (x) {}
    };
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', watch);
    else watch();
    W.addEventListener('load', measure);
    var n = 0;
    var iv = setInterval(function () {
      measure();
      if (++n > 20) clearInterval(iv);
    }, 150);
  }

  if (CFG.worker) {
    var wurl = CFG.worker;
    W.importScripts = function () {
      [].slice.call(arguments).forEach(function (p) {
        var u = new URL(String(p), wurl).href;
        var x = new RX();
        x.open('GET', u, false);
        x.send();
        if (x.status >= 400 || (x.status === 0 && !x.responseText)) throw new Error("Failed to execute 'importScripts': " + u);
        (0, eval)(x.responseText + '\n//# sourceURL=' + u);
      });
    };
    var reg = {
      scope: CFG.base,
      active: { state: 'activated', scriptURL: wurl, postMessage: function () {}, addEventListener: function () {} },
      installing: null,
      waiting: null,
      navigationPreload: { enable: function () { return Promise.resolve(); }, disable: function () { return Promise.resolve(); }, setHeaderValue: function () { return Promise.resolve(); }, getState: function () { return Promise.resolve({ enabled: false }); } },
      update: function () { return Promise.resolve(); },
      unregister: function () { return Promise.resolve(true); },
      showNotification: function (title, o) { chrome.notifications.create({ title: title, message: (o && o.body) || '' }); return Promise.resolve(); },
      getNotifications: function () { return Promise.resolve([]); },
      addEventListener: function () {},
      removeEventListener: function () {}
    };
    W.registration = reg;
    W.serviceWorker = reg.active;
    W.clients = {
      matchAll: function () { return Promise.resolve([]); },
      claim: function () { return Promise.resolve(); },
      get: function () { return Promise.resolve(undefined); },
      openWindow: function (u) { chrome.tabs.create({ url: u }); return Promise.resolve(null); }
    };
    W.skipWaiting = function () { return Promise.resolve(); };
    W.addEventListener('load', function () {
      setTimeout(function () {
        ['install', 'activate'].forEach(function (t) {
          var e = new Event(t);
          e.waitUntil = function () {};
          W.dispatchEvent(e);
        });
      }, 0);
    });
  }
})(window);
