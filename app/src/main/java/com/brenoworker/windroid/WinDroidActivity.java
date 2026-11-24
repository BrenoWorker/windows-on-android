package com.brenoworker.windroid;

import android.annotation.SuppressLint;
import android.app.NativeActivity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.Display;
import android.view.DisplayCutout;
import android.view.PixelCopy;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.webkit.CookieManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.PopupWindow;

import org.json.JSONArray;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

public class WinDroidActivity extends NativeActivity {

    private static final String CHROME_PACKAGE = "com.android.chrome";

    private static final String HOOK = "(function(){"
            + "if(window.__wd)return;window.__wd=1;"
            + "var KC={ArrowLeft:37,ArrowUp:38,ArrowRight:39,ArrowDown:40,Enter:13,' ':32,Escape:27,Backspace:8,Tab:9,Delete:46,Home:36,End:35,Shift:16,Control:17,Alt:18};"
            + "var kcode=function(k){if(k==' ')return 'Space';if(k.length==1){var c=k.toUpperCase();"
            + "if(c>='A'&&c<='Z')return 'Key'+c;if(c>='0'&&c<='9')return 'Digit'+c;}return k;};"
            + "var knum=function(k){if(KC[k])return KC[k];if(k.length==1)return k.toUpperCase().charCodeAt(0);return 0;};"
            + "var ktgt=function(){var a=document.activeElement;if(a&&a!==document.body&&a!==document.documentElement)return a;"
            + "var f=document.fullscreenElement||document.webkitFullscreenElement;"
            + "var c=(f&&f.tagName=='CANVAS')?f:((f&&f.querySelector&&f.querySelector('canvas'))||document.querySelector('canvas'));"
            + "return c||document.body||document;};"
            + "var kev=function(t,type,k){var n=knum(k);var cc=type=='keypress'?(k.length==1?k.charCodeAt(0):n):0;"
            + "var e=new KeyboardEvent(type,{key:k,code:kcode(k),keyCode:n,which:n,charCode:cc,bubbles:true,cancelable:true,composed:true});"
            + "try{Object.defineProperty(e,'keyCode',{get:function(){return n;}});Object.defineProperty(e,'which',{get:function(){return type=='keypress'?cc:n;}});"
            + "Object.defineProperty(e,'charCode',{get:function(){return cc;}});}catch(x){}"
            + "return t.dispatchEvent(e);};"
            + "var held={};"
            + "window.__wdPad=function(k,d){var t=ktgt();if(d){held[k]=1;var ok=kev(t,'keydown',k);"
            + "if(ok&&(k.length==1||k=='Enter'))kev(t,'keypress',k);return ok;}"
            + "if(!held[k])return true;delete held[k];return kev(t,'keyup',k);};"
            + "window.__wdTap=function(k){var ok=window.__wdPad(k,true);window.__wdPad(k,false);return ok;};"
            + "window.__wdEmit=function(d){try{window.dispatchEvent(new CustomEvent('windroid',{detail:d}));}catch(x){}};"
            + "var ed=function(e){if(!e)return false;if(e.isContentEditable)return true;"
            + "var t=(e.tagName||'').toUpperCase();if(t=='TEXTAREA')return !e.readOnly&&!e.disabled;"
            + "if(t!='INPUT')return false;"
            + "return !/^(button|submit|checkbox|radio|range|color|file|image|reset|hidden)$/i.test(e.type||'')&&!e.readOnly&&!e.disabled;};"
            + "var say=function(v){try{WinDroid.focus(v);}catch(x){}};"
            + "document.addEventListener('focusin',function(e){if(ed(e.target))window.__wdTarget=e.target;say(ed(e.target));},true);"
            + "document.addEventListener('focusout',function(){setTimeout(function(){say(ed(document.activeElement));},60);},true);"
            + "document.addEventListener('touchend',function(e){if(ed(e.target)){window.__wdTarget=e.target;say(true);}},true);"
            + "var tg=function(){var a=document.activeElement;if(ed(a))return a;var t=window.__wdTarget;"
            + "if(t&&t.isConnected&&ed(t))return t;return null;};"
            + "var setv=function(e,v){var p=(e.tagName||'').toUpperCase()=='TEXTAREA'?HTMLTextAreaElement.prototype:HTMLInputElement.prototype;"
            + "var d=Object.getOwnPropertyDescriptor(p,'value');if(d&&d.set)d.set.call(e,v);else e.value=v;"
            + "e.dispatchEvent(new Event('input',{bubbles:true}));};"
            + "var sel=function(e){try{var s=e.selectionStart,n=e.selectionEnd;if(typeof s=='number')return [s,n];}catch(x){}"
            + "return [e.value.length,e.value.length];};"
            + "var setsel=function(e,p){try{e.setSelectionRange(p,p);}catch(x){}};"
            + "var ins=function(e,t){if(e.isContentEditable){document.execCommand('insertText',false,t);return;}"
            + "var r=sel(e);setv(e,e.value.slice(0,r[0])+t+e.value.slice(r[1]));setsel(e,r[0]+t.length);};"
            + "window.__wdInsert=function(t){var e=tg();if(!e){for(var i=0;i<t.length;i++)window.__wdTap(t.charAt(i));return;}if(document.activeElement!==e){try{e.focus();}catch(x){}}ins(e,t);};"
            + "window.__wdKey=function(k){var e=tg();"
            + "if(!e){if(!window.__wdTap(k))return;if(k=='ArrowDown')window.scrollBy(0,80);if(k=='ArrowUp')window.scrollBy(0,-80);"
            + "if(k=='Home')window.scrollTo(0,0);if(k=='End')window.scrollTo(0,document.body.scrollHeight);return;}"
            + "if(document.activeElement!==e){try{e.focus();}catch(x){}}"
            + "var ok=e.dispatchEvent(new KeyboardEvent('keydown',{key:k,code:k,bubbles:true,cancelable:true}));"
            + "if(!ok)return;"
            + "if(e.isContentEditable){if(k=='Backspace')document.execCommand('delete');"
            + "else if(k=='Delete')document.execCommand('forwardDelete');"
            + "else if(k=='Enter')document.execCommand('insertParagraph');return;}"
            + "var r=sel(e),v=e.value;"
            + "if(k=='Backspace'){var s=r[0];if(r[0]==r[1]&&s>0)s--;setv(e,v.slice(0,s)+v.slice(r[1]));setsel(e,s);}"
            + "else if(k=='Delete'){var n=r[1];if(r[0]==n&&n<v.length)n++;setv(e,v.slice(0,r[0])+v.slice(n));setsel(e,r[0]);}"
            + "else if(k=='ArrowLeft')setsel(e,Math.max(0,r[0]-1));"
            + "else if(k=='ArrowRight')setsel(e,Math.min(v.length,r[1]+1));"
            + "else if(k=='Home')setsel(e,0);"
            + "else if(k=='End')setsel(e,v.length);"
            + "else if(k=='Enter'){if((e.tagName||'').toUpperCase()=='TEXTAREA'){ins(e,'\\n');}"
            + "else{e.dispatchEvent(new KeyboardEvent('keypress',{key:'Enter',bubbles:true}));"
            + "e.dispatchEvent(new KeyboardEvent('keyup',{key:'Enter',bubbles:true}));"
            + "var f=e.form;if(f){if(f.requestSubmit)f.requestSubmit();else f.submit();}}}};"
            + "say(ed(document.activeElement));"
            + "})();";

    private final Map<Integer, Browser> browsers = new ConcurrentHashMap<>();
    private final AtomicInteger nextId = new AtomicInteger(1);
    private volatile int insetLeft;
    private volatile int insetTop;
    private volatile int insetRight;
    private volatile int insetBottom;
    private WebView spare;
    private BluetoothHub bluetooth;
    private WinRuntime winRuntime;

    private final class Browser {
        final int id;
        WebView web;
        FrameLayout frame;
        PopupWindow popup;
        String defaultAgent;
        volatile String url = "";
        volatile String title = "";
        volatile int progress = 100;
        volatile boolean canBack;
        volatile boolean canForward;
        volatile boolean loading;
        volatile boolean editable;
        volatile boolean desktop;
        volatile int[] snapshot;
        boolean parked;
        boolean capturing;
        boolean wantVisible;
        int px;
        int py;
        int pw = 1;
        int ph = 1;
        int hw = 1;
        int hh = 1;
        int parkW;
        int parkH;
        int captureSeq;
        GamePad pad;
        View custom;
        WebChromeClient.CustomViewCallback customCallback;
        volatile boolean apiFullscreen;
        volatile boolean controls;
        volatile boolean fullscreen;
        boolean shown;

        Browser(int id) {
            this.id = id;
        }

        void sync() {
            if (web == null) return;
            canBack = web.canGoBack();
            canForward = web.canGoForward();
            String u = web.getUrl();
            if (u != null) url = u;
        }
    }

    private final class PageBridge {
        private final Browser browser;

        PageBridge(Browser b) {
            browser = b;
        }

        @JavascriptInterface
        public void focus(boolean editable) {
            browser.editable = editable;
        }

        @JavascriptInterface
        public String version() {
            return API_VERSION;
        }

        @JavascriptInterface
        public String info() {
            try {
                JSONObject o = new JSONObject();
                o.put("platform", "WinDroid");
                o.put("version", API_VERSION);
                o.put("model", Build.MODEL);
                o.put("manufacturer", Build.MANUFACTURER);
                o.put("android", Build.VERSION.RELEASE);
                o.put("sdk", Build.VERSION.SDK_INT);
                o.put("touch", true);
                o.put("fullscreen", browser.fullscreen);
                o.put("controls", browser.controls);
                return o.toString();
            } catch (Exception e) {
                return "{}";
            }
        }

        @JavascriptInterface
        public void fullscreen(final boolean on) {
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    if (on) {
                        browser.apiFullscreen = true;
                        refreshGame(browser);
                    } else {
                        exitFullscreen(browser);
                    }
                }
            });
        }

        @JavascriptInterface
        public boolean isFullscreen() {
            return browser.fullscreen;
        }

        @JavascriptInterface
        public void controls(final boolean show) {
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    browser.controls = show;
                    refreshGame(browser);
                }
            });
        }

        @JavascriptInterface
        public boolean hasControls() {
            return browser.controls;
        }

        @JavascriptInterface
        public void setControls(final String json) {
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    if (browser.pad != null) browser.pad.configure(json);
                }
            });
        }

        @JavascriptInterface
        public void vibrate(int ms) {
            vibrateFor(Math.max(1, Math.min(ms, 2000)));
        }

        @JavascriptInterface
        public void save(String key, String value) {
            String k = storageKey(browser, key);
            if (k == null) return;
            if (value == null) storage().edit().remove(k).apply();
            else storage().edit().putString(k, value).apply();
        }

        @JavascriptInterface
        public String load(String key) {
            String k = storageKey(browser, key);
            return k == null ? null : storage().getString(k, null);
        }

        @JavascriptInterface
        public void remove(String key) {
            String k = storageKey(browser, key);
            if (k != null) storage().edit().remove(k).apply();
        }

        @JavascriptInterface
        public String keys() {
            String prefix = storageKey(browser, "");
            JSONArray out = new JSONArray();
            if (prefix == null) return out.toString();
            for (String k : storage().getAll().keySet())
                if (k.startsWith(prefix)) out.put(k.substring(prefix.length()));
            return out.toString();
        }

        @JavascriptInterface
        public void clear() {
            String prefix = storageKey(browser, "");
            if (prefix == null) return;
            SharedPreferences.Editor ed = storage().edit();
            for (String k : storage().getAll().keySet())
                if (k.startsWith(prefix)) ed.remove(k);
            ed.apply();
        }
    }

    private static final String API_VERSION = "1.0";

    private SharedPreferences storage() {
        return getSharedPreferences("windroid_games", Context.MODE_PRIVATE);
    }

    private static String storageKey(Browser b, String key) {
        if (key == null) return null;
        String url = b.url == null ? "" : b.url;
        Uri u = Uri.parse(url);
        String scheme = u.getScheme();
        String host = u.getHost();
        if (scheme == null) return null;
        String origin = scheme + "://" + (host == null ? "" : host) + (u.getPort() > 0 ? ":" + u.getPort() : "");
        return origin + "\n" + key;
    }

    private void vibrateFor(int ms) {
        try {
            Vibrator v = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
            if (v == null || !v.hasVibrator()) return;
            if (Build.VERSION.SDK_INT >= 26) v.vibrate(VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE));
            else v.vibrate(ms);
        } catch (Throwable ignored) {
        }
    }

    private void emit(Browser b, String detail) {
        if (b.web == null) return;
        b.web.evaluateJavascript(HOOK + ";window.__wdEmit(" + detail + ")", null);
    }

    private void refreshGame(Browser b) {
        boolean fs = b.custom != null || b.apiFullscreen;
        boolean changed = fs != b.fullscreen;
        b.fullscreen = fs;
        if (b.pad != null) b.pad.setState(b.controls, fs);
        if (changed) emit(b, "{type:'fullscreen',value:" + fs + "}");
    }

    private void exitFullscreen(Browser b) {
        b.apiFullscreen = false;
        if (b.custom != null) {
            if (b.web != null) b.web.evaluateJavascript("(function(){var d=document;if(d.exitFullscreen&&d.fullscreenElement)d.exitFullscreen();"
                    + "else if(d.webkitExitFullscreen)d.webkitExitFullscreen();})()", null);
            final Browser fb = b;
            getWindow().getDecorView().postDelayed(new Runnable() {
                @Override
                public void run() {
                    if (fb.custom != null) hideCustom(fb);
                }
            }, 250);
        }
        refreshGame(b);
    }

    private void hideCustom(Browser b) {
        View v = b.custom;
        WebChromeClient.CustomViewCallback cb = b.customCallback;
        b.custom = null;
        b.customCallback = null;
        if (v != null && b.frame != null) b.frame.removeView(v);
        if (b.web != null) b.web.setVisibility(View.VISIBLE);
        if (cb != null) {
            try {
                cb.onCustomViewHidden();
            } catch (Throwable ignored) {
            }
        }
        refreshGame(b);
    }

    private void padKey(Browser b, String key, boolean down) {
        if (b.web == null) return;
        b.web.evaluateJavascript(HOOK + ";window.__wdPad(" + JSONObject.quote(key) + "," + down + ")", null);
    }

    @Override
    @SuppressWarnings("deprecation")
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Window window = getWindow();
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = window.getAttributes();
            lp.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= 30
                    ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                    : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            window.setAttributes(lp);
        }
        View decor = window.getDecorView();
        decor.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
                readInsets(insets);
                return v.onApplyWindowInsets(insets);
            }
        });
        if (Build.VERSION.SDK_INT < 30) {
            decor.setOnSystemUiVisibilityChangeListener(new View.OnSystemUiVisibilityChangeListener() {
                @Override
                public void onSystemUiVisibilityChange(int visibility) {
                    if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) immersive();
                }
            });
        }
        immersive();
        preferHighRefreshRate();
        bluetooth = new BluetoothHub(this);
        winRuntime = new WinRuntime(this);
        bluetooth.start();
        decor.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (spare == null && !isFinishing()) spare = buildWebView();
            }
        }, 1500);
    }

    @SuppressWarnings("deprecation")
    private void preferHighRefreshRate() {
        try {
            Display display = getWindowManager().getDefaultDisplay();
            Display.Mode current = display.getMode();
            Display.Mode best = current;
            for (Display.Mode m : display.getSupportedModes()) {
                if (m.getPhysicalWidth() == current.getPhysicalWidth()
                        && m.getPhysicalHeight() == current.getPhysicalHeight()
                        && m.getRefreshRate() > best.getRefreshRate()) {
                    best = m;
                }
            }
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.preferredDisplayModeId = best.getModeId();
            lp.preferredRefreshRate = best.getRefreshRate();
            getWindow().setAttributes(lp);
        } catch (Throwable ignored) {
            return;
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) immersive();
    }

    @Override
    protected void onResume() {
        super.onResume();
        immersive();
        for (Browser b : browsers.values())
            if (b.web != null) b.web.onResume();
    }

    @Override
    protected void onPause() {
        for (Browser b : browsers.values())
            if (b.web != null) b.web.onPause();
        super.onPause();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (bluetooth != null) bluetooth.onPermissionResult();
    }

    private static String text(byte[] b) {
        return b == null ? "" : new String(b, StandardCharsets.UTF_8);
    }

    private void ui(Runnable r) {
        runOnUiThread(r);
    }

    public byte[] sys(byte[] cmdBytes, byte[] argBytes) {
        final String cmd = text(cmdBytes);
        final String arg = text(argBytes);
        String out = "";
        try {
            out = dispatch(cmd, arg);
        } catch (Throwable t) {
            out = "";
        }
        return (out == null ? "" : out).getBytes(StandardCharsets.UTF_8);
    }

    private String dispatch(String cmd, final String arg) {
        final BluetoothHub bt = bluetooth;
        final WinRuntime rt = winRuntime;
        if (bt == null || rt == null) return "";
        switch (cmd) {
            case "bt.state":
                return bt.state();
            case "bt.devices":
                return bt.devices();
            case "bt.permission":
                ui(new Runnable() {
                    @Override
                    public void run() {
                        bt.requestPermission();
                    }
                });
                return "";
            case "bt.enable":
                ui(new Runnable() {
                    @Override
                    public void run() {
                        bt.setEnabled(arg.equals("1"));
                    }
                });
                return "";
            case "bt.scan":
                ui(new Runnable() {
                    @Override
                    public void run() {
                        bt.scan(arg.equals("1"));
                    }
                });
                return "";
            case "bt.pair":
                bt.pair(arg);
                return "";
            case "bt.unpair":
                ui(new Runnable() {
                    @Override
                    public void run() {
                        bt.unpair(arg);
                    }
                });
                return "";
            case "bt.rename":
                bt.rename(arg);
                return "";
            case "bt.settings":
                ui(new Runnable() {
                    @Override
                    public void run() {
                        bt.openSystemSettings();
                    }
                });
                return "";
            case "bt.send": {
                final String[] result = {""};
                final java.util.concurrent.CountDownLatch latch = new java.util.concurrent.CountDownLatch(1);
                ui(new Runnable() {
                    @Override
                    public void run() {
                        result[0] = bt.send(new java.io.File(arg));
                        latch.countDown();
                    }
                });
                try {
                    latch.await(3, java.util.concurrent.TimeUnit.SECONDS);
                } catch (InterruptedException ignored) {
                }
                return result[0];
            }
            case "rt.status":
                return rt.status();
            case "rt.install":
                rt.install(arg);
                return "";
            case "rt.cancel":
                rt.cancelInstall();
                return "";
            case "rt.remove":
                rt.remove();
                return "";
            case "rt.reset":
                rt.resetPrefix();
                return "";
            case "exe.run": {
                int tab = arg.indexOf('\t');
                String exe = tab < 0 ? arg : arg.substring(0, tab);
                String args = tab < 0 ? "" : arg.substring(tab + 1);
                return String.valueOf(rt.run(exe, args));
            }
            case "exe.poll":
                return rt.poll(parseInt(arg));
            case "exe.write": {
                int tab = arg.indexOf('\t');
                if (tab > 0) rt.write(parseInt(arg.substring(0, tab)), arg.substring(tab + 1));
                return "";
            }
            case "exe.kill":
                rt.kill(parseInt(arg));
                return "";
            default:
                return "";
        }
    }

    private static int parseInt(String s) {
        try {
            return Integer.parseInt(s.trim());
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    public void webGamepad(final int id, final int button, final boolean down) {
        final Browser b = browsers.get(id);
        if (b == null) return;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (b.pad == null) return;
                String key = b.pad.gamepadKey(button);
                if (key != null && !key.isEmpty()) padKey(b, key, down);
                emit(b, "{type:'button',id:" + JSONObject.quote(b.pad.gamepadId(button)) + ",down:" + down + ",source:'gamepad'}");
            }
        });
    }

    @Override
    protected void onDestroy() {
        if (bluetooth != null) bluetooth.stop();
        for (Browser b : browsers.values()) release(b);
        browsers.clear();
        if (spare != null) {
            spare.destroy();
            spare = null;
        }
        super.onDestroy();
    }

    @SuppressWarnings("deprecation")
    private void immersive() {
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController c = window.getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }

    private void readInsets(WindowInsets insets) {
        int l = 0, t = 0, r = 0, b = 0;
        if (Build.VERSION.SDK_INT >= 28) {
            DisplayCutout cutout = insets.getDisplayCutout();
            if (cutout != null) {
                l = cutout.getSafeInsetLeft();
                t = cutout.getSafeInsetTop();
                r = cutout.getSafeInsetRight();
                b = cutout.getSafeInsetBottom();
            }
        }
        insetLeft = l;
        insetTop = t;
        insetRight = r;
        insetBottom = b;
    }

    public int[] getSafeInsets() {
        return new int[]{insetLeft, insetTop, insetRight, insetBottom};
    }

    public int webCreate() {
        final Browser b = new Browser(nextId.getAndIncrement());
        browsers.put(b.id, b);
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                setup(b);
            }
        });
        return b.id;
    }

    @SuppressLint("SetJavaScriptEnabled")
    private WebView buildWebView() {
        WebView web = new WebView(this);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setSupportZoom(true);
        s.setBuiltInZoomControls(true);
        s.setDisplayZoomControls(false);
        s.setUseWideViewPort(true);
        s.setLoadWithOverviewMode(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setSupportMultipleWindows(false);
        s.setMixedContentMode(WebSettings.MIXED_CONTENT_COMPATIBILITY_MODE);
        s.setOffscreenPreRaster(true);
        CookieManager.getInstance().setAcceptCookie(true);
        CookieManager.getInstance().setAcceptThirdPartyCookies(web, true);
        web.setBackgroundColor(Color.WHITE);
        return web;
    }

    private void setup(final Browser b) {
        if (!browsers.containsKey(b.id)) return;
        WebView web = spare != null ? spare : buildWebView();
        spare = null;
        b.defaultAgent = web.getSettings().getUserAgentString();
        web.addJavascriptInterface(new PageBridge(b), "WinDroid");
        web.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                Uri uri = request.getUrl();
                String scheme = uri.getScheme() == null ? "" : uri.getScheme();
                if (scheme.equals("http") || scheme.equals("https") || scheme.equals("about")
                        || scheme.equals("data") || scheme.equals("javascript") || scheme.equals("blob")) {
                    return false;
                }
                try {
                    Intent intent = scheme.equals("intent")
                            ? Intent.parseUri(uri.toString(), Intent.URI_INTENT_SCHEME)
                            : new Intent(Intent.ACTION_VIEW, uri);
                    intent.addCategory(Intent.CATEGORY_BROWSABLE);
                    intent.setComponent(null);
                    startActivity(intent);
                } catch (Exception ignored) {
                    String fallback = null;
                    try {
                        fallback = Intent.parseUri(uri.toString(), Intent.URI_INTENT_SCHEME).getStringExtra("browser_fallback_url");
                    } catch (Exception ignoredToo) {
                        fallback = null;
                    }
                    if (fallback != null) view.loadUrl(fallback);
                }
                return true;
            }

            @Override
            public void onPageStarted(WebView view, String url, Bitmap favicon) {
                b.url = url == null ? "" : url;
                if (b.apiFullscreen) {
                    b.apiFullscreen = false;
                    refreshGame(b);
                }
                if (b.pad != null) b.pad.configure(null);
                b.loading = true;
                b.editable = false;
                b.sync();
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                b.loading = false;
                b.progress = 100;
                b.sync();
                view.evaluateJavascript(HOOK, null);
                if (b.parked && !b.wantVisible) scheduleCapture(b, 450);
            }

            @Override
            public void doUpdateVisitedHistory(WebView view, String url, boolean isReload) {
                b.sync();
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onProgressChanged(WebView view, int newProgress) {
                b.progress = newProgress;
                if (newProgress >= 80) view.evaluateJavascript(HOOK, null);
                b.sync();
            }

            @Override
            public void onReceivedTitle(WebView view, String title) {
                b.title = title == null ? "" : title;
            }

            @Override
            public void onShowCustomView(View view, CustomViewCallback callback) {
                if (b.frame == null) {
                    callback.onCustomViewHidden();
                    return;
                }
                if (b.custom != null) hideCustom(b);
                b.custom = view;
                b.customCallback = callback;
                view.setBackgroundColor(Color.BLACK);
                b.frame.addView(view, 1, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
                refreshGame(b);
            }

            @Override
            public void onHideCustomView() {
                if (b.custom != null) hideCustom(b);
            }
        });
        FrameLayout frame = new FrameLayout(this);
        frame.setBackgroundColor(Color.WHITE);
        frame.addView(web, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        GamePad pad = new GamePad(this, new GamePad.Listener() {
            @Override
            public void onKey(String key, boolean down) {
                padKey(b, key, down);
            }

            @Override
            public void onButton(String id, boolean down) {
                emit(b, "{type:'button',id:" + JSONObject.quote(id) + ",down:" + down + "}");
            }

            @Override
            public void onToggleControls() {
                b.controls = !b.controls;
                refreshGame(b);
            }

            @Override
            public void onExitFullscreen() {
                exitFullscreen(b);
            }
        });
        frame.addView(pad, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        b.pad = pad;
        PopupWindow popup = new PopupWindow(frame, 1, 1, false);
        popup.setTouchable(true);
        popup.setOutsideTouchable(false);
        popup.setClippingEnabled(false);
        popup.setAnimationStyle(0);
        popup.setInputMethodMode(PopupWindow.INPUT_METHOD_NOT_NEEDED);
        b.web = web;
        b.frame = frame;
        b.popup = popup;
    }

    private void release(Browser b) {
        if (b.custom != null) hideCustom(b);
        if (b.popup != null && b.popup.isShowing()) b.popup.dismiss();
        if (b.web != null) {
            b.web.stopLoading();
            b.web.destroy();
        }
        b.web = null;
        b.popup = null;
        b.frame = null;
    }

    public void webDestroy(final int id) {
        final Browser b = browsers.remove(id);
        if (b == null) return;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                release(b);
            }
        });
    }

    public void webLayout(final int id, final int x, final int y, final int w, final int h, final boolean visible) {
        final Browser b = browsers.get(id);
        if (b == null) return;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (b.popup == null || isFinishing()) return;
                b.wantVisible = visible;
                if (visible != b.shown) {
                    b.shown = visible;
                    if (!visible && b.pad != null) b.pad.releaseAll();
                    emit(b, visible ? "{type:'resume'}" : "{type:'pause'}");
                }
                if (visible) {
                    b.px = x;
                    b.py = y;
                    b.pw = w;
                    b.ph = h;
                    if (!b.popup.isShowing()) {
                        View anchor = getWindow().getDecorView();
                        if (anchor.getWindowToken() == null) return;
                        b.popup.setWidth(w);
                        b.popup.setHeight(h);
                        b.popup.showAtLocation(anchor, Gravity.TOP | Gravity.START, x, y);
                    } else {
                        b.popup.update(x, y, w, h);
                    }
                    b.parked = false;
                } else {
                    b.hw = Math.max(1, w);
                    b.hh = Math.max(1, h);
                    if (!b.popup.isShowing()) {
                        View anchor = getWindow().getDecorView();
                        if (anchor.getWindowToken() == null) return;
                        b.popup.setWidth(b.hw);
                        b.popup.setHeight(b.hh);
                        b.popup.showAtLocation(anchor, Gravity.TOP | Gravity.START, PARK, PARK);
                        b.parked = true;
                        b.parkW = b.hw;
                        b.parkH = b.hh;
                        scheduleCapture(b, 700);
                    } else if (b.parked) {
                        if (b.parkW == b.hw && b.parkH == b.hh) return;
                        b.popup.update(PARK, PARK, b.hw, b.hh);
                        b.parkW = b.hw;
                        b.parkH = b.hh;
                        scheduleCapture(b, 400);
                    } else if (!b.capturing) {
                        capture(b);
                    }
                }
            }
        });
    }

    private static final int PARK = -30000;

    private void park(Browser b) {
        b.capturing = false;
        if (b.popup == null || !b.popup.isShowing() || b.wantVisible) return;
        int w = b.hw > 1 ? b.hw : b.pw;
        int h = b.hh > 1 ? b.hh : b.ph;
        boolean resized = !b.parked || w != b.parkW || h != b.parkH;
        b.popup.update(PARK, PARK, w, h);
        b.parked = true;
        b.parkW = w;
        b.parkH = h;
        if (resized) scheduleCapture(b, 400);
    }

    private void scheduleCapture(final Browser b, long delay) {
        final int seq = ++b.captureSeq;
        View anchor = getWindow().getDecorView();
        anchor.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (seq != b.captureSeq || isFinishing()) return;
                if (b.popup == null || !b.popup.isShowing() || !b.parked || b.wantVisible) return;
                if (b.capturing) {
                    scheduleCapture(b, 200);
                    return;
                }
                capture(b);
            }
        }, delay);
    }

    private void capture(final Browser b) {
        WebView web = b.web;
        if (web == null) {
            park(b);
            return;
        }
        if (Build.VERSION.SDK_INT >= 34 && b.frame != null) {
            b.capturing = true;
            try {
                PixelCopy.Request request = PixelCopy.Request.Builder.ofWindow(b.frame).build();
                PixelCopy.request(request, getMainExecutor(), new java.util.function.Consumer<PixelCopy.Result>() {
                    @Override
                    public void accept(PixelCopy.Result result) {
                        if (result.getStatus() == PixelCopy.SUCCESS && result.getBitmap() != null) {
                            store(b, result.getBitmap());
                        } else {
                            drawSnapshot(b);
                        }
                        park(b);
                    }
                });
                return;
            } catch (Throwable t) {
                b.capturing = false;
            }
        }
        drawSnapshot(b);
        park(b);
    }

    private void store(Browser b, Bitmap source) {
        try {
            Bitmap bmp = source.getConfig() == Bitmap.Config.ARGB_8888 ? source : source.copy(Bitmap.Config.ARGB_8888, false);
            int w = bmp.getWidth(), h = bmp.getHeight();
            if (w <= 0 || h <= 0 || (long) w * h > 8L * 1024 * 1024) return;
            int[] data = new int[w * h + 2];
            data[0] = w;
            data[1] = h;
            bmp.getPixels(data, 2, w, 0, 0, w, h);
            if (bmp != source) bmp.recycle();
            b.snapshot = data;
        } catch (Throwable ignored) {
            b.snapshot = null;
        }
    }

    private void drawSnapshot(Browser b) {
        WebView web = b.web;
        if (web == null) return;
        int w = web.getWidth(), h = web.getHeight();
        if (w <= 0 || h <= 0 || (long) w * h > 8L * 1024 * 1024) return;
        try {
            Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(bmp);
            canvas.drawColor(Color.WHITE);
            web.draw(canvas);
            store(b, bmp);
            bmp.recycle();
        } catch (Throwable ignored) {
            b.snapshot = null;
        }
    }

    public int[] webSnapshot(int id) {
        Browser b = browsers.get(id);
        if (b == null) return null;
        int[] s = b.snapshot;
        b.snapshot = null;
        return s;
    }

    public void webLoad(final int id, byte[] urlBytes) {
        final Browser b = browsers.get(id);
        if (b == null) return;
        final String url = new String(urlBytes, StandardCharsets.UTF_8);
        b.url = url;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (b.web != null) b.web.loadUrl(url);
            }
        });
    }

    public void webAction(final int id, final int action) {
        final Browser b = browsers.get(id);
        if (b == null) return;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                WebView web = b.web;
                if (web == null) return;
                switch (action) {
                    case 0:
                        if (web.canGoBack()) web.goBack();
                        break;
                    case 1:
                        if (web.canGoForward()) web.goForward();
                        break;
                    case 2:
                        web.reload();
                        break;
                    case 3:
                        web.stopLoading();
                        b.loading = false;
                        break;
                    case 4:
                        openInChrome(web.getUrl());
                        break;
                    case 5:
                        toggleDesktop(b);
                        break;
                    case 6:
                        b.controls = !b.controls;
                        refreshGame(b);
                        break;
                    case 7:
                        exitFullscreen(b);
                        break;
                    default:
                        break;
                }
                b.sync();
            }
        });
    }

    private void toggleDesktop(Browser b) {
        b.desktop = !b.desktop;
        WebSettings s = b.web.getSettings();
        if (b.desktop) {
            String agent = b.defaultAgent == null ? "" : b.defaultAgent;
            agent = agent.replaceAll("\\(Linux; Android[^)]*\\)", "(X11; Linux x86_64)")
                    .replace(" Mobile", "")
                    .replace("; wv", "");
            s.setUserAgentString(agent);
        } else {
            s.setUserAgentString(null);
        }
        b.web.reload();
    }

    private void openInChrome(String url) {
        if (url == null || url.isEmpty()) url = "https://www.google.com/";
        Uri uri = Uri.parse(url);
        Intent intent = new Intent(Intent.ACTION_VIEW, uri);
        Bundle extras = new Bundle();
        extras.putBinder("android.support.customtabs.extra.SESSION", null);
        intent.putExtras(extras);
        intent.putExtra("android.support.customtabs.extra.TOOLBAR_COLOR", 0xFF202020);
        intent.putExtra("android.support.customtabs.extra.SHARE_STATE", 1);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        Intent chrome = new Intent(intent).setPackage(CHROME_PACKAGE);
        try {
            startActivity(chrome);
        } catch (Exception e) {
            try {
                startActivity(intent);
            } catch (Exception ignored) {
                return;
            }
        }
    }

    public void webText(final int id, byte[] textBytes) {
        final Browser b = browsers.get(id);
        if (b == null) return;
        final String js = "window.__wdInsert&&window.__wdInsert(" + JSONObject.quote(new String(textBytes, StandardCharsets.UTF_8)) + ")";
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (b.web != null) b.web.evaluateJavascript(js, null);
            }
        });
    }

    public void webKey(final int id, int key) {
        final Browser b = browsers.get(id);
        if (b == null) return;
        String name;
        switch (key) {
            case 1: name = "Backspace"; break;
            case 2: name = "Delete"; break;
            case 3: name = "Enter"; break;
            case 4: name = "ArrowLeft"; break;
            case 5: name = "ArrowRight"; break;
            case 6: name = "ArrowUp"; break;
            case 7: name = "ArrowDown"; break;
            case 8: name = "Home"; break;
            case 9: name = "End"; break;
            case 10: name = "Tab"; break;
            default: return;
        }
        final String js = "window.__wdKey&&window.__wdKey('" + name + "')";
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (b.web != null) b.web.evaluateJavascript(js, null);
            }
        });
    }

    public String webState(int id) {
        Browser b = browsers.get(id);
        if (b == null) return "";
        String title = b.title == null ? "" : b.title.replace('\t', ' ').replace('\n', ' ');
        String url = b.url == null ? "" : b.url.replace('\t', ' ');
        return b.progress + "\t" + (b.canBack ? 1 : 0) + "\t" + (b.canForward ? 1 : 0) + "\t" + (b.loading ? 1 : 0)
                + "\t" + (b.editable ? 1 : 0) + "\t" + (b.desktop ? 1 : 0) + "\t" + (b.fullscreen ? 1 : 0)
                + "\t" + (b.controls ? 1 : 0) + "\t" + url + "\t" + title;
    }
}
