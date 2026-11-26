package com.brenoworker.windroid;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.TextUtils;
import android.util.Base64;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.CookieManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.PopupWindow;
import android.widget.Toast;

import androidx.webkit.JavaScriptReplyProxy;
import androidx.webkit.ScriptHandler;
import androidx.webkit.WebViewCompat;
import androidx.webkit.WebViewFeature;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.json.JSONTokener;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.ProtocolException;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TimeZone;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

final class Extensions {

    interface Host {
        void download(String url, String name);
    }

    static final String SUFFIX = ".ext.windroid";
    private static final String NEW_TAB = "https://www.google.com/";
    private static final int K_CONTENT = 0;
    private static final int K_PAGE = 1;
    private static final int K_BG = 2;
    private static final int K_POPUP = 3;
    private static final int K_OFF = 4;
    private static final Set<String> ALL = Collections.singleton("*");
    private static final Pattern ID = Pattern.compile("^[a-p]{32}$");
    private static final Pattern CWS = Pattern.compile(
            "^https://(?:chromewebstore\\.google\\.com/detail/|chrome\\.google\\.com/webstore/detail/)(?:[^/?#]+/)?([a-p]{32})(?:[/?#].*)?$");
    private static final Pattern MSG = Pattern.compile("^__MSG_(.+)__$");
    private static final Pattern HEAD = Pattern.compile("<head(\\s[^>]*)?>", Pattern.CASE_INSENSITIVE);
    private static final Pattern HTML = Pattern.compile("<html(\\s[^>]*)?>", Pattern.CASE_INSENSITIVE);
    private static final Pattern DOCTYPE = Pattern.compile("^\\s*<!doctype[^>]*>", Pattern.CASE_INSENSITIVE);
    private static final long MAX_CRX = 256L * 1024 * 1024;

    private final class Cs {
        JSONArray matches = new JSONArray();
        JSONArray excludes = new JSONArray();
        JSONArray includeGlobs;
        JSONArray excludeGlobs;
        String js = "";
        String css = "";
        String runAt = "document_idle";
        boolean allFrames;
        boolean aboutBlank;
    }

    private final class Alarm {
        String name;
        long when;
        double period;
        Runnable run;
    }

    private final class Ext {
        String id;
        File dir;
        JSONObject manifest;
        int mv;
        String name = "";
        String version = "";
        String description = "";
        volatile boolean enabled = true;
        final String token = randomToken();
        JSONObject messages = new JSONObject();
        String popup = "";
        String options = "";
        String title = "";
        String iconPath;
        volatile String iconOverride;
        volatile int iconStamp = 1;
        String bgWorker;
        boolean bgModule;
        List<String> bgScripts = new ArrayList<>();
        String bgPage;
        final List<Cs> scripts = new ArrayList<>();
        volatile String badge = "";
        volatile String badgeColor = "#D93025";
        boolean actionEnabled = true;
        JSONObject local;
        JSONObject sync;
        JSONObject session = new JSONObject();
        WebView bg;
        boolean bgLoaded;
        WebView off;
        String boot;
        final List<Object[]> startEvents = new ArrayList<>();
        final Map<String, Alarm> alarms = new HashMap<>();

        String base() {
            return "https://" + id + SUFFIX + "/";
        }

        String host() {
            return id + SUFFIX;
        }

        boolean hasBackground() {
            return bgWorker != null || !bgScripts.isEmpty() || bgPage != null;
        }
    }

    private final class Tab {
        final int id;
        final WebView web;
        volatile String url = "";
        volatile String title = "";
        volatile boolean loading;
        final List<ScriptHandler> handlers = new ArrayList<>();

        Tab(int id, WebView web) {
            this.id = id;
            this.web = web;
        }
    }

    private final class Ctx {
        int id;
        Ext ext;
        int kind;
        int tab = -1;
        boolean main = true;
        String url = "";
        JavaScriptReplyProxy proxy;
        WebView web;
        long born;
        boolean ready = true;
        final List<String> pending = new ArrayList<>();
    }

    private final class Route {
        String id;
        Ctx from;
        int cb;
        final Set<Integer> wait = new HashSet<>();
        Runnable timer;
    }

    private final class PortRec {
        Ctx opener;
        final Set<Ctx> receivers = new LinkedHashSet<>();
    }

    private final class Exec {
        Ctx ctx;
        int cb;
    }

    private final class Staged {
        String id;
        File dir;
        String name;
    }

    private final Activity act;
    private final Host host;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private final ExecutorService net = Executors.newCachedThreadPool();
    private final File root;
    private final File data;
    private final SharedPreferences prefs;
    private final Map<String, Ext> exts = new ConcurrentHashMap<>();
    private final List<String> order = new CopyOnWriteArrayList<>();
    private final Map<Integer, Tab> tabs = new ConcurrentHashMap<>();
    private final Map<Integer, Ctx> ctxs = new HashMap<>();
    private final Map<String, Route> routes = new HashMap<>();
    private final Map<String, PortRec> ports = new HashMap<>();
    private final Map<String, Exec> execs = new HashMap<>();
    private final ConcurrentLinkedQueue<String> opens = new ConcurrentLinkedQueue<>();
    private final Set<Integer> closeRequests = ConcurrentHashMap.newKeySet();
    private final float density;
    private String userAgent = "";
    private String runtimeJs = "";
    private String contentTpl = "";
    private String pageTpl = "";
    private boolean docStart;
    private boolean listener;
    private int nextCtx = 1;
    private int routeSeq;
    private int execSeq;
    private volatile int activeTab = -1;
    private volatile String state = "idle";
    private volatile boolean busy;
    private Staged staged;
    private String pendingJob;
    private PopupWindow popup;
    private WebView popupWeb;
    private int popupRight;
    private int popupTop;

    Extensions(Activity act, Host host) {
        this.act = act;
        this.host = host;
        root = new File(act.getFilesDir(), "extensions");
        data = new File(act.getFilesDir(), "extension-data");
        root.mkdirs();
        data.mkdirs();
        prefs = act.getSharedPreferences("windroid_extensions", Context.MODE_PRIVATE);
        density = act.getResources().getDisplayMetrics().density;
        try {
            docStart = WebViewFeature.isFeatureSupported(WebViewFeature.DOCUMENT_START_SCRIPT);
        } catch (Throwable t) {
            docStart = false;
        }
        try {
            listener = WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER);
        } catch (Throwable t) {
            listener = false;
        }
        try {
            userAgent = WebSettings.getDefaultUserAgent(act);
        } catch (Throwable t) {
            userAgent = "Mozilla/5.0 (Linux; Android) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0 Mobile Safari/537.36";
        }
        runtimeJs = asset("wdx/runtime.js");
        contentTpl = asset("wdx/content.js");
        pageTpl = asset("wdx/page.js");
        io.execute(this::loadAll);
    }

    private String asset(String path) {
        try (InputStream in = act.getAssets().open(path)) {
            return new String(readAll(in, 8L * 1024 * 1024), StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "";
        }
    }

    private static String randomToken() {
        byte[] b = new byte[24];
        new SecureRandom().nextBytes(b);
        StringBuilder sb = new StringBuilder();
        for (byte x : b) sb.append(String.format(Locale.ROOT, "%02x", x));
        return sb.toString();
    }

    private void loadAll() {
        List<String> saved = new ArrayList<>();
        for (String s : prefs.getString("order", "").split(",")) if (ID.matcher(s).matches()) saved.add(s);
        File[] dirs = root.listFiles();
        List<String> found = new ArrayList<>();
        if (dirs != null) {
            for (File d : dirs) {
                if (d.isDirectory() && ID.matcher(d.getName()).matches()) found.add(d.getName());
                else if (d.isDirectory() && d.getName().startsWith(".staging-")) deleteTree(d);
            }
        }
        List<String> ids = new ArrayList<>();
        for (String s : saved) if (found.contains(s)) ids.add(s);
        Collections.sort(found);
        for (String s : found) if (!ids.contains(s)) ids.add(s);
        final List<Ext> loaded = new ArrayList<>();
        for (String id : ids) {
            try {
                Ext e = load(id, new File(root, id));
                e.enabled = prefs.getBoolean("enabled." + id, true);
                loaded.add(e);
            } catch (Exception ignored) {
                continue;
            }
        }
        ui.post(() -> {
            for (Ext e : loaded) {
                exts.put(e.id, e);
                if (!order.contains(e.id)) order.add(e.id);
            }
            rebuild();
            ui.postDelayed(() -> {
                for (Ext e : loaded) {
                    if (!e.enabled || exts.get(e.id) != e) continue;
                    e.startEvents.add(new Object[]{"runtime.onStartup", new JSONArray()});
                    startBackground(e);
                }
            }, 1500);
        });
    }

    private Ext load(String id, File dir) throws IOException, JSONException {
        JSONObject mf = readManifest(dir);
        Ext e = new Ext();
        e.id = id;
        e.dir = dir;
        e.manifest = mf;
        e.mv = mf.optInt("manifest_version", 2);
        e.messages = loadMessages(dir, mf.optString("default_locale", ""));
        e.name = msg(e, mf.optString("name", id));
        e.version = mf.optString("version", "");
        e.description = msg(e, mf.optString("description", ""));
        JSONObject action = mf.optJSONObject(e.mv >= 3 ? "action" : "browser_action");
        if (action == null) action = mf.optJSONObject("browser_action");
        if (action == null) action = mf.optJSONObject("page_action");
        if (action != null) {
            e.popup = stripSlash(action.optString("default_popup", ""));
            e.title = msg(e, action.optString("default_title", ""));
        }
        e.iconPath = pickIcon(action != null ? action.opt("default_icon") : null, mf.optJSONObject("icons"));
        JSONObject bg = mf.optJSONObject("background");
        if (bg != null) {
            if (bg.has("service_worker")) {
                e.bgWorker = stripSlash(bg.optString("service_worker"));
                e.bgModule = "module".equals(bg.optString("type"));
            }
            JSONArray sc = bg.optJSONArray("scripts");
            if (sc != null && e.bgWorker == null) for (int i = 0; i < sc.length(); i++) e.bgScripts.add(stripSlash(sc.optString(i)));
            if (bg.has("page") && e.bgWorker == null && e.bgScripts.isEmpty()) e.bgPage = stripSlash(bg.optString("page"));
        }
        JSONObject ou = mf.optJSONObject("options_ui");
        e.options = stripSlash(ou != null ? ou.optString("page", "") : mf.optString("options_page", ""));
        JSONArray cs = mf.optJSONArray("content_scripts");
        if (cs != null) {
            for (int i = 0; i < cs.length(); i++) {
                JSONObject c = cs.optJSONObject(i);
                if (c == null) continue;
                Cs s = new Cs();
                s.matches = c.optJSONArray("matches") != null ? c.optJSONArray("matches") : new JSONArray();
                s.excludes = c.optJSONArray("exclude_matches") != null ? c.optJSONArray("exclude_matches") : new JSONArray();
                s.includeGlobs = c.optJSONArray("include_globs");
                s.excludeGlobs = c.optJSONArray("exclude_globs");
                s.runAt = c.optString("run_at", "document_idle");
                s.allFrames = c.optBoolean("all_frames", false);
                s.aboutBlank = c.optBoolean("match_about_blank", false) || c.optBoolean("match_origin_as_fallback", false);
                s.js = readFiles(e, c.optJSONArray("js"), "\n;\n");
                s.css = readFiles(e, c.optJSONArray("css"), "\n")
                        .replace("chrome-extension://__MSG_@@extension_id__/", e.base())
                        .replace("__MSG_@@extension_id__", e.id);
                e.scripts.add(s);
            }
        }
        e.local = readJson(dataFile(id, "local"));
        e.sync = readJson(dataFile(id, "sync"));
        return e;
    }

    private static String stripSlash(String s) {
        if (s == null) return "";
        while (s.startsWith("/")) s = s.substring(1);
        return s;
    }

    private JSONObject readManifest(File dir) throws IOException, JSONException {
        File f = new File(dir, "manifest.json");
        if (!f.isFile()) throw new IOException("The package has no manifest.json");
        Object v = new JSONTokener(stripComments(readText(f))).nextValue();
        if (!(v instanceof JSONObject)) throw new JSONException("manifest.json is not an object");
        return (JSONObject) v;
    }

    private static String stripComments(String s) {
        if (s.startsWith("\uFEFF")) s = s.substring(1);
        StringBuilder out = new StringBuilder(s.length());
        boolean str = false;
        int i = 0;
        while (i < s.length()) {
            char c = s.charAt(i);
            if (str) {
                out.append(c);
                if (c == '\\' && i + 1 < s.length()) {
                    out.append(s.charAt(i + 1));
                    i += 2;
                    continue;
                }
                if (c == '"') str = false;
                i++;
                continue;
            }
            if (c == '"') {
                str = true;
                out.append(c);
                i++;
            } else if (c == '/' && i + 1 < s.length() && s.charAt(i + 1) == '/') {
                while (i < s.length() && s.charAt(i) != '\n') i++;
            } else if (c == '/' && i + 1 < s.length() && s.charAt(i + 1) == '*') {
                int end = s.indexOf("*/", i + 2);
                i = end < 0 ? s.length() : end + 2;
            } else {
                out.append(c);
                i++;
            }
        }
        return out.toString();
    }

    private JSONObject loadMessages(File dir, String def) {
        JSONObject out = new JSONObject();
        Locale l = Locale.getDefault();
        List<String> locs = new ArrayList<>();
        if (!def.isEmpty()) locs.add(def);
        locs.add(l.getLanguage());
        if (!l.getCountry().isEmpty()) locs.add(l.getLanguage() + "_" + l.getCountry());
        for (String loc : locs) {
            File f = new File(dir, "_locales/" + loc + "/messages.json");
            if (!f.isFile()) continue;
            try {
                Object v = new JSONTokener(stripComments(readText(f))).nextValue();
                if (!(v instanceof JSONObject)) continue;
                JSONObject m = (JSONObject) v;
                Iterator<String> it = m.keys();
                while (it.hasNext()) {
                    String k = it.next();
                    out.put(k.toLowerCase(Locale.ROOT), m.get(k));
                }
            } catch (Exception ignored) {
                continue;
            }
        }
        return out;
    }

    private static String msg(Ext e, String s) {
        if (s == null) return "";
        Matcher m = MSG.matcher(s);
        if (!m.matches()) return s;
        JSONObject o = e.messages.optJSONObject(m.group(1).toLowerCase(Locale.ROOT));
        return o == null ? s : o.optString("message", s);
    }

    private static String pickIcon(Object action, JSONObject icons) {
        if (action instanceof String) return stripSlash((String) action);
        JSONObject set = action instanceof JSONObject ? (JSONObject) action : icons;
        if (set == null) return null;
        String best = null;
        int bestSize = -1;
        Iterator<String> it = set.keys();
        while (it.hasNext()) {
            String k = it.next();
            int size;
            try {
                size = Integer.parseInt(k);
            } catch (NumberFormatException ex) {
                continue;
            }
            boolean better;
            if (bestSize < 0) better = true;
            else if (bestSize >= 48) better = size >= 48 && size < bestSize;
            else better = size > bestSize;
            if (better) {
                bestSize = size;
                best = set.optString(k, null);
            }
        }
        if (best == null && action instanceof JSONObject && icons != null) return pickIcon(null, icons);
        return best == null ? null : stripSlash(best);
    }

    private String readFiles(Ext e, JSONArray list, String sep) {
        if (list == null) return "";
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < list.length(); i++) {
            File f = resolveFile(e, list.optString(i));
            if (f == null || !f.isFile()) continue;
            try {
                if (sb.length() > 0) sb.append(sep);
                sb.append(readText(f));
            } catch (IOException ignored) {
                continue;
            }
        }
        return sb.toString();
    }

    private static File resolveFile(Ext e, String path) {
        if (path == null) return null;
        try {
            String p = stripSlash(path);
            int q = p.indexOf('?');
            if (q >= 0) p = p.substring(0, q);
            int h = p.indexOf('#');
            if (h >= 0) p = p.substring(0, h);
            File base = e.dir.getCanonicalFile();
            File f = new File(base, p).getCanonicalFile();
            if (!f.getPath().startsWith(base.getPath() + File.separator)) return null;
            return f;
        } catch (IOException ex) {
            return null;
        }
    }

    private File dataFile(String id, String area) {
        return new File(data, id + "." + area + ".json");
    }

    private static JSONObject readJson(File f) {
        if (!f.isFile()) return new JSONObject();
        try {
            return new JSONObject(readText(f));
        } catch (Exception e) {
            return new JSONObject();
        }
    }

    private static String readText(File f) throws IOException {
        try (InputStream in = new FileInputStream(f)) {
            return new String(readAll(in, 64L * 1024 * 1024), StandardCharsets.UTF_8);
        }
    }

    private static byte[] readAll(InputStream in, long limit) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[65536];
        long total = 0;
        int n;
        while ((n = in.read(buf)) > 0) {
            total += n;
            if (total > limit) throw new IOException("The file is too large");
            out.write(buf, 0, n);
        }
        return out.toByteArray();
    }

    private void writeLater(final File f, final String text) {
        io.execute(() -> {
            File tmp = new File(f.getPath() + ".tmp");
            try (OutputStream os = new FileOutputStream(tmp)) {
                os.write(text.getBytes(StandardCharsets.UTF_8));
            } catch (IOException e) {
                return;
            }
            if (!tmp.renameTo(f)) tmp.delete();
        });
    }

    private static void deleteTree(File f) {
        if (f == null || !f.exists()) return;
        File[] kids = f.isDirectory() ? f.listFiles() : null;
        if (kids != null) for (File k : kids) deleteTree(k);
        f.delete();
    }

    private static String fill(String tpl, String... kv) {
        StringBuilder out = new StringBuilder(tpl.length() + 4096);
        int i = 0;
        while (i < tpl.length()) {
            int best = -1;
            int bk = -1;
            for (int k = 0; k < kv.length; k += 2) {
                int p = tpl.indexOf(kv[k], i);
                if (p >= 0 && (best < 0 || p < best)) {
                    best = p;
                    bk = k;
                }
            }
            if (best < 0) {
                out.append(tpl, i, tpl.length());
                break;
            }
            out.append(tpl, i, best).append(kv[bk + 1]);
            i = best + kv[bk].length();
        }
        return out.toString();
    }

    private String lang() {
        return Locale.getDefault().toLanguageTag();
    }

    private void rebuild() {
        JSONObject tokens = new JSONObject();
        for (String id : order) {
            Ext e = exts.get(id);
            if (e == null || !e.enabled) continue;
            try {
                tokens.put(e.id, e.token);
            } catch (JSONException ignored) {
                continue;
            }
        }
        String tok = tokens.toString();
        for (String id : order) {
            Ext e = exts.get(id);
            if (e == null) continue;
            e.boot = e.enabled ? bootFor(e, tok) : null;
        }
        for (Tab t : tabs.values()) apply(t);
    }

    private String bootFor(Ext e, String tokens) {
        JSONObject cfg = new JSONObject();
        StringBuilder fns = new StringBuilder("[");
        int fi = 0;
        try {
            cfg.put("id", e.id);
            cfg.put("token", e.token);
            cfg.put("base", e.base());
            cfg.put("messages", e.messages);
            cfg.put("manifest", e.manifest);
            cfg.put("lang", lang());
            JSONArray list = new JSONArray();
            for (Cs s : e.scripts) {
                JSONObject j = new JSONObject();
                j.put("m", s.matches);
                j.put("x", s.excludes);
                if (s.includeGlobs != null) j.put("ig", s.includeGlobs);
                if (s.excludeGlobs != null) j.put("xg", s.excludeGlobs);
                j.put("r", s.runAt);
                j.put("af", s.allFrames);
                j.put("ab", s.aboutBlank);
                if (!s.css.isEmpty()) j.put("css", s.css);
                if (s.js.isEmpty()) {
                    j.put("f", -1);
                } else {
                    if (fi > 0) fns.append(',');
                    fns.append("function(chrome,browser){\n").append(s.js).append("\n}");
                    j.put("f", fi++);
                }
                list.put(j);
            }
            cfg.put("cs", list);
        } catch (JSONException ignored) {
            return "";
        }
        fns.append(']');
        return fill(contentTpl, "__WDX_EXT__", cfg.toString(), "__WDX_TOKENS__", tokens, "__WDX_RUNTIME__", runtimeJs,
                "__WDX_SCRIPTS__", fns.toString());
    }

    private void apply(Tab t) {
        if (!docStart) return;
        try {
            for (ScriptHandler h : t.handlers) h.remove();
            t.handlers.clear();
            for (String id : order) {
                Ext e = exts.get(id);
                if (e == null || !e.enabled || e.boot == null || e.boot.isEmpty()) continue;
                t.handlers.add(WebViewCompat.addDocumentStartJavaScript(t.web, e.boot, ALL));
            }
        } catch (Throwable ex) {
            docStart = false;
        }
    }

    private void injectFallback(Tab t) {
        if (docStart || t.web == null) return;
        for (String id : order) {
            Ext e = exts.get(id);
            if (e == null || !e.enabled || e.boot == null || e.boot.isEmpty()) continue;
            t.web.evaluateJavascript(e.boot, null);
        }
    }

    void attach(final int id, WebView web) {
        final Tab t = new Tab(id, web);
        tabs.put(id, t);
        if (listener) {
            try {
                WebViewCompat.addWebMessageListener(web, "__wdxPort", ALL, (view, message, origin, isMain, proxy) -> {
                    String d = message.getData();
                    if (d != null) receive(d, t, proxy, isMain, origin == null ? null : origin.toString(), null, -1, null);
                });
            } catch (Throwable ex) {
                listener = false;
            }
        }
        if (!listener) web.addJavascriptInterface(new TabNative(t), "__wdxNative");
        apply(t);
        eventAll("tabs.onCreated", new JSONArray().put(tabJson(t)));
    }

    void detach(int id) {
        Tab t = tabs.remove(id);
        if (t == null) return;
        for (Ctx c : new ArrayList<>(ctxs.values())) if (c.tab == id) kill(c);
        closeRequests.remove(id);
        if (activeTab == id) activeTab = -1;
        JSONObject info = new JSONObject();
        put(info, "windowId", 1);
        put(info, "isWindowClosing", false);
        eventAll("tabs.onRemoved", new JSONArray().put(id).put(info));
    }

    void started(int id, String url) {
        Tab t = tabs.get(id);
        if (t == null) return;
        t.url = url == null ? "" : url;
        t.loading = true;
        long now = SystemClock.uptimeMillis();
        for (Ctx c : new ArrayList<>(ctxs.values())) if (c.tab == id && c.born <= now) kill(c);
        JSONObject ch = new JSONObject();
        put(ch, "status", "loading");
        put(ch, "url", t.url);
        eventAll("tabs.onUpdated", new JSONArray().put(id).put(ch).put(tabJson(t)));
        JSONObject nav = navJson(t);
        eventAll("webNavigation.onBeforeNavigate", new JSONArray().put(nav));
        eventAll("webNavigation.onCommitted", new JSONArray().put(nav));
    }

    void committed(int id) {
        Tab t = tabs.get(id);
        if (t != null) injectFallback(t);
    }

    void finished(int id, String url) {
        Tab t = tabs.get(id);
        if (t == null) return;
        if (url != null) t.url = url;
        t.loading = false;
        injectFallback(t);
        JSONObject ch = new JSONObject();
        put(ch, "status", "complete");
        eventAll("tabs.onUpdated", new JSONArray().put(id).put(ch).put(tabJson(t)));
        JSONObject nav = navJson(t);
        eventAll("webNavigation.onDOMContentLoaded", new JSONArray().put(nav));
        eventAll("webNavigation.onCompleted", new JSONArray().put(nav));
        cws(t);
    }

    void history(int id, String url) {
        Tab t = tabs.get(id);
        if (t == null || url == null || url.equals(t.url)) return;
        t.url = url;
        JSONObject ch = new JSONObject();
        put(ch, "url", url);
        eventAll("tabs.onUpdated", new JSONArray().put(id).put(ch).put(tabJson(t)));
        eventAll("webNavigation.onHistoryStateUpdated", new JSONArray().put(navJson(t)));
        cws(t);
    }

    void title(int id, String title) {
        Tab t = tabs.get(id);
        if (t == null) return;
        t.title = title == null ? "" : title;
        JSONObject ch = new JSONObject();
        put(ch, "title", t.title);
        eventAll("tabs.onUpdated", new JSONArray().put(id).put(ch).put(tabJson(t)));
    }

    void activated(int id) {
        if (activeTab == id || !tabs.containsKey(id)) return;
        activeTab = id;
        JSONObject info = new JSONObject();
        put(info, "tabId", id);
        put(info, "windowId", 1);
        eventAll("tabs.onActivated", new JSONArray().put(info));
    }

    void expect(int id, String url) {
        Tab t = tabs.get(id);
        if (t != null && url != null) t.url = url;
    }

    WebResourceResponse intercept(int tabId, WebResourceRequest req) {
        return serve(req, null, K_PAGE, tabId);
    }

    static String convertUrl(String url) {
        if (url == null || !url.startsWith("chrome-extension://")) return url;
        String rest = url.substring("chrome-extension://".length());
        int slash = rest.indexOf('/');
        String id = slash < 0 ? rest : rest.substring(0, slash);
        String path = slash < 0 ? "/" : rest.substring(slash);
        return "https://" + id + SUFFIX + path;
    }

    static String cwsId(String url) {
        if (url == null) return null;
        Matcher m = CWS.matcher(url);
        return m.matches() ? m.group(1) : null;
    }

    void installFromPage(final int tabId, final String id) {
        ui.post(() -> {
            Tab t = tabs.get(tabId);
            if (t == null || id == null || !id.equals(cwsId(t.url))) return;
            install(id);
        });
    }

    private void cws(Tab t) {
        String id = cwsId(t.url);
        if (id != null) {
            boolean installed = exts.containsKey(id);
            String js = "(function(){var b=document.getElementById('__wdx_cws');"
                    + "if(!b){b=document.createElement('button');b.id='__wdx_cws';"
                    + "b.style.cssText='position:fixed;right:18px;bottom:18px;z-index:2147483647;padding:12px 22px;border:0;border-radius:24px;"
                    + "background:#0B57D0;color:#fff;font:600 14px system-ui,sans-serif;box-shadow:0 4px 16px rgba(0,0,0,.3);cursor:pointer';"
                    + "(document.body||document.documentElement).appendChild(b);}"
                    + "b.textContent=" + JSONObject.quote(installed ? "Installed in WinDroid" : "Add to WinDroid") + ";"
                    + "b.disabled=" + installed + ";b.style.opacity=" + (installed ? "'.75'" : "'1'") + ";"
                    + "b.onclick=function(){try{WinDroid.installExtension(" + JSONObject.quote(id) + ");}catch(e){}};})()";
            t.web.evaluateJavascript(js, null);
        } else if (t.url.startsWith("https://chromewebstore.google.com/") || t.url.startsWith("https://chrome.google.com/webstore")) {
            t.web.evaluateJavascript("(function(){var b=document.getElementById('__wdx_cws');if(b)b.remove();})()", null);
        }
    }

    private final class TabNative {
        private final Tab tab;

        TabNative(Tab t) {
            tab = t;
        }

        @JavascriptInterface
        public void post(final String s) {
            ui.post(() -> receive(s, tab, null, true, null, null, -1, null));
        }
    }

    private final class ExtNative {
        private final Ext ext;
        private final int kind;
        private final WebView web;

        ExtNative(Ext e, int k, WebView w) {
            ext = e;
            kind = k;
            web = w;
        }

        @JavascriptInterface
        public void post(final String s) {
            ui.post(() -> receive(s, null, null, true, null, ext, kind, web));
        }
    }

    private static void put(JSONObject o, String k, Object v) {
        try {
            o.put(k, v);
        } catch (JSONException ignored) {
            return;
        }
    }

    private static String hostOf(String url) {
        if (url == null) return "";
        try {
            String h = Uri.parse(url).getHost();
            return h == null ? "" : h;
        } catch (Exception e) {
            return "";
        }
    }

    private static String originOf(String url) {
        try {
            Uri u = Uri.parse(url);
            if (u.getScheme() == null || u.getHost() == null) return "null";
            return u.getScheme() + "://" + u.getHost() + (u.getPort() > 0 ? ":" + u.getPort() : "");
        } catch (Exception e) {
            return "null";
        }
    }

    private void receive(String text, Tab tab, JavaScriptReplyProxy proxy, boolean main, String origin, Ext src, int srcKind, WebView srcWeb) {
        JSONObject o;
        try {
            o = new JSONObject(text);
        } catch (JSONException ex) {
            return;
        }
        Ext e = exts.get(o.optString("e"));
        if (e == null || !e.enabled || !e.token.equals(o.optString("k"))) return;
        if (src != null && src != e) return;
        String op = o.optString("o");
        if ("hello".equals(op)) {
            hello(o, e, tab, proxy, main, origin, srcKind, srcWeb);
            return;
        }
        Ctx c = ctxs.get(o.optInt("c"));
        if (c == null || c.ext != e) return;
        if (tab != null) {
            if (c.tab != tab.id) return;
        } else if (c.web != srcWeb) {
            return;
        }
        try {
            dispatch(c, op, o);
        } catch (Exception ex) {
            if (o.has("id")) cb(c, o.optInt("id"), null, String.valueOf(ex.getMessage()));
        }
    }

    private void hello(JSONObject o, Ext e, Tab tab, JavaScriptReplyProxy proxy, boolean main, String origin, int srcKind, WebView srcWeb) {
        int kind;
        if (tab != null) {
            String k = o.optString("kind");
            if ("content".equals(k)) {
                kind = K_CONTENT;
            } else if ("page".equals(k)) {
                kind = K_PAGE;
                String want = "https://" + e.host();
                if (origin != null ? !origin.equals(want) : !e.host().equals(hostOf(tab.url))) return;
            } else {
                return;
            }
        } else {
            kind = srcKind;
        }
        Ctx c = new Ctx();
        c.id = nextCtx++;
        c.ext = e;
        c.kind = kind;
        c.tab = tab != null ? tab.id : -1;
        c.main = proxy != null ? main : o.optBoolean("main", true);
        c.url = o.optString("url", "");
        c.proxy = proxy;
        c.web = tab != null ? (proxy == null ? tab.web : null) : srcWeb;
        c.born = SystemClock.uptimeMillis();
        c.ready = kind != K_BG || e.bgLoaded;
        ctxs.put(c.id, c);
        JSONObject hi = new JSONObject();
        put(hi, "o", "hi");
        put(hi, "hid", o.optString("hid"));
        put(hi, "tab", c.tab);
        put(hi, "f", c.main ? 0 : c.id);
        put(hi, "c", c.id);
        post(c, hi.toString());
        if (kind == K_BG && c.ready) ui.postDelayed(() -> flushStart(e), 60);
    }

    private void post(Ctx c, String s) {
        if (c.proxy != null) {
            try {
                c.proxy.postMessage(s);
            } catch (Throwable ignored) {
                return;
            }
            return;
        }
        WebView w = c.web;
        if (w != null) w.evaluateJavascript("window.__wdxIn&&window.__wdxIn(" + JSONObject.quote(s) + ")", null);
    }

    private void deliver(Ctx c, JSONObject o) {
        put(o, "c", c.id);
        String s = o.toString();
        if (!c.ready) {
            c.pending.add(s);
            return;
        }
        post(c, s);
    }

    private void cb(Ctx c, int id, Object v, String err) {
        if (!ctxs.containsKey(c.id)) return;
        JSONObject o = new JSONObject();
        put(o, "o", "cb");
        put(o, "id", id);
        if (v != null) put(o, "v", v);
        if (err != null) put(o, "err", err);
        put(o, "c", c.id);
        post(c, o.toString());
    }

    private void event(Ext e, String name, JSONArray args, boolean pagesOnly) {
        for (Ctx c : new ArrayList<>(ctxs.values())) {
            if (c.ext != e || (pagesOnly && c.kind == K_CONTENT)) continue;
            JSONObject o = new JSONObject();
            put(o, "o", "ev");
            put(o, "n", name);
            put(o, "a", args);
            deliver(c, o);
        }
    }

    private void eventAll(String name, JSONArray args) {
        if (ctxs.isEmpty()) return;
        for (Ctx c : new ArrayList<>(ctxs.values())) {
            if (c.kind == K_CONTENT) continue;
            JSONObject o = new JSONObject();
            put(o, "o", "ev");
            put(o, "n", name);
            put(o, "a", args);
            deliver(c, o);
        }
    }

    private Ctx background(Ext e) {
        for (Ctx c : ctxs.values()) if (c.ext == e && c.kind == K_BG) return c;
        return null;
    }

    private void bgReady(Ext e) {
        Ctx c = background(e);
        if (c != null && !c.ready) {
            c.ready = true;
            List<String> p = new ArrayList<>(c.pending);
            c.pending.clear();
            for (String s : p) post(c, s);
        }
        ui.postDelayed(() -> flushStart(e), 60);
    }

    private void flushStart(Ext e) {
        Ctx c = background(e);
        if (c == null || !c.ready || e.startEvents.isEmpty()) return;
        List<Object[]> list = new ArrayList<>(e.startEvents);
        e.startEvents.clear();
        for (Object[] ev : list) event(e, (String) ev[0], (JSONArray) ev[1], true);
    }

    private void kill(Ctx c) {
        if (ctxs.remove(c.id) == null) return;
        for (Route r : new ArrayList<>(routes.values())) {
            if (r.from == c) {
                routes.remove(r.id);
                ui.removeCallbacks(r.timer);
            } else if (r.wait.remove(c.id) && r.wait.isEmpty()) {
                finish(r, null, null);
            }
        }
        for (Map.Entry<String, PortRec> en : new ArrayList<>(ports.entrySet())) {
            PortRec p = en.getValue();
            if (p.opener == c) {
                ports.remove(en.getKey());
                for (Ctx r : p.receivers) deliver(r, portDis(en.getKey(), null));
            } else if (p.receivers.remove(c) && p.receivers.isEmpty()) {
                ports.remove(en.getKey());
                deliver(p.opener, portDis(en.getKey(), null));
            }
        }
        for (Map.Entry<String, Exec> en : new ArrayList<>(execs.entrySet())) {
            Exec x = en.getValue();
            if (x.ctx == c) execs.remove(en.getKey());
        }
    }

    private static JSONObject portDis(String id, String err) {
        JSONObject o = new JSONObject();
        put(o, "o", "pdis");
        put(o, "p", id);
        if (err != null) put(o, "err", err);
        return o;
    }

    private JSONObject sender(Ctx c) {
        JSONObject s = new JSONObject();
        put(s, "id", c.ext.id);
        put(s, "url", c.url);
        put(s, "origin", originOf(c.url));
        if (c.tab >= 0) {
            Tab t = tabs.get(c.tab);
            if (t != null) put(s, "tab", tabJson(t));
            put(s, "frameId", c.main ? 0 : c.id);
            put(s, "documentLifecycle", "active");
        }
        return s;
    }

    private List<Tab> sortedTabs() {
        List<Tab> list = new ArrayList<>(tabs.values());
        Collections.sort(list, (a, b) -> Integer.compare(a.id, b.id));
        return list;
    }

    private JSONObject tabJson(Tab t) {
        JSONObject o = new JSONObject();
        boolean active = t.id == activeTab;
        put(o, "id", t.id);
        put(o, "index", sortedTabs().indexOf(t));
        put(o, "windowId", 1);
        put(o, "openerTabId", JSONObject.NULL);
        put(o, "active", active);
        put(o, "highlighted", active);
        put(o, "selected", active);
        put(o, "pinned", false);
        put(o, "audible", false);
        put(o, "discarded", false);
        put(o, "autoDiscardable", true);
        put(o, "frozen", false);
        put(o, "incognito", false);
        put(o, "groupId", -1);
        JSONObject muted = new JSONObject();
        put(muted, "muted", false);
        put(o, "mutedInfo", muted);
        put(o, "status", t.loading ? "loading" : "complete");
        put(o, "url", t.url);
        put(o, "title", t.title.isEmpty() ? t.url : t.title);
        String origin = originOf(t.url);
        put(o, "favIconUrl", origin.startsWith("http") ? origin + "/favicon.ico" : "");
        if (t.web != null) {
            put(o, "width", Math.round(t.web.getWidth() / density));
            put(o, "height", Math.round(t.web.getHeight() / density));
        }
        return o;
    }

    private JSONObject pendingTab(String url) {
        JSONObject o = new JSONObject();
        put(o, "id", -1);
        put(o, "index", tabs.size());
        put(o, "windowId", 1);
        put(o, "active", true);
        put(o, "highlighted", true);
        put(o, "pinned", false);
        put(o, "incognito", false);
        put(o, "status", "loading");
        put(o, "url", "");
        put(o, "pendingUrl", url);
        put(o, "title", "");
        put(o, "groupId", -1);
        return o;
    }

    private JSONObject navJson(Tab t) {
        JSONObject o = new JSONObject();
        put(o, "tabId", t.id);
        put(o, "url", t.url);
        put(o, "frameId", 0);
        put(o, "parentFrameId", -1);
        put(o, "processId", 1);
        put(o, "timeStamp", (double) System.currentTimeMillis());
        put(o, "frameType", "outermost_frame");
        put(o, "documentLifecycle", "active");
        put(o, "transitionType", "link");
        put(o, "transitionQualifiers", new JSONArray());
        return o;
    }

    private void dispatch(Ctx c, String op, JSONObject o) throws JSONException {
        int id = o.optInt("id");
        JSONObject d = o.optJSONObject("d");
        if (d == null) d = new JSONObject();
        boolean page = c.kind != K_CONTENT;
        switch (op) {
            case "send":
                route(c, id, pageTargets(c), o);
                break;
            case "tsend":
                route(c, id, contentTargets(c.ext, o.optInt("t", -1), o.has("f") ? o.optInt("f") : -1), o);
                break;
            case "reply":
                onReply(c, o);
                break;
            case "conn":
                onConnect(c, o);
                break;
            case "pmsg":
                onPortMessage(c, o);
                break;
            case "pdis":
                onPortDisconnect(c, o);
                break;
            case "st":
                onStorage(c, o, id);
                break;
            case "xres":
                onExecResult(c, o);
                break;
            case "bye":
                kill(c);
                break;
            case "tabs":
                if (page) onTabs(c, o.optString("x"), d, id);
                break;
            case "exec":
                if (page) onExec(c, d, id);
                break;
            case "act":
                if (page) onAction(c, o.optString("x"), d, id);
                break;
            case "rt":
                if (page) onRuntime(c, o.optString("x"), id);
                break;
            case "al":
                if (page) onAlarm(c, o.optString("x"), d, id);
                break;
            case "fetch":
                if (page) onFetch(c, d, id);
                break;
            case "dl":
                if (page) {
                    String u = resolveUrl(c.ext, d.optString("url", ""));
                    if (u.startsWith("http") || u.startsWith("data:")) {
                        host.download(u, d.optString("filename", ""));
                        cb(c, id, null, null);
                    } else {
                        cb(c, id, null, "Invalid URL");
                    }
                }
                break;
            case "notify":
                if (page) {
                    JSONObject opts = d.optJSONObject("opts");
                    String title = opts == null ? "" : opts.optString("title", "");
                    String message = opts == null ? "" : opts.optString("message", "");
                    String text = title.isEmpty() ? message : message.isEmpty() ? title : title + "\n" + message;
                    if (!text.isEmpty()) Toast.makeText(act, text, Toast.LENGTH_LONG).show();
                    cb(c, id, null, null);
                }
                break;
            case "cookies":
                if (page) onCookies(c, o.optString("x"), d, id);
                break;
            case "off":
                if (page) onOffscreen(c, o.optString("x"), d, id);
                break;
            case "close":
                if (c.kind == K_POPUP) dismissPopup();
                break;
            case "size":
                if (c.kind == K_POPUP) resizePopup(o.optInt("w", 320), o.optInt("h", 120));
                break;
            default:
                if (o.has("id")) cb(c, id, null, null);
                break;
        }
    }

    private List<Ctx> pageTargets(Ctx from) {
        List<Ctx> out = new ArrayList<>();
        for (Ctx c : ctxs.values()) if (c.ext == from.ext && c.kind != K_CONTENT && c != from) out.add(c);
        return out;
    }

    private List<Ctx> contentTargets(Ext e, int tab, int frame) {
        List<Ctx> out = new ArrayList<>();
        for (Ctx c : ctxs.values()) {
            if (c.ext != e || c.kind != K_CONTENT || c.tab != tab) continue;
            if (frame == 0 && !c.main) continue;
            if (frame > 0 && c.id != frame) continue;
            out.add(c);
        }
        return out;
    }

    private void route(Ctx from, int cbId, List<Ctx> targets, JSONObject o) {
        if (targets.isEmpty()) {
            cb(from, cbId, null, "Could not establish connection. Receiving end does not exist.");
            return;
        }
        final Route r = new Route();
        r.id = "r" + (++routeSeq);
        r.from = from;
        r.cb = cbId;
        routes.put(r.id, r);
        JSONObject s = sender(from);
        for (Ctx t : targets) {
            JSONObject m = new JSONObject();
            put(m, "o", "msg");
            put(m, "r", r.id);
            if (o.has("m")) put(m, "m", o.opt("m"));
            put(m, "s", s);
            r.wait.add(t.id);
            deliver(t, m);
        }
        r.timer = () -> {
            if (routes.containsKey(r.id)) finish(r, null, null);
        };
        ui.postDelayed(r.timer, 300000);
    }

    private void onReply(Ctx c, JSONObject o) {
        Route r = routes.get(o.optString("r"));
        if (r == null || !r.wait.contains(c.id)) return;
        if (o.has("err")) {
            finish(r, null, o.optString("err"));
        } else if (o.optInt("n") == 1) {
            r.wait.remove(c.id);
            if (r.wait.isEmpty()) finish(r, null, null);
        } else {
            finish(r, o.has("v") ? o.opt("v") : null, null);
        }
    }

    private void finish(Route r, Object v, String err) {
        routes.remove(r.id);
        if (r.timer != null) ui.removeCallbacks(r.timer);
        cb(r.from, r.cb, v, err);
    }

    private void onConnect(Ctx c, JSONObject o) {
        String p = o.optString("p");
        if (p.isEmpty() || ports.containsKey(p)) return;
        List<Ctx> targets = o.has("t") ? contentTargets(c.ext, o.optInt("t", -1), o.has("f") ? o.optInt("f") : -1) : pageTargets(c);
        if (targets.isEmpty()) {
            deliver(c, portDis(p, "Could not establish connection. Receiving end does not exist."));
            return;
        }
        PortRec pr = new PortRec();
        pr.opener = c;
        ports.put(p, pr);
        JSONObject s = sender(c);
        for (Ctx t : targets) {
            JSONObject m = new JSONObject();
            put(m, "o", "conn");
            put(m, "p", p);
            put(m, "name", o.optString("name", ""));
            put(m, "s", s);
            pr.receivers.add(t);
            deliver(t, m);
        }
    }

    private void onPortMessage(Ctx c, JSONObject o) {
        String p = o.optString("p");
        PortRec pr = ports.get(p);
        if (pr == null) return;
        if (pr.opener == c) {
            for (Ctx t : pr.receivers) deliver(t, portMsg(p, o));
        } else if (pr.receivers.contains(c)) {
            deliver(pr.opener, portMsg(p, o));
        }
    }

    private static JSONObject portMsg(String p, JSONObject o) {
        JSONObject m = new JSONObject();
        put(m, "o", "pmsg");
        put(m, "p", p);
        if (o.has("m")) put(m, "m", o.opt("m"));
        return m;
    }

    private void onPortDisconnect(Ctx c, JSONObject o) {
        String p = o.optString("p");
        PortRec pr = ports.get(p);
        if (pr == null) return;
        if (pr.opener == c) {
            ports.remove(p);
            for (Ctx t : pr.receivers) deliver(t, portDis(p, null));
        } else if (pr.receivers.remove(c) && pr.receivers.isEmpty()) {
            ports.remove(p);
            deliver(pr.opener, portDis(p, null));
        }
    }

    private JSONObject area(Ext e, String name) {
        switch (name) {
            case "local":
                return e.local;
            case "sync":
                return e.sync;
            case "session":
                return e.session;
            default:
                return null;
        }
    }

    private static List<String> keyList(Object d) {
        List<String> out = new ArrayList<>();
        if (d instanceof String) {
            out.add((String) d);
        } else if (d instanceof JSONArray) {
            JSONArray a = (JSONArray) d;
            for (int i = 0; i < a.length(); i++) out.add(a.optString(i));
        } else if (d instanceof JSONObject) {
            Iterator<String> it = ((JSONObject) d).keys();
            while (it.hasNext()) out.add(it.next());
        }
        return out;
    }

    private void onStorage(Ctx c, JSONObject o, int id) throws JSONException {
        String name = o.optString("a");
        String x = o.optString("x");
        Object d = o.opt("d");
        if ("managed".equals(name)) {
            if ("get".equals(x)) cb(c, id, new JSONObject(), null);
            else if ("bytes".equals(x)) cb(c, id, 0, null);
            else cb(c, id, null, "This is a read-only store.");
            return;
        }
        Ext e = c.ext;
        JSONObject st = area(e, name);
        if (st == null) {
            cb(c, id, null, "Unknown storage area");
            return;
        }
        JSONObject changes = new JSONObject();
        switch (x) {
            case "get": {
                JSONObject out = new JSONObject();
                if (d == null || d == JSONObject.NULL) {
                    Iterator<String> it = st.keys();
                    while (it.hasNext()) {
                        String k = it.next();
                        out.put(k, st.get(k));
                    }
                } else if (d instanceof JSONObject) {
                    JSONObject def = (JSONObject) d;
                    Iterator<String> it = def.keys();
                    while (it.hasNext()) {
                        String k = it.next();
                        out.put(k, st.has(k) ? st.get(k) : def.get(k));
                    }
                } else {
                    for (String k : keyList(d)) if (st.has(k)) out.put(k, st.get(k));
                }
                cb(c, id, out, null);
                return;
            }
            case "bytes": {
                long n = 0;
                List<String> keys = d == null || d == JSONObject.NULL ? keyList(st) : keyList(d);
                for (String k : keys) if (st.has(k)) n += k.length() + String.valueOf(st.get(k)).length();
                cb(c, id, n, null);
                return;
            }
            case "set": {
                if (!(d instanceof JSONObject)) {
                    cb(c, id, null, "Error in invocation of storage.set: items must be an object");
                    return;
                }
                JSONObject items = (JSONObject) d;
                Iterator<String> it = items.keys();
                while (it.hasNext()) {
                    String k = it.next();
                    Object nv = items.get(k);
                    Object ov = st.opt(k);
                    if (ov != null && String.valueOf(ov).equals(String.valueOf(nv))) continue;
                    st.put(k, nv);
                    JSONObject ch = new JSONObject();
                    if (ov != null) ch.put("oldValue", ov);
                    ch.put("newValue", nv);
                    changes.put(k, ch);
                }
                break;
            }
            case "remove":
                for (String k : keyList(d)) {
                    if (!st.has(k)) continue;
                    JSONObject ch = new JSONObject();
                    ch.put("oldValue", st.get(k));
                    changes.put(k, ch);
                    st.remove(k);
                }
                break;
            case "clear":
                for (String k : keyList(st)) {
                    JSONObject ch = new JSONObject();
                    ch.put("oldValue", st.get(k));
                    changes.put(k, ch);
                }
                for (String k : keyList(changes)) st.remove(k);
                break;
            default:
                cb(c, id, null, "Unsupported storage operation");
                return;
        }
        if (changes.length() > 0) {
            if (!"session".equals(name)) writeLater(dataFile(e.id, name), st.toString());
            event(e, "storage.onChanged", new JSONArray().put(changes).put(name), false);
        }
        cb(c, id, null, null);
    }

    private Tab targetTab(Ctx c, JSONObject d) {
        if (d.has("tab") && !d.isNull("tab")) return tabs.get(d.optInt("tab", -1));
        int id = c.tab >= 0 ? c.tab : activeTab;
        Tab t = tabs.get(id);
        if (t == null && !tabs.isEmpty()) t = sortedTabs().get(0);
        return t;
    }

    private String resolveUrl(Ext e, String url) {
        if (url == null || url.isEmpty()) return "";
        if (url.startsWith("chrome-extension://")) return convertUrl(url);
        if (url.matches("^[a-zA-Z][a-zA-Z0-9+.-]*:.*")) return url;
        return e.base() + stripSlash(url);
    }

    private void onTabs(Ctx c, String x, JSONObject d, int id) {
        switch (x) {
            case "query":
                cb(c, id, query(d), null);
                return;
            case "get": {
                Tab t = tabs.get(d.optInt("tab", -1));
                if (t == null) cb(c, id, null, "No tab with id: " + d.opt("tab") + ".");
                else cb(c, id, tabJson(t), null);
                return;
            }
            case "current": {
                Tab t = c.tab >= 0 ? tabs.get(c.tab) : null;
                cb(c, id, t == null ? null : tabJson(t), null);
                return;
            }
            case "create": {
                String u = resolveUrl(c.ext, d.optString("url", ""));
                if (u.isEmpty()) u = NEW_TAB;
                opens.add(u);
                cb(c, id, pendingTab(u), null);
                return;
            }
            case "remove": {
                JSONArray ids = d.optJSONArray("tabs");
                if (ids != null) for (int i = 0; i < ids.length(); i++) closeRequests.add(ids.optInt(i, -1));
                cb(c, id, null, null);
                return;
            }
            default:
                break;
        }
        Tab t = targetTab(c, d);
        if (t == null) {
            cb(c, id, null, "No tab with id: " + d.opt("tab") + ".");
            return;
        }
        switch (x) {
            case "duplicate":
                opens.add(t.url);
                cb(c, id, pendingTab(t.url), null);
                break;
            case "update": {
                JSONObject p = d.optJSONObject("props");
                if (p != null && p.has("url")) {
                    String u = resolveUrl(c.ext, p.optString("url"));
                    if (!u.isEmpty()) {
                        t.url = u;
                        t.web.loadUrl(u);
                    }
                }
                cb(c, id, tabJson(t), null);
                break;
            }
            case "reload":
                t.web.reload();
                cb(c, id, null, null);
                break;
            case "back":
                if (t.web.canGoBack()) t.web.goBack();
                cb(c, id, null, null);
                break;
            case "forward":
                if (t.web.canGoForward()) t.web.goForward();
                cb(c, id, null, null);
                break;
            default:
                cb(c, id, null, "Unsupported tabs operation");
                break;
        }
    }

    private JSONArray query(JSONObject d) {
        JSONArray out = new JSONArray();
        List<Pattern> urls = new ArrayList<>();
        Object u = d.opt("url");
        if (u instanceof String) {
            Pattern p = matchPattern((String) u);
            if (p != null) urls.add(p);
        } else if (u instanceof JSONArray) {
            JSONArray a = (JSONArray) u;
            for (int i = 0; i < a.length(); i++) {
                Pattern p = matchPattern(a.optString(i));
                if (p != null) urls.add(p);
            }
        }
        Pattern title = d.has("title") ? glob(d.optString("title")) : null;
        for (Tab t : sortedTabs()) {
            boolean active = t.id == activeTab;
            if (d.has("active") && !d.isNull("active") && d.optBoolean("active") != active) continue;
            if (d.has("highlighted") && !d.isNull("highlighted") && d.optBoolean("highlighted") != active) continue;
            if (d.has("status") && !d.isNull("status") && !d.optString("status").equals(t.loading ? "loading" : "complete")) continue;
            if (d.optBoolean("pinned", false) || d.optBoolean("audible", false) || d.optBoolean("discarded", false)) continue;
            if (d.has("windowType") && !"normal".equals(d.optString("windowType"))) continue;
            if (u != null && u != JSONObject.NULL) {
                boolean ok = false;
                for (Pattern p : urls) if (p.matcher(t.url.split("#", 2)[0]).matches()) ok = true;
                if (!ok) continue;
            }
            if (title != null && !title.matcher(t.title).matches()) continue;
            out.put(tabJson(t));
        }
        return out;
    }

    private static String quoteRe(String s) {
        StringBuilder sb = new StringBuilder();
        for (char ch : s.toCharArray()) {
            if ("\\.[]{}()<>*+-=!?^$|/".indexOf(ch) >= 0) sb.append('\\');
            sb.append(ch);
        }
        return sb.toString();
    }

    static Pattern matchPattern(String p) {
        if (p == null) return null;
        if ("<all_urls>".equals(p)) return Pattern.compile("^(https?|wss?|file|ftp)://.*");
        Matcher m = Pattern.compile("^(\\*|https?|wss?|file|ftp|urn)://([^/]*)(/.*)?$").matcher(p);
        if (!m.matches()) return null;
        String sc = "*".equals(m.group(1)) ? "(https?|wss?)" : quoteRe(m.group(1));
        String h = m.group(2);
        String hr;
        if ("*".equals(h)) hr = "[^/]*";
        else if (h.startsWith("*.")) hr = "([^/]*\\.)?" + quoteRe(h.substring(2)) + "(:\\d+)?";
        else hr = quoteRe(h) + (h.contains(":") ? "" : "(:\\d+)?");
        String path = m.group(3) == null ? "/" : m.group(3);
        String pathRe = rebuildGlob(path);
        return Pattern.compile("^" + sc + "://" + hr + pathRe + "$");
    }

    private static String rebuildGlob(String path) {
        String[] parts = path.split("\\*", -1);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < parts.length; i++) {
            if (i > 0) sb.append(".*");
            sb.append(quoteRe(parts[i]));
        }
        return sb.toString();
    }

    private static Pattern glob(String g) {
        StringBuilder sb = new StringBuilder("^");
        for (char ch : g.toCharArray()) {
            if (ch == '*') sb.append(".*");
            else if (ch == '?') sb.append('.');
            else sb.append(quoteRe(String.valueOf(ch)));
        }
        return Pattern.compile(sb.append('$').toString(), Pattern.DOTALL);
    }

    private boolean restricted(String url) {
        String h = hostOf(url);
        return h.endsWith(SUFFIX) || cwsId(url) != null || h.equals("chromewebstore.google.com")
                || (url != null && url.startsWith("https://chrome.google.com/webstore"));
    }

    private void onExec(final Ctx c, JSONObject d, final int id) {
        final Tab t = targetTab(c, d);
        if (t == null || t.web == null) {
            cb(c, id, null, "No tab with id: " + d.opt("tab") + ".");
            return;
        }
        if (restricted(t.url)) {
            cb(c, id, null, "Cannot access contents of the page. Extension manifest must request permission to access the respective host.");
            return;
        }
        final Ext e = c.ext;
        String kind = d.optString("kind");
        if ("css".equals(kind) || "rcss".equals(kind)) {
            String css = d.has("css") && !d.isNull("css") ? d.optString("css") : readFiles(e, d.optJSONArray("files"), "\n");
            String key = e.id + ":" + css.hashCode();
            String js = "(function(c,k,r){var m=window.__wdxCss||(window.__wdxCss={});var d=document;"
                    + "if(r){var s=m[k];delete m[k];if(!s)return;if(s.nodeType)s.remove();else d.adoptedStyleSheets=d.adoptedStyleSheets.filter(function(x){return x!==s;});return;}"
                    + "if(m[k])return;try{var y=new CSSStyleSheet();y.replaceSync(c);d.adoptedStyleSheets=d.adoptedStyleSheets.concat([y]);m[k]=y;}"
                    + "catch(x){var st=d.createElement('style');st.textContent=c;(d.head||d.documentElement).appendChild(st);m[k]=st;}})("
                    + JSONObject.quote(css) + "," + JSONObject.quote(key) + "," + "rcss".equals(kind) + ")";
            t.web.evaluateJavascript(js, v -> cb(c, id, null, null));
            return;
        }
        String ensure = "if(!(window.__wdxReg&&window.__wdxReg.has(" + JSONObject.quote(e.id) + "))){" + (e.boot == null ? "" : e.boot) + "\n}\n";
        if ("code".equals(kind)) {
            String js = ensure + "window.__wdxReg&&window.__wdxReg.enter(" + JSONObject.quote(e.token) + "," + JSONObject.quote(e.id) + ");\ntry{\n"
                    + d.optString("code") + "\n}finally{window.__wdxReg&&window.__wdxReg.pop();}";
            t.web.evaluateJavascript(js, v -> {
                JSONArray out = new JSONArray();
                JSONObject r = new JSONObject();
                put(r, "frameId", 0);
                put(r, "documentId", "");
                Object val = parseValue(v);
                if (val != null) put(r, "result", val);
                out.put(r);
                cb(c, id, out, null);
            });
            return;
        }
        String body;
        if ("func".equals(kind)) {
            JSONArray args = d.optJSONArray("args");
            body = "return (" + d.optString("func") + ").apply(null," + (args == null ? "[]" : args.toString()) + ");";
        } else {
            body = readFiles(e, d.optJSONArray("files"), "\n;\n");
        }
        final String xid = "x" + (++execSeq);
        Exec ex = new Exec();
        ex.ctx = c;
        ex.cb = id;
        execs.put(xid, ex);
        String js = ensure + "(function(){var f=function(chrome,browser){\n" + body + "\n};return window.__wdxReg?window.__wdxReg.run("
                + JSONObject.quote(e.token) + "," + JSONObject.quote(e.id) + "," + JSONObject.quote(xid) + ",f):'__wdx_none';})()";
        t.web.evaluateJavascript(js, v -> {
            if ("\"__wdx_async\"".equals(v)) return;
            if (execs.remove(xid) != null) cb(c, id, null, "Cannot access contents of the page.");
        });
    }

    private static Object parseValue(String v) {
        if (v == null || "null".equals(v) || v.isEmpty()) return null;
        try {
            return new JSONTokener(v).nextValue();
        } catch (JSONException e) {
            return null;
        }
    }

    private void onExecResult(Ctx c, JSONObject o) {
        Exec ex = execs.remove(o.optString("x"));
        if (ex == null || ex.ctx.ext != c.ext) return;
        if (o.has("err")) {
            cb(ex.ctx, ex.cb, null, o.optString("err"));
            return;
        }
        JSONArray out = new JSONArray();
        JSONObject r = new JSONObject();
        put(r, "frameId", 0);
        put(r, "documentId", "");
        if (o.has("v")) put(r, "result", o.opt("v"));
        out.put(r);
        cb(ex.ctx, ex.cb, out, null);
    }

    private static String normColor(String s, String fallback) {
        if (s == null) return fallback;
        s = s.trim();
        Matcher m = Pattern.compile("^rgba?\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+).*\\)$").matcher(s);
        if (m.matches()) {
            int r = Math.min(255, Integer.parseInt(m.group(1)));
            int g = Math.min(255, Integer.parseInt(m.group(2)));
            int b = Math.min(255, Integer.parseInt(m.group(3)));
            return String.format(Locale.ROOT, "#%02X%02X%02X", r, g, b);
        }
        if (s.matches("^#[0-9a-fA-F]{3}$")) {
            return ("#" + s.charAt(1) + s.charAt(1) + s.charAt(2) + s.charAt(2) + s.charAt(3) + s.charAt(3)).toUpperCase(Locale.ROOT);
        }
        try {
            int c = Color.parseColor(s);
            return String.format(Locale.ROOT, "#%06X", c & 0xFFFFFF);
        } catch (IllegalArgumentException e) {
            return fallback;
        }
    }

    private void onAction(Ctx c, String x, JSONObject d, int id) {
        Ext e = c.ext;
        switch (x) {
            case "badge":
                e.badge = d.optString("text", "");
                cb(c, id, null, null);
                break;
            case "getBadge":
                cb(c, id, e.badge, null);
                break;
            case "color":
                e.badgeColor = normColor(d.optString("color"), e.badgeColor);
                cb(c, id, null, null);
                break;
            case "getColor": {
                int col = Color.parseColor(e.badgeColor);
                cb(c, id, new JSONArray().put(Color.red(col)).put(Color.green(col)).put(Color.blue(col)).put(255), null);
                break;
            }
            case "title":
                e.title = d.optString("title", "");
                cb(c, id, null, null);
                break;
            case "getTitle":
                cb(c, id, e.title.isEmpty() ? e.name : e.title, null);
                break;
            case "icon": {
                Object p = d.opt("path");
                String path = p == null || p == JSONObject.NULL ? null : pickIcon(p, null);
                if (path != null) {
                    e.iconOverride = path;
                    e.iconStamp++;
                }
                cb(c, id, null, null);
                break;
            }
            case "popup":
                e.popup = stripSlash(d.optString("popup", ""));
                cb(c, id, null, null);
                break;
            case "getPopup":
                cb(c, id, e.popup.isEmpty() ? "" : e.base() + e.popup, null);
                break;
            case "enable":
                e.actionEnabled = true;
                cb(c, id, null, null);
                break;
            case "disable":
                e.actionEnabled = false;
                cb(c, id, null, null);
                break;
            case "isEnabled":
                cb(c, id, e.actionEnabled, null);
                break;
            default:
                cb(c, id, null, null);
                break;
        }
    }

    private void onRuntime(Ctx c, String x, int id) {
        Ext e = c.ext;
        if ("options".equals(x)) {
            if (e.options.isEmpty()) {
                cb(c, id, null, "Could not create an options page.");
                return;
            }
            opens.add(e.base() + e.options);
            cb(c, id, null, null);
        } else if ("reload".equals(x)) {
            stopBackground(e);
            startBackground(e);
        } else {
            cb(c, id, null, null);
        }
    }

    private JSONObject alarmJson(Alarm a) {
        JSONObject o = new JSONObject();
        put(o, "name", a.name);
        put(o, "scheduledTime", (double) a.when);
        if (a.period > 0) put(o, "periodInMinutes", a.period);
        return o;
    }

    private void onAlarm(Ctx c, String x, JSONObject d, int id) {
        final Ext e = c.ext;
        switch (x) {
            case "create": {
                String name = d.optString("name", "");
                JSONObject info = d.optJSONObject("info");
                if (info == null) info = new JSONObject();
                Alarm old = e.alarms.remove(name);
                if (old != null) ui.removeCallbacks(old.run);
                final Alarm a = new Alarm();
                a.name = name;
                long now = System.currentTimeMillis();
                a.period = info.optDouble("periodInMinutes", 0);
                if (Double.isNaN(a.period)) a.period = 0;
                if (info.has("when")) {
                    a.when = (long) info.optDouble("when", now);
                } else {
                    double delay = info.optDouble("delayInMinutes", a.period);
                    if (Double.isNaN(delay)) delay = 0;
                    a.when = now + (long) (delay * 60000);
                }
                a.run = new Runnable() {
                    @Override
                    public void run() {
                        if (e.alarms.get(a.name) != a || !e.enabled) return;
                        event(e, "alarms.onAlarm", new JSONArray().put(alarmJson(a)), true);
                        if (a.period > 0) {
                            a.when += (long) (a.period * 60000);
                            ui.postDelayed(this, Math.max(1000, a.when - System.currentTimeMillis()));
                        } else {
                            e.alarms.remove(a.name);
                        }
                    }
                };
                e.alarms.put(name, a);
                ui.postDelayed(a.run, Math.max(0, a.when - now));
                cb(c, id, null, null);
                break;
            }
            case "get": {
                Alarm a = e.alarms.get(d.optString("name", ""));
                cb(c, id, a == null ? null : alarmJson(a), null);
                break;
            }
            case "getAll": {
                JSONArray out = new JSONArray();
                for (Alarm a : e.alarms.values()) out.put(alarmJson(a));
                cb(c, id, out, null);
                break;
            }
            case "clear": {
                Alarm a = e.alarms.remove(d.optString("name", ""));
                if (a != null) ui.removeCallbacks(a.run);
                cb(c, id, a != null, null);
                break;
            }
            case "clearAll": {
                boolean any = !e.alarms.isEmpty();
                clearAlarms(e);
                cb(c, id, any, null);
                break;
            }
            default:
                cb(c, id, null, null);
                break;
        }
    }

    private void clearAlarms(Ext e) {
        for (Alarm a : e.alarms.values()) ui.removeCallbacks(a.run);
        e.alarms.clear();
    }

    private void onFetch(final Ctx c, final JSONObject d, final int id) {
        net.execute(() -> {
            final JSONObject r = httpFetch(d);
            ui.post(() -> cb(c, id, r, null));
        });
    }

    private static void setMethod(HttpURLConnection c, String method) {
        try {
            c.setRequestMethod(method);
        } catch (ProtocolException e) {
            try {
                c.setRequestMethod("POST");
            } catch (ProtocolException ignored) {
                return;
            }
            c.setRequestProperty("X-HTTP-Method-Override", method);
        }
    }

    private JSONObject httpFetch(JSONObject d) {
        JSONObject r = new JSONObject();
        try {
            String url = d.optString("url");
            String method = d.optString("method", "GET").toUpperCase(Locale.ROOT);
            JSONObject hs = d.optJSONObject("headers");
            byte[] body = d.has("body") && !d.isNull("body") ? Base64.decode(d.optString("body"), Base64.DEFAULT) : null;
            boolean cred = d.optBoolean("cred", true);
            boolean redirected = false;
            CookieManager cm = CookieManager.getInstance();
            for (int hop = 0; hop < 12; hop++) {
                if (!url.startsWith("http://") && !url.startsWith("https://")) throw new IOException("Unsupported URL");
                HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
                c.setInstanceFollowRedirects(false);
                c.setConnectTimeout(20000);
                c.setReadTimeout(60000);
                setMethod(c, method);
                boolean hasUa = false;
                boolean hasCookie = false;
                if (hs != null) {
                    Iterator<String> it = hs.keys();
                    while (it.hasNext()) {
                        String k = it.next();
                        String lk = k.toLowerCase(Locale.ROOT);
                        if (lk.equals("host") || lk.equals("content-length") || lk.equals("connection") || lk.equals("accept-encoding")) continue;
                        if (lk.equals("user-agent")) hasUa = true;
                        if (lk.equals("cookie")) hasCookie = true;
                        c.setRequestProperty(k, hs.optString(k));
                    }
                }
                if (!hasUa) c.setRequestProperty("User-Agent", userAgent);
                if (!hasCookie && cred) {
                    String ck = cm.getCookie(url);
                    if (ck != null && !ck.isEmpty()) c.setRequestProperty("Cookie", ck);
                }
                if (body != null && !method.equals("GET") && !method.equals("HEAD")) {
                    c.setDoOutput(true);
                    c.setFixedLengthStreamingMode(body.length);
                    try (OutputStream os = c.getOutputStream()) {
                        os.write(body);
                    }
                }
                int code = c.getResponseCode();
                Map<String, List<String>> headers = c.getHeaderFields();
                if (cred && headers != null) {
                    for (Map.Entry<String, List<String>> en : headers.entrySet()) {
                        if (en.getKey() == null || !en.getKey().equalsIgnoreCase("set-cookie")) continue;
                        for (String v : en.getValue()) cm.setCookie(url, v);
                    }
                }
                if (code >= 300 && code < 400 && code != 304) {
                    String loc = c.getHeaderField("Location");
                    if (loc != null) {
                        url = new URL(new URL(url), loc).toString();
                        redirected = true;
                        if (code == 303 || ((code == 301 || code == 302) && method.equals("POST"))) {
                            method = "GET";
                            body = null;
                        }
                        c.disconnect();
                        continue;
                    }
                }
                InputStream in;
                try {
                    in = code >= 400 ? c.getErrorStream() : c.getInputStream();
                } catch (IOException ex) {
                    in = c.getErrorStream();
                }
                byte[] bytes = in == null || method.equals("HEAD") ? new byte[0] : readAll(in, 64L * 1024 * 1024);
                if (in != null) in.close();
                JSONArray ha = new JSONArray();
                if (headers != null) {
                    for (Map.Entry<String, List<String>> en : headers.entrySet()) {
                        String k = en.getKey();
                        if (k == null) continue;
                        String lk = k.toLowerCase(Locale.ROOT);
                        if (lk.equals("content-encoding") || lk.equals("content-length") || lk.equals("transfer-encoding") || lk.startsWith("x-android-")) continue;
                        for (String v : en.getValue()) ha.put(new JSONArray().put(k).put(v));
                    }
                }
                r.put("status", code);
                r.put("statusText", c.getResponseMessage() == null ? "" : c.getResponseMessage());
                r.put("headers", ha);
                r.put("body", Base64.encodeToString(bytes, Base64.NO_WRAP));
                r.put("url", url);
                r.put("redirected", redirected);
                c.disconnect();
                cm.flush();
                return r;
            }
            r.put("error", "Too many redirects");
        } catch (Exception ex) {
            put(r, "error", String.valueOf(ex.getMessage()));
        }
        return r;
    }

    private static String cookieUrl(JSONObject d) {
        if (d.has("url")) return d.optString("url");
        if (d.has("domain")) {
            String dom = d.optString("domain");
            while (dom.startsWith(".")) dom = dom.substring(1);
            return "https://" + dom + "/";
        }
        return null;
    }

    private JSONArray cookieList(String url, String only) {
        JSONArray out = new JSONArray();
        if (url == null) return out;
        String raw = CookieManager.getInstance().getCookie(url);
        if (raw == null || raw.isEmpty()) return out;
        String hostName = hostOf(url);
        for (String part : raw.split(";")) {
            String p = part.trim();
            if (p.isEmpty()) continue;
            int eq = p.indexOf('=');
            String name = eq < 0 ? "" : p.substring(0, eq);
            String value = eq < 0 ? p : p.substring(eq + 1);
            if (only != null && !only.equals(name)) continue;
            JSONObject c = new JSONObject();
            put(c, "name", name);
            put(c, "value", value);
            put(c, "domain", hostName);
            put(c, "hostOnly", true);
            put(c, "path", "/");
            put(c, "secure", url.startsWith("https"));
            put(c, "httpOnly", false);
            put(c, "sameSite", "unspecified");
            put(c, "session", true);
            put(c, "storeId", "0");
            out.put(c);
        }
        return out;
    }

    private void onCookies(Ctx c, String x, JSONObject d, int id) {
        CookieManager cm = CookieManager.getInstance();
        String url = cookieUrl(d);
        switch (x) {
            case "getAll":
                cb(c, id, cookieList(url, d.has("name") ? d.optString("name") : null), null);
                break;
            case "get": {
                JSONArray l = cookieList(url, d.optString("name"));
                cb(c, id, l.length() > 0 ? l.opt(0) : JSONObject.NULL, null);
                break;
            }
            case "set": {
                if (url == null) {
                    cb(c, id, null, "A url is required");
                    return;
                }
                StringBuilder sb = new StringBuilder();
                sb.append(d.optString("name", "")).append('=').append(d.optString("value", ""));
                if (d.has("domain")) sb.append("; Domain=").append(d.optString("domain"));
                sb.append("; Path=").append(d.optString("path", "/"));
                if (d.has("expirationDate")) {
                    SimpleDateFormat f = new SimpleDateFormat("EEE, dd MMM yyyy HH:mm:ss 'GMT'", Locale.US);
                    f.setTimeZone(TimeZone.getTimeZone("GMT"));
                    sb.append("; Expires=").append(f.format(new Date((long) (d.optDouble("expirationDate", 0) * 1000))));
                }
                if (d.optBoolean("secure", false)) sb.append("; Secure");
                if (d.optBoolean("httpOnly", false)) sb.append("; HttpOnly");
                String ss = d.optString("sameSite", "");
                if (ss.equals("lax")) sb.append("; SameSite=Lax");
                else if (ss.equals("strict")) sb.append("; SameSite=Strict");
                else if (ss.equals("no_restriction")) sb.append("; SameSite=None");
                cm.setCookie(url, sb.toString());
                cm.flush();
                JSONArray l = cookieList(url, d.optString("name", ""));
                cb(c, id, l.length() > 0 ? l.opt(0) : JSONObject.NULL, null);
                break;
            }
            case "remove": {
                if (url == null) {
                    cb(c, id, null, "A url is required");
                    return;
                }
                cm.setCookie(url, d.optString("name", "") + "=; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Path=/");
                cm.flush();
                JSONObject r = new JSONObject();
                put(r, "url", url);
                put(r, "name", d.optString("name", ""));
                put(r, "storeId", "0");
                cb(c, id, r, null);
                break;
            }
            case "stores": {
                JSONArray ids = new JSONArray();
                for (Tab t : sortedTabs()) ids.put(t.id);
                JSONObject s = new JSONObject();
                put(s, "id", "0");
                put(s, "tabIds", ids);
                cb(c, id, new JSONArray().put(s), null);
                break;
            }
            default:
                cb(c, id, null, null);
                break;
        }
    }

    private void onOffscreen(Ctx c, String x, JSONObject d, int id) {
        Ext e = c.ext;
        switch (x) {
            case "create":
                if (e.off != null) {
                    cb(c, id, null, "Only a single offscreen document may be created.");
                    return;
                }
                e.off = extView(e, K_OFF);
                e.off.loadUrl(resolveUrl(e, d.optString("url", "")));
                cb(c, id, null, null);
                break;
            case "close":
                if (e.off == null) {
                    cb(c, id, null, "No current offscreen document.");
                    return;
                }
                destroyView(e.off);
                e.off = null;
                cb(c, id, null, null);
                break;
            case "has":
                cb(c, id, e.off != null, null);
                break;
            default:
                cb(c, id, null, null);
                break;
        }
    }

    @SuppressLint("SetJavaScriptEnabled")
    private WebView extView(final Ext e, final int kind) {
        WebView w = new WebView(act);
        WebSettings s = w.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setDatabaseEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setMixedContentMode(WebSettings.MIXED_CONTENT_COMPATIBILITY_MODE);
        s.setAllowFileAccess(false);
        s.setSupportMultipleWindows(false);
        w.setBackgroundColor(kind == K_POPUP ? Color.WHITE : Color.TRANSPARENT);
        w.addJavascriptInterface(new ExtNative(e, kind, w), "__wdxNative");
        w.setWebViewClient(new WebViewClient() {
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                return serve(request, e, kind, -1);
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                Uri u = request.getUrl();
                if (e.host().equals(u.getHost())) return false;
                String url = convertUrl(u.toString());
                if (url.startsWith("http://") || url.startsWith("https://")) {
                    opens.add(url);
                    if (kind == K_POPUP) dismissPopup();
                }
                return true;
            }

            @Override
            public void onPageStarted(WebView view, String url, Bitmap favicon) {
                long now = SystemClock.uptimeMillis();
                for (Ctx c : new ArrayList<>(ctxs.values())) if (c.web == view && c.born <= now) kill(c);
                if (kind == K_BG && view == e.bg) e.bgLoaded = false;
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                if (kind == K_BG && view == e.bg) {
                    e.bgLoaded = true;
                    bgReady(e);
                }
            }
        });
        w.setWebChromeClient(new WebChromeClient());
        return w;
    }

    private void destroyView(WebView w) {
        if (w == null) return;
        for (Ctx c : new ArrayList<>(ctxs.values())) if (c.web == w) kill(c);
        try {
            w.stopLoading();
            w.destroy();
        } catch (Throwable ignored) {
            return;
        }
    }

    private void startBackground(Ext e) {
        if (!e.enabled || e.bg != null || !e.hasBackground()) return;
        e.bgLoaded = false;
        e.bg = extView(e, K_BG);
        e.bg.loadUrl(e.bgPage != null ? e.base() + e.bgPage : e.base() + "__wdx/bg.html");
    }

    private void stopBackground(Ext e) {
        WebView w = e.bg;
        e.bg = null;
        e.bgLoaded = false;
        destroyView(w);
    }

    private void stopExt(Ext e) {
        if (popupWeb != null) {
            for (Ctx c : ctxs.values()) {
                if (c.web == popupWeb && c.ext == e) {
                    dismissPopup();
                    break;
                }
            }
        }
        stopBackground(e);
        if (e.off != null) {
            destroyView(e.off);
            e.off = null;
        }
        clearAlarms(e);
        for (Ctx c : new ArrayList<>(ctxs.values())) if (c.ext == e) kill(c);
    }

    private static String esc(String s) {
        return s.replace("&", "&amp;").replace("\"", "&quot;").replace("<", "&lt;");
    }

    private String bgHtml(Ext e) {
        StringBuilder sb = new StringBuilder("<!doctype html><html><head><meta charset=\"utf-8\"><script src=\"/__wdx/api.js\"></script>");
        if (e.bgWorker != null) {
            sb.append("<script").append(e.bgModule ? " type=\"module\"" : "").append(" src=\"/").append(esc(e.bgWorker)).append("\"></script>");
        } else {
            for (String s : e.bgScripts) sb.append("<script src=\"/").append(esc(s)).append("\"></script>");
        }
        return sb.append("</head><body></body></html>").toString();
    }

    private String pageJs(Ext e, String kind) {
        JSONObject cfg = new JSONObject();
        put(cfg, "id", e.id);
        put(cfg, "token", e.token);
        put(cfg, "base", e.base());
        put(cfg, "kind", kind);
        put(cfg, "messages", e.messages);
        put(cfg, "manifest", e.manifest);
        put(cfg, "lang", lang());
        put(cfg, "worker", "background".equals(kind) && e.bgWorker != null ? e.base() + e.bgWorker : JSONObject.NULL);
        return fill(pageTpl, "__WDX_CFG__", cfg.toString(), "__WDX_RUNTIME__", runtimeJs);
    }

    private static String injectApi(String html) {
        String tag = "<script src=\"/__wdx/api.js\"></script>";
        Matcher m = HEAD.matcher(html);
        if (m.find()) return html.substring(0, m.end()) + tag + html.substring(m.end());
        m = HTML.matcher(html);
        if (m.find()) return html.substring(0, m.end()) + tag + html.substring(m.end());
        m = DOCTYPE.matcher(html);
        if (m.find()) return html.substring(0, m.end()) + tag + html.substring(m.end());
        return tag + html;
    }

    private static Map<String, String> headers() {
        Map<String, String> h = new HashMap<>();
        h.put("Access-Control-Allow-Origin", "*");
        h.put("Cache-Control", "no-cache");
        return h;
    }

    private static WebResourceResponse text(String mime, String body) {
        return new WebResourceResponse(mime, "UTF-8", 200, "OK", headers(),
                new ByteArrayInputStream(body.getBytes(StandardCharsets.UTF_8)));
    }

    private static WebResourceResponse notFound() {
        return new WebResourceResponse("text/plain", "UTF-8", 404, "Not Found", headers(), new ByteArrayInputStream(new byte[0]));
    }

    private static String mime(String name) {
        String n = name.toLowerCase(Locale.ROOT);
        int dot = n.lastIndexOf('.');
        String ext = dot < 0 ? "" : n.substring(dot + 1);
        switch (ext) {
            case "html":
            case "htm":
                return "text/html";
            case "js":
            case "mjs":
                return "text/javascript";
            case "css":
                return "text/css";
            case "json":
            case "map":
                return "application/json";
            case "png":
                return "image/png";
            case "jpg":
            case "jpeg":
                return "image/jpeg";
            case "gif":
                return "image/gif";
            case "webp":
                return "image/webp";
            case "svg":
                return "image/svg+xml";
            case "ico":
                return "image/x-icon";
            case "woff":
                return "font/woff";
            case "woff2":
                return "font/woff2";
            case "ttf":
                return "font/ttf";
            case "otf":
                return "font/otf";
            case "wasm":
                return "application/wasm";
            case "txt":
                return "text/plain";
            case "xml":
                return "application/xml";
            case "mp3":
                return "audio/mpeg";
            case "ogg":
                return "audio/ogg";
            case "wav":
                return "audio/wav";
            case "mp4":
                return "video/mp4";
            case "webm":
                return "video/webm";
            default:
                return "application/octet-stream";
        }
    }

    private WebResourceResponse serve(WebResourceRequest req, Ext viewExt, int kind, int tabId) {
        Uri u = req.getUrl();
        String h = u.getHost();
        if (h == null || !h.endsWith(SUFFIX)) return null;
        Ext e = exts.get(h.substring(0, h.length() - SUFFIX.length()));
        if (e == null || !e.enabled) return notFound();
        String path = u.getPath() == null ? "/" : u.getPath();
        try {
            if ("/__wdx/api.js".equals(path)) {
                String kindName;
                if (viewExt != null) {
                    if (viewExt != e) return notFound();
                    kindName = kind == K_BG ? "background" : kind == K_POPUP ? "popup" : "offscreen";
                } else {
                    Tab t = tabs.get(tabId);
                    if (t == null || !h.equals(hostOf(t.url))) return notFound();
                    kindName = "page";
                }
                return text("text/javascript", pageJs(e, kindName));
            }
            if ("/__wdx/bg.html".equals(path)) {
                if (viewExt != e || kind != K_BG) return notFound();
                return text("text/html", bgHtml(e));
            }
            File f = resolveFile(e, path);
            if (f == null || !f.isFile()) return notFound();
            String mime = mime(f.getName());
            if ("text/html".equals(mime)) return text(mime, injectApi(readText(f)));
            return new WebResourceResponse(mime, mime.startsWith("text/") || mime.endsWith("javascript") ? "UTF-8" : null, 200, "OK", headers(),
                    new FileInputStream(f));
        } catch (IOException ex) {
            return notFound();
        }
    }

    private int dp(double css) {
        return (int) Math.round(css * density);
    }

    private void openPopup(Ext e, int tab, int right, int top) {
        dismissPopup();
        if (tab > 0) activated(tab);
        View anchor = act.getWindow().getDecorView();
        if (anchor.getWindowToken() == null) return;
        WebView w = extView(e, K_POPUP);
        FrameLayout frame = new FrameLayout(act);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.WHITE);
        bg.setCornerRadius(dp(8));
        bg.setStroke(Math.max(1, dp(0.5)), 0x33000000);
        frame.setBackground(bg);
        frame.setClipToOutline(true);
        frame.addView(w, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        int pw = dp(320);
        int ph = dp(120);
        final PopupWindow p = new PopupWindow(frame, pw, ph, true);
        p.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        p.setOutsideTouchable(true);
        p.setElevation(dp(12));
        p.setAnimationStyle(0);
        p.setInputMethodMode(PopupWindow.INPUT_METHOD_NEEDED);
        p.setOnDismissListener(() -> {
            if (popup != p) return;
            WebView pv = popupWeb;
            popup = null;
            popupWeb = null;
            destroyView(pv);
        });
        popup = p;
        popupWeb = w;
        popupRight = right;
        popupTop = top;
        int sw = anchor.getWidth();
        p.showAtLocation(anchor, Gravity.TOP | Gravity.START, Math.max(4, Math.min(right - pw, sw - pw - 4)), top);
        w.loadUrl(e.base() + e.popup);
    }

    private void resizePopup(int cw, int ch) {
        if (popup == null) return;
        View anchor = act.getWindow().getDecorView();
        int sw = anchor.getWidth();
        int sh = anchor.getHeight();
        int pw = Math.min(dp(Math.max(25, Math.min(800, cw))), Math.max(dp(25), sw - 8));
        int ph = Math.min(dp(Math.max(25, Math.min(600, ch))), Math.max(dp(25), sh - popupTop - 8));
        int x = Math.max(4, Math.min(popupRight - pw, sw - pw - 4));
        popup.update(x, popupTop, pw, ph);
    }

    private void dismissPopup() {
        PopupWindow p = popup;
        if (p != null && p.isShowing()) p.dismiss();
    }

    private void click(Ext e, int tab) {
        if (tab > 0) activated(tab);
        Tab t = tabs.get(activeTab);
        if (e.bg == null) startBackground(e);
        Object arg = t == null ? JSONObject.NULL : tabJson(t);
        if (background(e) == null || !background(e).ready) e.startEvents.add(new Object[]{"action.onClicked", new JSONArray().put(arg)});
        else event(e, "action.onClicked", new JSONArray().put(arg), true);
    }

    private void setEnabled(String id, boolean on) {
        Ext e = exts.get(id);
        if (e == null || e.enabled == on) return;
        e.enabled = on;
        prefs.edit().putBoolean("enabled." + id, on).apply();
        if (on) startBackground(e);
        else stopExt(e);
        rebuild();
    }

    private void uninstall(String id) {
        Ext e = exts.get(id);
        if (e == null) return;
        e.enabled = false;
        stopExt(e);
        exts.remove(id);
        order.remove(id);
        saveOrder();
        prefs.edit().remove("enabled." + id).apply();
        final File dir = e.dir;
        io.execute(() -> {
            deleteTree(dir);
            deleteTree(dataFile(id, "local"));
            deleteTree(dataFile(id, "sync"));
        });
        rebuild();
        for (Tab t : tabs.values()) cws(t);
    }

    private void saveOrder() {
        StringBuilder sb = new StringBuilder();
        for (String s : order) {
            if (sb.length() > 0) sb.append(',');
            sb.append(s);
        }
        prefs.edit().putString("order", sb.toString()).apply();
    }

    private static String clean(String s) {
        return s == null ? "" : s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
    }

    private void install(String arg) {
        if (busy) return;
        String a = arg == null ? "" : arg.trim();
        final String id = ID.matcher(a).matches() ? a : cwsId(a);
        if (id == null) {
            state = "failed\t\tOpen an extension page in the Chrome Web Store first.";
            return;
        }
        busy = true;
        final String job = id + "-" + System.nanoTime();
        pendingJob = job;
        state = "downloading\t" + id;
        net.execute(() -> {
            File stage = new File(root, ".staging-" + job);
            try {
                byte[] crx = downloadCrx(id);
                deleteTree(stage);
                unpack(crx, stage);
                JSONObject mf = readManifest(stage);
                if (mf.has("app")) throw new IOException("Chrome apps are not supported in WinDroid.");
                if (mf.has("theme") && !mf.has("background") && !mf.has("content_scripts") && !mf.has("action")
                        && !mf.has("browser_action")) {
                    throw new IOException("Chrome themes are not supported in WinDroid.");
                }
                Ext probe = load(id, stage);
                List<String> perms = describe(probe);
                StringBuilder sb = new StringBuilder();
                for (String p : perms) {
                    if (sb.length() > 0) sb.append('|');
                    sb.append(clean(p).replace('|', '/'));
                }
                final Staged s = new Staged();
                s.id = id;
                s.dir = stage;
                s.name = probe.name;
                final String st = "ready\t" + id + "\t" + clean(probe.name) + "\t" + clean(probe.version) + "\t" + sb;
                ui.post(() -> {
                    if (!job.equals(pendingJob)) {
                        io.execute(() -> deleteTree(stage));
                        return;
                    }
                    staged = s;
                    state = st;
                });
            } catch (Exception ex) {
                deleteTree(stage);
                final String m = ex.getMessage() == null ? ex.getClass().getSimpleName() : ex.getMessage();
                ui.post(() -> {
                    if (!job.equals(pendingJob)) return;
                    pendingJob = null;
                    state = "failed\t" + id + "\t" + clean(m);
                    busy = false;
                });
            }
        });
    }

    private List<String> describe(Ext e) {
        List<String> out = new ArrayList<>();
        Set<String> hosts = new LinkedHashSet<>();
        Set<String> perms = new HashSet<>();
        JSONArray p = e.manifest.optJSONArray("permissions");
        if (p != null) {
            for (int i = 0; i < p.length(); i++) {
                String s = p.optString(i);
                if (s.equals("<all_urls>") || s.contains("://")) hosts.add(s);
                else perms.add(s);
            }
        }
        JSONArray hp = e.manifest.optJSONArray("host_permissions");
        if (hp != null) for (int i = 0; i < hp.length(); i++) hosts.add(hp.optString(i));
        for (Cs c : e.scripts) for (int i = 0; i < c.matches.length(); i++) hosts.add(c.matches.optString(i));
        boolean all = false;
        Set<String> names = new LinkedHashSet<>();
        for (String h : hosts) {
            if (h.equals("<all_urls>")) {
                all = true;
                continue;
            }
            Matcher m = Pattern.compile("^[^:]+://([^/]*)").matcher(h);
            if (!m.find()) continue;
            String hn = m.group(1);
            if (hn.equals("*")) all = true;
            else names.add(hn.startsWith("*.") ? hn.substring(2) : hn);
        }
        if (all) out.add("Read and change all your data on all websites");
        else if (names.size() > 0 && names.size() <= 3) out.add("Read and change your data on " + TextUtils.join(", ", names));
        else if (names.size() > 3) out.add("Read and change your data on a number of websites");
        if (perms.contains("tabs") || perms.contains("webNavigation") || perms.contains("history")) out.add("Read your browsing history");
        if (perms.contains("bookmarks")) out.add("Read and change your bookmarks");
        if (perms.contains("clipboardRead")) out.add("Read data you copy and paste");
        if (perms.contains("clipboardWrite")) out.add("Modify data you copy and paste");
        if (perms.contains("downloads")) out.add("Manage your downloads");
        if (perms.contains("notifications")) out.add("Display notifications");
        if (perms.contains("geolocation")) out.add("Detect your physical location");
        if (perms.contains("management")) out.add("Manage your apps, extensions, and themes");
        if (perms.contains("declarativeNetRequest") || perms.contains("declarativeNetRequestWithHostAccess") || perms.contains("webRequestBlocking")) {
            out.add("Block content on any page");
        }
        boolean partial = perms.contains("declarativeNetRequest") || perms.contains("declarativeNetRequestWithHostAccess")
                || perms.contains("webRequest") || perms.contains("nativeMessaging") || perms.contains("sidePanel")
                || perms.contains("identity") || perms.contains("debugger") || perms.contains("proxy")
                || e.manifest.has("devtools_page") || e.manifest.has("chrome_url_overrides") || e.manifest.has("side_panel");
        if (partial) out.add("!Some features of this extension may not work in WinDroid");
        return out;
    }

    private byte[] downloadCrx(String id) throws IOException {
        String url = "https://clients2.google.com/service/update2/crx?response=redirect&os=linux&arch=x64&os_arch=x86_64"
                + "&nacl_arch=x86-64&prod=chromecrx&prodchannel=&prodversion=131.0.6778.204&lang=en&acceptformat=crx3,crx2"
                + "&x=id%3D" + id + "%26installsource%3Dondemand%26uc";
        for (int hop = 0; hop < 8; hop++) {
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            c.setInstanceFollowRedirects(false);
            c.setConnectTimeout(20000);
            c.setReadTimeout(60000);
            c.setRequestProperty("User-Agent",
                    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");
            int code = c.getResponseCode();
            if (code >= 300 && code < 400) {
                String loc = c.getHeaderField("Location");
                c.disconnect();
                if (loc == null) break;
                url = new URL(new URL(url), loc).toString();
                continue;
            }
            if (code == 204 || code == 404) throw new IOException("This extension isn't available in the Chrome Web Store.");
            if (code != 200) throw new IOException("The Chrome Web Store returned HTTP " + code + ".");
            try (InputStream in = c.getInputStream()) {
                byte[] b = readAll(in, MAX_CRX);
                if (b.length == 0) throw new IOException("This extension isn't available in the Chrome Web Store.");
                return b;
            } finally {
                c.disconnect();
            }
        }
        throw new IOException("Too many redirects while downloading the extension.");
    }

    private static int le32(byte[] b, int off) {
        return (b[off] & 0xFF) | (b[off + 1] & 0xFF) << 8 | (b[off + 2] & 0xFF) << 16 | (b[off + 3] & 0xFF) << 24;
    }

    private static void unpack(byte[] crx, File dest) throws IOException {
        int start;
        if (crx.length > 16 && crx[0] == 'C' && crx[1] == 'r' && crx[2] == '2' && crx[3] == '4') {
            int version = le32(crx, 4);
            if (version == 3) start = 12 + le32(crx, 8);
            else if (version == 2) start = 16 + le32(crx, 8) + le32(crx, 12);
            else throw new IOException("Unsupported CRX version " + version + ".");
        } else if (crx.length > 4 && crx[0] == 'P' && crx[1] == 'K') {
            start = 0;
        } else {
            throw new IOException("The downloaded file is not a Chrome extension.");
        }
        if (start < 0 || start >= crx.length) throw new IOException("The extension package is damaged.");
        if (!dest.mkdirs() && !dest.isDirectory()) throw new IOException("Couldn't create the extension folder.");
        File base = dest.getCanonicalFile();
        long total = 0;
        try (ZipInputStream zin = new ZipInputStream(new ByteArrayInputStream(crx, start, crx.length - start))) {
            ZipEntry en;
            byte[] buf = new byte[65536];
            boolean any = false;
            while ((en = zin.getNextEntry()) != null) {
                File out = new File(base, en.getName()).getCanonicalFile();
                if (!out.getPath().startsWith(base.getPath() + File.separator)) throw new IOException("The extension package is unsafe.");
                if (en.isDirectory()) {
                    out.mkdirs();
                    continue;
                }
                File parent = out.getParentFile();
                if (parent != null) parent.mkdirs();
                try (OutputStream os = new FileOutputStream(out)) {
                    int n;
                    while ((n = zin.read(buf)) > 0) {
                        total += n;
                        if (total > 1024L * 1024 * 1024) throw new IOException("The extension is too large.");
                        os.write(buf, 0, n);
                    }
                }
                any = true;
            }
            if (!any) throw new IOException("The extension package is empty.");
        }
    }

    private void confirm() {
        Staged s = staged;
        if (s == null) return;
        staged = null;
        pendingJob = null;
        Ext old = exts.get(s.id);
        String previous = old == null ? null : old.version;
        if (old != null) {
            old.enabled = false;
            stopExt(old);
            exts.remove(s.id);
        }
        File dest = new File(root, s.id);
        deleteTree(dest);
        if (!s.dir.renameTo(dest)) {
            deleteTree(s.dir);
            state = "failed\t" + s.id + "\tCouldn't install the extension files.";
            busy = false;
            return;
        }
        Ext e;
        try {
            e = load(s.id, dest);
        } catch (Exception ex) {
            state = "failed\t" + s.id + "\t" + clean(String.valueOf(ex.getMessage()));
            busy = false;
            return;
        }
        e.enabled = true;
        prefs.edit().putBoolean("enabled." + s.id, true).apply();
        exts.put(e.id, e);
        if (!order.contains(e.id)) order.add(e.id);
        saveOrder();
        JSONObject details = new JSONObject();
        put(details, "reason", previous == null ? "install" : "update");
        if (previous != null) put(details, "previousVersion", previous);
        e.startEvents.add(new Object[]{"runtime.onInstalled", new JSONArray().put(details)});
        rebuild();
        startBackground(e);
        state = "installed\t" + e.id + "\t" + clean(e.name);
        busy = false;
        for (Tab t : tabs.values()) cws(t);
    }

    private void cancelInstall() {
        Staged s = staged;
        staged = null;
        pendingJob = null;
        if (s != null) {
            final File dir = s.dir;
            io.execute(() -> deleteTree(dir));
        }
        busy = false;
        state = "idle";
    }

    private String list() {
        StringBuilder sb = new StringBuilder();
        for (String id : order) {
            Ext e = exts.get(id);
            if (e == null) continue;
            sb.append(e.id).append('\t').append(clean(e.name)).append('\t').append(clean(e.version)).append('\t')
                    .append(e.enabled ? 1 : 0).append('\t').append(e.popup.isEmpty() ? 0 : 1).append('\t')
                    .append(e.options.isEmpty() ? 0 : 1).append('\t').append(clean(e.badge)).append('\t')
                    .append(e.badgeColor).append('\t').append(e.iconStamp).append('\t')
                    .append(clean(e.title.isEmpty() ? e.description : e.title)).append('\n');
        }
        return sb.toString();
    }

    private String icon(String id) {
        Ext e = exts.get(id);
        if (e == null) return "";
        String p = e.iconOverride != null ? e.iconOverride : e.iconPath;
        File f = p == null ? null : resolveFile(e, p);
        if (f == null || !f.isFile()) return "";
        Bitmap b = BitmapFactory.decodeFile(f.getPath());
        if (b == null) return "";
        int size = 48;
        Bitmap s = Bitmap.createScaledBitmap(b, size, size, true);
        int[] px = new int[size * size];
        s.getPixels(px, 0, size, 0, 0, size, size);
        if (s != b) s.recycle();
        b.recycle();
        byte[] rgba = new byte[px.length * 4];
        for (int i = 0; i < px.length; i++) {
            int c = px[i];
            rgba[i * 4] = (byte) (c >> 16);
            rgba[i * 4 + 1] = (byte) (c >> 8);
            rgba[i * 4 + 2] = (byte) c;
            rgba[i * 4 + 3] = (byte) (c >>> 24);
        }
        return size + "\t" + size + "\t" + Base64.encodeToString(rgba, Base64.NO_WRAP);
    }

    private String take(String ids) {
        StringBuilder sb = new StringBuilder();
        String u;
        while ((u = opens.poll()) != null) sb.append("open\t").append(clean(u)).append('\n');
        for (String part : ids.split(",")) {
            int id = num(part);
            if (id >= 0 && closeRequests.remove(id)) sb.append("close\t").append(id).append('\n');
        }
        return sb.toString();
    }

    private static int num(String s) {
        try {
            return Integer.parseInt(s.trim());
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    String sys(String cmd, String arg) {
        final String[] f = arg.split("\t", -1);
        switch (cmd) {
            case "ext.list":
                return list();
            case "ext.state":
                return state;
            case "ext.icon":
                return icon(arg);
            case "ext.take":
                return take(arg);
            case "ext.install":
                ui.post(() -> install(arg));
                return "";
            case "ext.confirm":
                ui.post(this::confirm);
                return "";
            case "ext.cancel":
                ui.post(this::cancelInstall);
                return "";
            case "ext.dismiss":
                ui.post(() -> {
                    if (!busy && (state.startsWith("failed") || state.startsWith("installed"))) state = "idle";
                });
                return "";
            case "ext.enable":
                ui.post(() -> setEnabled(f[0], f.length > 1 && f[1].equals("1")));
                return "";
            case "ext.remove":
                ui.post(() -> uninstall(arg));
                return "";
            case "ext.options":
                ui.post(() -> {
                    Ext e = exts.get(arg);
                    if (e != null && e.enabled && !e.options.isEmpty()) opens.add(e.base() + e.options);
                });
                return "";
            case "ext.popup":
                ui.post(() -> {
                    Ext e = exts.get(f[0]);
                    if (e == null || !e.enabled || f.length < 4) return;
                    if (e.popup.isEmpty()) click(e, num(f[1]));
                    else openPopup(e, num(f[1]), num(f[2]), num(f[3]));
                });
                return "";
            case "ext.close":
                ui.post(this::dismissPopup);
                return "";
            default:
                return "";
        }
    }

    void destroy() {
        dismissPopup();
        for (Ext e : exts.values()) {
            stopBackground(e);
            if (e.off != null) destroyView(e.off);
            e.off = null;
            clearAlarms(e);
        }
        io.shutdown();
        net.shutdownNow();
    }
}
