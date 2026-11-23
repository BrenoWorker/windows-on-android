package com.brenoworker.windroid;

import android.annotation.SuppressLint;
import android.app.NativeActivity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
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

import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

public class WinDroidActivity extends NativeActivity {

    private static final String CHROME_PACKAGE = "com.android.chrome";

    private static final String HOOK = "(function(){"
            + "if(window.__wd)return;window.__wd=1;"
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
            + "window.__wdInsert=function(t){var e=tg();if(!e)return;if(document.activeElement!==e){try{e.focus();}catch(x){}}ins(e,t);};"
            + "window.__wdKey=function(k){var e=tg();"
            + "if(!e){if(k=='ArrowDown')window.scrollBy(0,80);if(k=='ArrowUp')window.scrollBy(0,-80);"
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
    protected void onDestroy() {
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
        });
        FrameLayout frame = new FrameLayout(this);
        frame.setBackgroundColor(Color.WHITE);
        frame.addView(web, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
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
                } else if (b.popup.isShowing() && !b.parked && !b.capturing) {
                    capture(b);
                }
            }
        });
    }

    private void park(Browser b) {
        b.capturing = false;
        if (b.popup == null || !b.popup.isShowing() || b.wantVisible) return;
        b.popup.update(-30000, -30000, b.pw, b.ph);
        b.parked = true;
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
                + "\t" + (b.editable ? 1 : 0) + "\t" + (b.desktop ? 1 : 0) + "\t" + url + "\t" + title;
    }
}
