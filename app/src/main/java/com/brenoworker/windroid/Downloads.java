package com.brenoworker.windroid;

import android.content.Context;
import android.util.Base64;
import android.webkit.CookieManager;
import android.webkit.URLUtil;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLDecoder;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

final class Downloads {

    interface Listener {
        void onFinished(boolean ok);
    }

    private static final class Item {
        int id;
        String name;
        File file;
        String url;
        String agent;
        String referer;
        String mime;
        String disposition;
        volatile String state = "running";
        volatile long done;
        volatile long total = -1;
        volatile long speed;
        volatile boolean cancel;
        long started;
        String error = "";
    }

    private final Context context;
    private final Listener listener;
    private final List<Item> items = new ArrayList<>();
    private final AtomicInteger nextId = new AtomicInteger(1);

    Downloads(Context context, Listener listener) {
        this.context = context;
        this.listener = listener;
    }

    private File dir() {
        File d = new File(context.getFilesDir(), "C/Users/User/Downloads");
        d.mkdirs();
        return d;
    }

    private static String sanitize(String name) {
        if (name == null) name = "";
        name = name.replaceAll("[\\\\/:*?\"<>|\\p{Cntrl}]", "_").trim();
        while (name.startsWith(".")) name = name.substring(1);
        if (name.isEmpty()) name = "download";
        if (name.length() > 120) {
            int dot = name.lastIndexOf('.');
            String ext = dot > 0 && name.length() - dot <= 10 ? name.substring(dot) : "";
            name = name.substring(0, 120 - ext.length()) + ext;
        }
        return name;
    }

    private File unique(String name) {
        File d = dir();
        File f = new File(d, name);
        if (!f.exists() && !partial(f).exists()) return f;
        int dot = name.lastIndexOf('.');
        String stem = dot > 0 ? name.substring(0, dot) : name;
        String ext = dot > 0 ? name.substring(dot) : "";
        for (int i = 1; i < 1000; i++) {
            f = new File(d, stem + " (" + i + ")" + ext);
            if (!f.exists() && !partial(f).exists()) return f;
        }
        return new File(d, stem + " (" + System.currentTimeMillis() + ")" + ext);
    }

    private static File partial(File f) {
        return new File(f.getParentFile(), f.getName() + ".crdownload");
    }

    private Item add(String name) {
        Item it = new Item();
        it.id = nextId.getAndIncrement();
        it.file = unique(sanitize(name));
        it.name = it.file.getName();
        it.started = System.currentTimeMillis();
        synchronized (items) {
            items.add(0, it);
            while (items.size() > 50) items.remove(items.size() - 1);
        }
        return it;
    }

    void start(String url, String agent, String disposition, String mime, String referer, String suggested) {
        if (url == null) return;
        String name = suggested != null && !suggested.isEmpty() ? suggested : URLUtil.guessFileName(url, disposition, mime);
        if (url.startsWith("data:")) {
            saveDataUrl(name, url);
            return;
        }
        Item it = add(name);
        it.url = url;
        it.agent = agent;
        it.referer = referer;
        it.mime = mime;
        it.disposition = disposition;
        run(it);
    }

    void saveDataUrl(String name, String dataUrl) {
        final Item it = add(name == null || name.isEmpty() ? "download" : name);
        try {
            int comma = dataUrl.indexOf(',');
            if (!dataUrl.startsWith("data:") || comma < 0) throw new IOException("Unsupported data");
            String meta = dataUrl.substring(5, comma);
            String payload = dataUrl.substring(comma + 1);
            byte[] bytes = meta.endsWith(";base64") ? Base64.decode(payload, Base64.DEFAULT)
                    : URLDecoder.decode(payload.replace("+", "%2B"), "UTF-8").getBytes("UTF-8");
            it.total = bytes.length;
            try (OutputStream os = new FileOutputStream(it.file)) {
                os.write(bytes);
            }
            it.done = bytes.length;
            it.state = "done";
            listener.onFinished(true);
        } catch (Throwable t) {
            it.state = "failed";
            it.error = t.getMessage() == null ? "Error" : t.getMessage();
            listener.onFinished(false);
        }
    }

    private void run(final Item it) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                File tmp = partial(it.file);
                HttpURLConnection c = null;
                try {
                    URL url = new URL(it.url);
                    for (int hop = 0; hop < 8; hop++) {
                        c = (HttpURLConnection) url.openConnection();
                        c.setInstanceFollowRedirects(false);
                        c.setConnectTimeout(20000);
                        c.setReadTimeout(60000);
                        if (it.agent != null) c.setRequestProperty("User-Agent", it.agent);
                        if (it.referer != null && !it.referer.isEmpty()) c.setRequestProperty("Referer", it.referer);
                        String cookies = CookieManager.getInstance().getCookie(url.toString());
                        if (cookies != null) c.setRequestProperty("Cookie", cookies);
                        int code = c.getResponseCode();
                        if (code >= 300 && code < 400 && c.getHeaderField("Location") != null) {
                            url = new URL(url, c.getHeaderField("Location"));
                            c.disconnect();
                            continue;
                        }
                        if (code >= 400) throw new IOException("Server error (" + code + ")");
                        break;
                    }
                    it.total = c.getContentLengthLong();
                    long lastT = System.nanoTime(), lastB = 0;
                    try (InputStream in = c.getInputStream(); OutputStream os = new FileOutputStream(tmp)) {
                        byte[] buf = new byte[1 << 16];
                        int n;
                        while ((n = in.read(buf)) > 0) {
                            if (it.cancel) throw new IOException("Cancelled");
                            os.write(buf, 0, n);
                            it.done += n;
                            long now = System.nanoTime();
                            if (now - lastT > 500_000_000L) {
                                it.speed = (it.done - lastB) * 1_000_000_000L / (now - lastT);
                                lastT = now;
                                lastB = it.done;
                            }
                        }
                    }
                    if (it.total > 0 && it.done < it.total) throw new IOException("Network error");
                    if (!tmp.renameTo(it.file)) throw new IOException("Couldn't save the file");
                    it.state = "done";
                    listener.onFinished(true);
                } catch (Throwable t) {
                    tmp.delete();
                    it.state = it.cancel ? "cancelled" : "failed";
                    it.error = t.getMessage() == null ? "Error" : t.getMessage();
                    if (!it.cancel) listener.onFinished(false);
                } finally {
                    if (c != null) c.disconnect();
                }
            }
        }, "download-" + it.id).start();
    }

    private Item find(int id) {
        synchronized (items) {
            for (Item it : items)
                if (it.id == id) return it;
        }
        return null;
    }

    void cancel(int id) {
        Item it = find(id);
        if (it != null) it.cancel = true;
    }

    void retry(int id) {
        Item it = find(id);
        if (it == null || it.url == null || it.state.equals("running")) return;
        synchronized (items) {
            items.remove(it);
        }
        start(it.url, it.agent, it.disposition, it.mime, it.referer, it.name);
    }

    void remove(int id) {
        Item it = find(id);
        if (it == null) return;
        if (it.state.equals("running")) it.cancel = true;
        synchronized (items) {
            items.remove(it);
        }
    }

    void clear() {
        synchronized (items) {
            List<Item> keep = new ArrayList<>();
            for (Item it : items)
                if (it.state.equals("running")) keep.add(it);
            items.clear();
            items.addAll(keep);
        }
    }

    String list() {
        StringBuilder sb = new StringBuilder();
        synchronized (items) {
            for (Item it : items) {
                String state = it.state;
                if (state.equals("done") && !it.file.exists()) state = "deleted";
                sb.append(it.id).append('\t').append(clean(it.name)).append('\t').append(it.file.getPath()).append('\t').append(state)
                        .append('\t').append(it.done).append('\t').append(it.total).append('\t').append(it.speed).append('\t')
                        .append(clean(it.error)).append('\n');
            }
        }
        return sb.toString();
    }

    private static String clean(String s) {
        return s == null ? "" : s.replace('\t', ' ').replace('\n', ' ');
    }
}
