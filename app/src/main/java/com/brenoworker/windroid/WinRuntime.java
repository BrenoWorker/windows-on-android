package com.brenoworker.windroid;

import android.content.Context;
import android.system.Os;
import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.zip.GZIPInputStream;

final class WinRuntime {

    static final String DEFAULT_URL =
            "https://github.com/BrenoWorker/windows-on-android/releases/download/runtime-v1/windroid-runtime-x86_64.tar.gz";
    private static final int MAX_OUTPUT = 256 * 1024;

    private static final class Job {
        Process process;
        final StringBuilder out = new StringBuilder();
        volatile boolean done;
        volatile int exit;
    }

    private final Context context;
    private final File base;
    private final File runtime;
    private final File prefix;
    private final File tmp;
    private final File home;
    private final File box64;
    private final Map<Integer, Job> jobs = new ConcurrentHashMap<>();
    private final AtomicInteger nextJob = new AtomicInteger(1);
    private volatile String state = "idle";
    private volatile long done;
    private volatile long total;
    private volatile String message = "";
    private volatile int messageSeq;
    private volatile boolean cancel;

    WinRuntime(Context context) {
        this.context = context;
        base = new File(context.getFilesDir(), "wine");
        runtime = new File(base, "runtime");
        prefix = new File(base, "prefix");
        tmp = new File(base, "tmp");
        home = new File(base, "home");
        box64 = new File(context.getApplicationInfo().nativeLibraryDir, "libbox64.so");
    }

    private void setMessage(String m) {
        message = m == null ? "" : m;
        messageSeq++;
    }

    boolean installed() {
        return new File(runtime, "windroid-runtime.ini").isFile();
    }

    String status() {
        Map<String, String> ini = installed() ? readIni() : new HashMap<String, String>();
        int progress = total > 0 ? (int) Math.min(1000, done * 1000 / total) : -1;
        String msg = message;
        return (box64.isFile() ? 1 : 0) + "\t" + (installed() ? 1 : 0) + "\t" + state + "\t" + progress + "\t"
                + clean(ini.containsKey("version") ? ini.get("version") : "") + "\t" + clean(msg) + "\t" + done + "\t" + messageSeq;
    }

    void install(final String source) {
        if (state.equals("downloading") || state.equals("extracting")) return;
        cancel = false;
        state = "downloading";
        done = 0;
        total = 0;
        message = "";
        new Thread(new Runnable() {
            @Override
            public void run() {
                File staging = new File(base, "runtime.new");
                try {
                    deleteTree(staging);
                    staging.mkdirs();
                    InputStream raw = open(source == null || source.isEmpty() ? DEFAULT_URL : source);
                    try (InputStream in = new GZIPInputStream(new BufferedInputStream(raw, 1 << 16), 1 << 16)) {
                        state = "extracting";
                        untar(in, staging);
                    }
                    if (cancel) throw new IOException("Cancelled");
                    if (!new File(staging, "windroid-runtime.ini").isFile()) throw new IOException("This file is not a WinDroid runtime package");
                    deleteTree(runtime);
                    if (!staging.renameTo(runtime)) throw new IOException("Couldn't finish the installation");
                    tmp.mkdirs();
                    home.mkdirs();
                    state = "idle";
                    setMessage("Windows compatibility runtime installed");
                } catch (Throwable t) {
                    deleteTree(staging);
                    state = "failed";
                    setMessage(t.getMessage() == null ? t.getClass().getSimpleName() : t.getMessage());
                }
            }
        }, "runtime-install").start();
    }

    void cancelInstall() {
        cancel = true;
    }

    void remove() {
        if (state.equals("downloading") || state.equals("extracting")) return;
        for (Job j : jobs.values())
            if (j.process != null) j.process.destroy();
        deleteTree(runtime);
        deleteTree(prefix);
        deleteTree(tmp);
        setMessage("Runtime removed");
    }

    void resetPrefix() {
        deleteTree(prefix);
        setMessage("Windows settings were reset");
    }

    private InputStream open(String source) throws IOException {
        File f = new File(source);
        if (f.isFile()) {
            total = f.length();
            return new Counting(new FileInputStream(f));
        }
        URL url = new URL(source);
        HttpURLConnection c = null;
        for (int i = 0; i < 6; i++) {
            c = (HttpURLConnection) url.openConnection();
            c.setInstanceFollowRedirects(false);
            c.setConnectTimeout(20000);
            c.setReadTimeout(60000);
            c.setRequestProperty("User-Agent", "WinDroid");
            int code = c.getResponseCode();
            if (code >= 300 && code < 400) {
                String loc = c.getHeaderField("Location");
                c.disconnect();
                if (loc == null) throw new IOException("Download failed (HTTP " + code + ")");
                url = new URL(url, loc);
                continue;
            }
            if (code != 200) {
                c.disconnect();
                throw new IOException(code == 404 ? "The runtime package was not found online" : "Download failed (HTTP " + code + ")");
            }
            total = c.getContentLengthLong();
            return new Counting(c.getInputStream());
        }
        throw new IOException("Too many redirects");
    }

    private final class Counting extends FilterInputStream {
        Counting(InputStream in) {
            super(in);
        }

        @Override
        public int read() throws IOException {
            if (cancel) throw new IOException("Cancelled");
            int b = super.read();
            if (b >= 0) done++;
            return b;
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (cancel) throw new IOException("Cancelled");
            int n = super.read(b, off, len);
            if (n > 0) done += n;
            return n;
        }
    }

    private static void readFully(InputStream in, byte[] buf, int len) throws IOException {
        int off = 0;
        while (off < len) {
            int n = in.read(buf, off, len - off);
            if (n < 0) throw new IOException("The runtime package is incomplete");
            off += n;
        }
    }

    private static void skip(InputStream in, long n) throws IOException {
        byte[] buf = new byte[8192];
        while (n > 0) {
            int r = in.read(buf, 0, (int) Math.min(buf.length, n));
            if (r < 0) throw new IOException("The runtime package is incomplete");
            n -= r;
        }
    }

    private static String field(byte[] h, int off, int len) {
        int end = off;
        while (end < off + len && h[end] != 0) end++;
        return new String(h, off, end - off, StandardCharsets.UTF_8);
    }

    private static long octal(byte[] h, int off, int len) {
        if ((h[off] & 0x80) != 0) {
            long v = 0;
            for (int i = off + 1; i < off + len; i++) v = (v << 8) | (h[i] & 0xFF);
            return v;
        }
        String s = field(h, off, len).trim();
        if (s.isEmpty()) return 0;
        return Long.parseLong(s, 8);
    }

    private static Map<String, String> pax(byte[] data) {
        Map<String, String> m = new HashMap<>();
        int i = 0;
        while (i < data.length) {
            int sp = i;
            while (sp < data.length && data[sp] != ' ') sp++;
            if (sp >= data.length) break;
            int len;
            try {
                len = Integer.parseInt(new String(data, i, sp - i, StandardCharsets.UTF_8));
            } catch (NumberFormatException e) {
                break;
            }
            if (len <= 0 || i + len > data.length) break;
            String rec = new String(data, sp + 1, i + len - sp - 2, StandardCharsets.UTF_8);
            int eq = rec.indexOf('=');
            if (eq > 0) m.put(rec.substring(0, eq), rec.substring(eq + 1));
            i += len;
        }
        return m;
    }

    private void untar(InputStream in, File dest) throws IOException {
        String root = dest.getCanonicalPath();
        byte[] h = new byte[512];
        byte[] buf = new byte[1 << 16];
        String longName = null;
        String longLink = null;
        List<String[]> hardLinks = new ArrayList<>();
        List<Object[]> dirModes = new ArrayList<>();
        while (true) {
            if (cancel) throw new IOException("Cancelled");
            readFully(in, h, 512);
            boolean empty = true;
            for (byte b : h)
                if (b != 0) {
                    empty = false;
                    break;
                }
            if (empty) break;
            String name = field(h, 0, 100);
            String prefixField = field(h, 345, 155);
            if (!prefixField.isEmpty() && "ustar".equals(field(h, 257, 5))) name = prefixField + "/" + name;
            int mode = (int) octal(h, 100, 8);
            long size = octal(h, 124, 12);
            char type = (char) h[156];
            String link = field(h, 157, 100);
            if (type == 'L' || type == 'K' || type == 'x' || type == 'g') {
                byte[] data = new byte[(int) size];
                readFully(in, data, (int) size);
                skip(in, (512 - size % 512) % 512);
                if (type == 'L') longName = field(data, 0, data.length);
                if (type == 'K') longLink = field(data, 0, data.length);
                if (type == 'x') {
                    Map<String, String> p = pax(data);
                    if (p.containsKey("path")) longName = p.get("path");
                    if (p.containsKey("linkpath")) longLink = p.get("linkpath");
                }
                continue;
            }
            if (longName != null) name = longName;
            if (longLink != null) link = longLink;
            longName = null;
            longLink = null;
            while (name.startsWith("./")) name = name.substring(2);
            while (name.startsWith("/")) name = name.substring(1);
            File out = new File(dest, name);
            String cp = out.getCanonicalPath();
            boolean inside = name.isEmpty() || cp.equals(root) || cp.startsWith(root + "/");
            if (!inside || name.contains("../")) {
                skip(in, size + (512 - size % 512) % 512);
                continue;
            }
            switch (type) {
                case '5':
                    out.mkdirs();
                    dirModes.add(new Object[] {out, mode});
                    break;
                case '2':
                    out.getParentFile().mkdirs();
                    out.delete();
                    try {
                        Os.symlink(link, out.getPath());
                    } catch (Exception e) {
                        throw new IOException("Couldn't create link " + name);
                    }
                    break;
                case '1':
                    hardLinks.add(new String[] {link, name});
                    break;
                case '0':
                case '\0':
                case '7': {
                    out.getParentFile().mkdirs();
                    try (OutputStream os = new FileOutputStream(out)) {
                        long left = size;
                        while (left > 0) {
                            int n = in.read(buf, 0, (int) Math.min(buf.length, left));
                            if (n < 0) throw new IOException("The runtime package is incomplete");
                            os.write(buf, 0, n);
                            left -= n;
                        }
                    }
                    skip(in, (512 - size % 512) % 512);
                    chmod(out, mode);
                    continue;
                }
                default:
                    break;
            }
            skip(in, size + (512 - size % 512) % 512);
        }
        for (String[] hl : hardLinks) {
            File src = new File(dest, hl[0].startsWith("./") ? hl[0].substring(2) : hl[0]);
            File dst = new File(dest, hl[1]);
            dst.getParentFile().mkdirs();
            try {
                Os.link(src.getPath(), dst.getPath());
            } catch (Exception e) {
                copyFile(src, dst);
            }
        }
        for (Object[] d : dirModes) chmod((File) d[0], ((Integer) d[1]) | 0700);
    }

    private static void chmod(File f, int mode) {
        try {
            Os.chmod(f.getPath(), (mode & 0777) | 0600);
        } catch (Exception ignored) {
        }
    }

    private static void copyFile(File src, File dst) throws IOException {
        try (InputStream in = new FileInputStream(src); OutputStream out = new FileOutputStream(dst)) {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
        dst.setExecutable(src.canExecute(), false);
    }

    static void deleteTree(File f) {
        if (f == null) return;
        try {
            if (Os.lstat(f.getPath()) == null) return;
        } catch (Exception e) {
            return;
        }
        boolean link;
        try {
            link = android.system.OsConstants.S_ISLNK(Os.lstat(f.getPath()).st_mode);
        } catch (Exception e) {
            link = false;
        }
        if (!link && f.isDirectory()) {
            File[] list = f.listFiles();
            if (list != null)
                for (File c : list) deleteTree(c);
        }
        f.delete();
    }

    private Map<String, String> readIni() {
        Map<String, String> m = new HashMap<>();
        File f = new File(runtime, "windroid-runtime.ini");
        try (InputStream in = new FileInputStream(f)) {
            byte[] data = new byte[(int) Math.min(f.length(), 65536)];
            readFully(in, data, data.length);
            for (String line : new String(data, StandardCharsets.UTF_8).split("\n")) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                int eq = line.indexOf('=');
                if (eq > 0) m.put(line.substring(0, eq).trim(), line.substring(eq + 1).trim());
            }
        } catch (IOException ignored) {
        }
        return m;
    }

    private static List<String> splitArgs(String s) {
        List<String> out = new ArrayList<>();
        if (s == null) return out;
        StringBuilder cur = new StringBuilder();
        boolean quote = false;
        boolean any = false;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '"') {
                quote = !quote;
                any = true;
            } else if (c == ' ' && !quote) {
                if (any) out.add(cur.toString());
                cur.setLength(0);
                any = false;
            } else {
                cur.append(c);
                any = true;
            }
        }
        if (any) out.add(cur.toString());
        return out;
    }

    private void linkDrive() {
        File dos = new File(prefix, "dosdevices");
        if (!dos.isDirectory()) return;
        File d = new File(dos, "d:");
        try {
            Os.lstat(d.getPath());
        } catch (Exception missing) {
            try {
                Os.symlink(new File(context.getFilesDir(), "C").getPath(), d.getPath());
            } catch (Exception ignored) {
            }
        }
    }

    int run(String exe, String args) {
        final Job job = new Job();
        int id = nextJob.getAndIncrement();
        jobs.put(id, job);
        if (!box64.isFile()) return fail(job, id, "Box64 is not included in this build of WinDroid.");
        if (!installed()) return fail(job, id, "The Windows compatibility runtime is not installed.");
        Map<String, String> ini = readIni();
        String wine = ini.get("wine");
        if (wine == null || !new File(runtime, wine).isFile()) return fail(job, id, "The runtime package is missing Wine.");
        tmp.mkdirs();
        home.mkdirs();
        prefix.mkdirs();
        linkDrive();
        List<String> cmd = new ArrayList<>();
        cmd.add(box64.getPath());
        cmd.add(new File(runtime, wine).getPath());
        if (exe != null && !exe.isEmpty()) cmd.add(exe);
        cmd.addAll(splitArgs(args));
        ProcessBuilder pb = new ProcessBuilder(cmd);
        File dir = exe == null ? null : new File(exe).getParentFile();
        pb.directory(dir != null && dir.isDirectory() ? dir : home);
        pb.redirectErrorStream(true);
        Map<String, String> env = pb.environment();
        String root = runtime.getPath();
        env.put("HOME", home.getPath());
        env.put("TMPDIR", tmp.getPath());
        env.put("XDG_RUNTIME_DIR", tmp.getPath());
        env.put("WINEPREFIX", prefix.getPath());
        env.put("WINEDEBUG", "-all");
        env.put("WINEDLLOVERRIDES", "mscoree=;mshtml=;winemenubuilder.exe=d");
        env.put("BOX64_NOBANNER", "1");
        env.put("BOX64_LOG", "0");
        env.put("LANG", "C.UTF-8");
        env.remove("DISPLAY");
        for (Map.Entry<String, String> e : ini.entrySet())
            if (e.getKey().startsWith("env.")) env.put(e.getKey().substring(4), e.getValue().replace("@ROOT@", root).replace("@PREFIX@", prefix.getPath()));
        try {
            job.process = pb.start();
        } catch (IOException e) {
            return fail(job, id, "Couldn't start the program: " + e.getMessage());
        }
        new Thread(new Runnable() {
            @Override
            public void run() {
                byte[] buf = new byte[4096];
                try (InputStream in = job.process.getInputStream()) {
                    int n;
                    while ((n = in.read(buf)) > 0) append(job, new String(buf, 0, n, StandardCharsets.UTF_8));
                } catch (IOException ignored) {
                }
                try {
                    job.exit = job.process.waitFor();
                } catch (InterruptedException e) {
                    job.exit = -1;
                }
                job.done = true;
            }
        }, "wine-" + id).start();
        return id;
    }

    private int fail(Job job, int id, String text) {
        append(job, text + "\n");
        job.exit = -1;
        job.done = true;
        return id;
    }

    private static void append(Job job, String s) {
        synchronized (job.out) {
            job.out.append(s);
            if (job.out.length() > MAX_OUTPUT) job.out.delete(0, job.out.length() - MAX_OUTPUT);
        }
    }

    String poll(int id) {
        Job job = jobs.get(id);
        if (job == null) return "0\t-1\t";
        String text;
        boolean finished = job.done;
        synchronized (job.out) {
            text = job.out.toString();
            job.out.setLength(0);
        }
        if (finished && text.isEmpty()) jobs.remove(id);
        return (finished ? 0 : 1) + "\t" + job.exit + "\t" + text;
    }

    void write(int id, String text) {
        Job job = jobs.get(id);
        if (job == null || job.process == null || job.done) return;
        try {
            OutputStream os = job.process.getOutputStream();
            os.write(text.getBytes(StandardCharsets.UTF_8));
            os.flush();
        } catch (IOException ignored) {
        }
    }

    void kill(int id) {
        Job job = jobs.get(id);
        if (job != null && job.process != null) job.process.destroy();
    }

    private static String clean(String s) {
        return s == null ? "" : s.replace('\t', ' ').replace('\n', ' ');
    }
}
