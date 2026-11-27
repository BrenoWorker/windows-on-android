package com.brenoworker.windroid;

import android.content.Context;
import android.os.SystemClock;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InterruptedIOException;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.zip.GZIPInputStream;

final class WinRuntime {

    static final String DEFAULT_URL =
            "https://github.com/BrenoWorker/windows-on-android/releases/download/runtime-v3/windroid-runtime-arm64.tar.gz";
    private static final int MAX_CHUNK = 64 * 1024;
    private static final int MAX_LOG = 16 * 1024;
    private static final String DISPLAY = ":1";
    private static final String PREFIX = "/root/.wine";
    private static final long LAUNCH_GRACE = 25000;
    private static final Set<String> LOADERS = new HashSet<>();
    private static final Set<String> SYSTEM = new HashSet<>();

    static {
        for (String s : new String[] {"box64", "wine", "wine64", "wine-preloader", "wine64-preloader"}) LOADERS.add(s);
        for (String s : new String[] {"explorer.exe", "start.exe", "services.exe", "winedevice.exe", "plugplay.exe", "svchost.exe",
                "rpcss.exe", "conhost.exe", "wineboot.exe", "winemenubuilder.exe", "tabtip.exe", "wineserver"}) {
            SYSTEM.add(s);
        }
    }

    private static final class Job {
        boolean console;
        long started;
        long offset;
        String pending = "";
        volatile boolean done;
        volatile int exit;
        Session session;
    }

    private static final class Session {
        Process process;
        OutputStream commands;
        final StringBuilder log = new StringBuilder();
        volatile boolean stopping;
        volatile boolean ready;
    }

    private final Context context;
    private final File base;
    private final File rootfs;
    private final File prefix;
    private final File tmp;
    private final File shm;
    private final File prootTmp;
    private final File drive;
    private final File libDir;
    private final File proot;
    private final File loader;
    private final Map<Integer, Job> jobs = new ConcurrentHashMap<>();
    private final AtomicInteger nextJob = new AtomicInteger(1);
    private final ExecutorService input = Executors.newSingleThreadExecutor();
    private volatile String state = "idle";
    private volatile long done;
    private volatile long total;
    private volatile String message = "";
    private volatile int messageSeq;
    private volatile boolean cancel;
    private volatile Session current;
    private Session retiring;
    private volatile String session = "off";
    private volatile int screenW = 1280;
    private volatile int screenH = 720;

    WinRuntime(Context context) {
        this.context = context;
        base = new File(context.getFilesDir(), "wine");
        rootfs = new File(base, "rootfs");
        prefix = new File(base, "prefix");
        tmp = new File(base, "tmp");
        shm = new File(base, "shm");
        prootTmp = new File(base, "proot-tmp");
        drive = new File(context.getFilesDir(), "C");
        libDir = new File(context.getApplicationInfo().nativeLibraryDir);
        proot = new File(libDir, "libproot.so");
        loader = new File(libDir, "libproot-loader.so");
    }

    private void setMessage(String m) {
        message = m == null ? "" : m;
        messageSeq++;
    }

    boolean installed() {
        return new File(rootfs, "windroid-runtime.ini").isFile();
    }

    private boolean busy() {
        return state.equals("downloading") || state.equals("extracting");
    }

    String status() {
        Map<String, String> ini = installed() ? readIni(rootfs) : new HashMap<String, String>();
        int progress = total > 0 ? (int) Math.min(1000, done * 1000 / total) : -1;
        String msg = message;
        return (proot.isFile() && loader.isFile() ? 1 : 0) + "\t" + (installed() ? 1 : 0) + "\t" + state + "\t" + progress + "\t"
                + clean(ini.containsKey("version") ? ini.get("version") : "") + "\t" + clean(msg) + "\t" + done + "\t" + messageSeq
                + "\t" + session + "\t" + screenW + "\t" + screenH + "\t" + ("1".equals(ini.get("wow64")) ? 1 : 0);
    }

    void install(final String source) {
        if (busy()) return;
        cancel = false;
        state = "downloading";
        done = 0;
        total = 0;
        message = "";
        new Thread(new Runnable() {
            @Override
            public void run() {
                File staging = new File(base, "rootfs.new");
                try {
                    deleteTree(staging);
                    staging.mkdirs();
                    InputStream raw = open(source == null || source.isEmpty() ? DEFAULT_URL : source);
                    try (InputStream in = new GZIPInputStream(new BufferedInputStream(raw, 1 << 16), 1 << 16)) {
                        state = "extracting";
                        untar(in, staging);
                    }
                    if (cancel) throw new IOException("Cancelled");
                    File ini = new File(staging, "windroid-runtime.ini");
                    if (!ini.isFile()) throw new IOException("This file is not a WinDroid runtime package");
                    String format = readIni(staging).get("format");
                    if (!"2".equals(format) && !"3".equals(format)) {
                        throw new IOException("This runtime package isn't compatible with this version of WinDroid");
                    }
                    shutdownSession();
                    migratePrefix();
                    deleteTree(rootfs);
                    deleteTree(new File(base, "runtime"));
                    if (!staging.renameTo(rootfs)) throw new IOException("Couldn't finish the installation");
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
        if (busy()) return;
        shutdownSession();
        deleteTree(rootfs);
        deleteTree(new File(base, "runtime"));
        deleteTree(prefix);
        deleteTree(tmp);
        deleteTree(shm);
        setMessage("Runtime removed");
    }

    void resetPrefix() {
        shutdownSession();
        deleteTree(new File(rootfs, "root/.wine"));
        deleteTree(prefix);
        setMessage("Windows settings were reset");
    }

    private synchronized void migratePrefix() {
        File old = new File(rootfs, "root/.wine");
        if (!prefix.exists() && new File(old, "system.reg").isFile()) old.renameTo(prefix);
    }

    String display() {
        return session + "\t" + new File(tmp, ".X11-unix/X1").getPath() + "\t" + new File(tmp, "Xvfb_screen0").getPath() + "\t" + screenW
                + "\t" + screenH;
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

    private static boolean isLink(File f) {
        try {
            return OsConstants.S_ISLNK(Os.lstat(f.getPath()).st_mode);
        } catch (ErrnoException e) {
            return false;
        }
    }

    static void deleteTree(File f) {
        if (f == null) return;
        try {
            Os.lstat(f.getPath());
        } catch (ErrnoException e) {
            return;
        }
        if (!isLink(f) && f.isDirectory()) {
            File[] list = f.listFiles();
            if (list != null)
                for (File c : list) deleteTree(c);
        }
        f.delete();
    }

    private static Map<String, String> readIni(File root) {
        Map<String, String> m = new HashMap<>();
        File f = new File(root, "windroid-runtime.ini");
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

    private String guestPath(String hostPath) {
        if (hostPath.startsWith("wine:")) return hostPath.substring(5);
        String c = drive.getPath();
        if (hostPath.equals(c)) return "/mnt/c";
        if (hostPath.startsWith(c + "/")) return "/mnt/c" + hostPath.substring(c.length());
        String p = prefix.getPath();
        if (hostPath.startsWith(p + "/")) return PREFIX + hostPath.substring(p.length());
        String r = rootfs.getPath();
        if (hostPath.startsWith(r + "/")) return hostPath.substring(r.length());
        return hostPath;
    }

    private ProcessBuilder guest(List<String> inner) {
        migratePrefix();
        tmp.mkdirs();
        shm.mkdirs();
        prootTmp.mkdirs();
        drive.mkdirs();
        prefix.mkdirs();
        new File(rootfs, "mnt/c").mkdirs();
        new File(rootfs, "root/.wine").mkdirs();
        List<String> cmd = new ArrayList<>();
        cmd.add(proot.getPath());
        cmd.add("--kill-on-exit");
        cmd.add("--link2symlink");
        cmd.add("-r");
        cmd.add(rootfs.getPath());
        for (String b : new String[] {"/dev", "/proc", "/sys", tmp.getPath() + ":/tmp", shm.getPath() + ":/dev/shm",
                drive.getPath() + ":/mnt/c", prefix.getPath() + ":" + PREFIX}) {
            cmd.add("-b");
            cmd.add(b);
        }
        cmd.add("-w");
        cmd.add("/root");
        cmd.add("/usr/bin/env");
        cmd.add("-i");
        cmd.add("HOME=/root");
        cmd.add("USER=root");
        cmd.add("PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin");
        cmd.add("TMPDIR=/tmp");
        cmd.add("DISPLAY=" + DISPLAY);
        cmd.add("LANG=C.UTF-8");
        cmd.add("WINEPREFIX=" + PREFIX);
        cmd.add("WINEDEBUG=-all");
        cmd.add("WINEDLLOVERRIDES=mscoree=;mshtml=;winemenubuilder.exe=d");
        cmd.add("BOX64_NOBANNER=1");
        cmd.add("BOX64_LOG=0");
        cmd.addAll(inner);
        ProcessBuilder pb = new ProcessBuilder(cmd);
        pb.directory(base);
        pb.redirectErrorStream(true);
        Map<String, String> env = pb.environment();
        env.put("LD_LIBRARY_PATH", libDir.getPath());
        env.put("PROOT_LOADER", loader.getPath());
        env.put("PROOT_TMP_DIR", prootTmp.getPath());
        env.remove("LD_PRELOAD");
        return pb;
    }

    private String check() {
        if (!proot.isFile() || !loader.isFile()) return "proot is not included in this build of WinDroid.";
        if (!installed()) return "The Windows compatibility runtime is not installed.";
        if (readIni(rootfs).get("wine") == null) return "The runtime package is missing Wine.";
        return null;
    }

    private static boolean alive(Session s) {
        if (s == null || s.process == null || s.stopping) return false;
        try {
            s.process.exitValue();
            return false;
        } catch (IllegalThreadStateException running) {
            return true;
        }
    }

    private File control() {
        return new File(tmp, "windroid");
    }

    private File jobFile(int id, String suffix) {
        return new File(control(), "jobs/" + id + suffix);
    }

    private void writeScript() throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (InputStream in = context.getAssets().open("runtime/session.sh")) {
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) bytes.write(buf, 0, n);
        }
        String script = new String(bytes.toByteArray(), StandardCharsets.UTF_8).replace("\r", "");
        write(new File(control(), "session.sh"), script);
    }

    private static void write(File f, String text) throws IOException {
        try (OutputStream os = new FileOutputStream(f)) {
            os.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    synchronized void startSession(int w, int h) {
        if (alive(current)) return;
        String problem = check();
        if (problem == null && !proot.canExecute() && !proot.setExecutable(true)) problem = "Couldn't start the Windows runtime";
        if (problem != null) {
            session = "failed";
            setMessage(problem);
            return;
        }
        shutdownSession();
        screenW = Math.max(640, Math.min(w, 2560)) & ~7;
        screenH = Math.max(400, Math.min(h, 1600)) & ~7;
        session = "starting";
        new File(tmp, ".X1-lock").delete();
        new File(tmp, ".X11-unix/X1").delete();
        new File(tmp, "Xvfb_screen0").delete();
        deleteTree(control());
        new File(control(), "jobs").mkdirs();
        new File(tmp, ".X11-unix").mkdirs();
        final Session s = new Session();
        try {
            writeScript();
            Map<String, String> ini = readIni(rootfs);
            String wine = ini.get("wine");
            List<String> inner = new ArrayList<>();
            inner.add("/bin/sh");
            inner.add("/tmp/windroid/session.sh");
            inner.add(screenW + "x" + screenH);
            inner.add(wine);
            inner.add(ini.containsKey("wineserver") ? ini.get("wineserver") : new File(wine).getParent() + "/wineserver");
            inner.add(ini.containsKey("box64") ? ini.get("box64") : "/usr/local/bin/box64");
            inner.add(ini.containsKey("xvfb") ? ini.get("xvfb") : "/usr/bin/Xvfb");
            s.process = guest(inner).start();
            s.commands = s.process.getOutputStream();
        } catch (IOException e) {
            session = "failed";
            setMessage("Couldn't start Windows: " + e.getMessage());
            return;
        }
        current = s;
        new Thread(() -> {
            byte[] buf = new byte[4096];
            try (InputStream in = s.process.getInputStream()) {
                int n;
                while ((n = in.read(buf)) > 0) {
                    synchronized (s.log) {
                        s.log.append(new String(buf, 0, n, StandardCharsets.UTF_8));
                        if (s.log.length() > MAX_LOG) s.log.delete(0, s.log.length() - MAX_LOG);
                    }
                }
            } catch (IOException ignored) {
            }
        }, "windows-log").start();
        new Thread(() -> watch(s), "windows-session").start();
    }

    private void watch(Session s) {
        File stateFile = new File(control(), "state");
        while (alive(s) && current == s) {
            String now = firstLine(stateFile);
            if (now.equals("setup") || now.equals("ready") || now.equals("starting")) {
                if (current == s) session = now;
                if (now.equals("ready")) s.ready = true;
            }
            SystemClock.sleep(200);
        }
        if (current != s) return;
        try {
            s.process.waitFor();
        } catch (InterruptedException e) {
            return;
        }
        SystemClock.sleep(150);
        synchronized (this) {
            if (current != s) return;
            current = null;
            if (s.stopping) {
                session = "off";
                return;
            }
            session = "failed";
            String log;
            synchronized (s.log) {
                log = s.log.toString().trim();
            }
            setMessage((s.ready ? "Windows stopped unexpectedly" : "Windows didn't start")
                    + (log.isEmpty() ? "" : ": " + log.substring(Math.max(0, log.length() - 240))));
        }
    }

    private static String firstLine(File f) {
        if (!f.isFile()) return "";
        try (InputStream in = new FileInputStream(f)) {
            byte[] buf = new byte[64];
            int n = in.read(buf);
            if (n <= 0) return "";
            return new String(buf, 0, n, StandardCharsets.UTF_8).trim();
        } catch (IOException e) {
            return "";
        }
    }

    private synchronized boolean send(Session s, String line) {
        if (s == null || s.commands == null) return false;
        try {
            s.commands.write((line + "\n").getBytes(StandardCharsets.UTF_8));
            s.commands.flush();
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    synchronized void stopSession() {
        Session s = current;
        if (s == null) {
            session = "off";
            return;
        }
        boolean console = false;
        for (Map.Entry<Integer, Job> en : jobs.entrySet()) {
            Job j = en.getValue();
            if (j.session != s || j.done || jobFile(en.getKey(), ".exit").isFile()) continue;
            if (j.console) console = true;
            else send(s, "kill " + en.getKey());
        }
        if (!console) retire();
    }

    private synchronized void retire() {
        final Session s = current;
        current = null;
        session = "off";
        if (s == null) return;
        send(s, "quit");
        s.stopping = true;
        try {
            s.commands.close();
        } catch (IOException ignored) {
        }
        for (Job j : jobs.values()) {
            if (j.session != s || j.done) continue;
            j.exit = -1;
            j.done = true;
        }
        retiring = s;
        new Thread(() -> reap(s, 5000), "windows-stop").start();
    }

    private static void reap(Session s, long grace) {
        long until = SystemClock.uptimeMillis() + grace;
        while (SystemClock.uptimeMillis() < until) {
            try {
                s.process.exitValue();
                return;
            } catch (IllegalThreadStateException running) {
                SystemClock.sleep(50);
            }
        }
        s.process.destroy();
    }

    synchronized void shutdownSession() {
        retire();
        if (retiring != null) reap(retiring, 2500);
        retiring = null;
    }

    private static String quote(String s) {
        return "'" + s.replace("'", "'\\''") + "'";
    }

    private static String suffix(String path) {
        int dot = path.lastIndexOf('.');
        return dot < 0 || dot < path.lastIndexOf('/') ? "" : path.substring(dot + 1).toLowerCase(Locale.ROOT);
    }

    int run(String exe, String args, boolean gui) {
        int id = nextJob.getAndIncrement();
        String problem = check();
        if (problem != null) return fail(id, problem);
        if (!alive(current)) startSession(screenW, screenH);
        Session s = current;
        if (s == null) return fail(id, message.isEmpty() ? "Couldn't start Windows." : message);
        Map<String, String> ini = readIni(rootfs);
        List<String> inner = new ArrayList<>();
        inner.add(ini.containsKey("box64") ? ini.get("box64") : "/usr/local/bin/box64");
        inner.add(ini.get("wine"));
        if (gui) {
            inner.add("explorer");
            inner.add("/desktop=WinDroid," + screenW + "x" + screenH);
        }
        String cwd = "/root";
        if (exe != null && !exe.isEmpty()) {
            if (exe.indexOf('/') < 0) {
                inner.add(exe);
            } else {
                String path = guestPath(exe);
                String windows = "Z:" + path.replace('/', '\\');
                String type = suffix(path);
                if (type.equals("msi")) {
                    inner.add("msiexec");
                    inner.add("/i");
                    inner.add(windows);
                } else if (type.equals("bat") || type.equals("cmd")) {
                    inner.add("cmd");
                    inner.add("/c");
                    inner.add(windows);
                } else if (gui) {
                    inner.add("start");
                    inner.add("/unix");
                    inner.add(path);
                } else {
                    inner.add(path);
                }
                int slash = path.lastIndexOf('/');
                if (slash > 0) cwd = path.substring(0, slash);
            }
        }
        inner.addAll(splitArgs(args));
        StringBuilder script = new StringBuilder("cd " + quote(cwd) + " 2>/dev/null\nexec");
        for (String a : inner) script.append(' ').append(quote(a));
        script.append('\n');
        Job job = new Job();
        job.console = !gui;
        job.session = s;
        job.started = SystemClock.uptimeMillis();
        try {
            write(jobFile(id, ".sh"), script.toString());
        } catch (IOException e) {
            return fail(id, "Couldn't start: " + e.getMessage());
        }
        jobs.put(id, job);
        if (!send(s, "run " + id + (gui ? " gui" : " console"))) return fail(id, "Windows is not responding.");
        return id;
    }

    private int fail(int id, String text) {
        Job job = new Job();
        job.pending = text + "\n";
        job.exit = -1;
        job.done = true;
        jobs.put(id, job);
        return id;
    }

    private String readOutput(int id, Job job) {
        if (job.session == null) return "";
        File log = jobFile(id, ".log");
        if (!log.isFile()) return "";
        try (RandomAccessFile in = new RandomAccessFile(log, "r")) {
            long length = in.length();
            if (length <= job.offset) return "";
            byte[] data = new byte[(int) Math.min(MAX_CHUNK, length - job.offset)];
            in.seek(job.offset);
            in.readFully(data);
            int end = data.length;
            int lead = end - 1;
            while (lead >= 0 && end - lead < 4 && (data[lead] & 0xC0) == 0x80) lead--;
            if (lead >= 0 && (data[lead] & 0xC0) == 0xC0) {
                int b = data[lead] & 0xFF;
                int need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
                if (end - lead < need && !job.done) end = lead;
            }
            job.offset += end;
            return new String(data, 0, end, StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "";
        }
    }

    synchronized String poll(int id) {
        Job job = jobs.get(id);
        if (job == null) return "0\t-1\t";
        if (!job.done) {
            File exit = jobFile(id, ".exit");
            if (job.session != current) {
                job.exit = -1;
                job.done = true;
            } else if (exit.isFile()) {
                try {
                    job.exit = Integer.parseInt(firstLine(exit));
                } catch (NumberFormatException e) {
                    job.exit = -1;
                }
                job.done = true;
            }
        }
        boolean finished = job.done;
        String text = job.pending + readOutput(id, job);
        job.pending = "";
        if (finished && text.isEmpty()) jobs.remove(id);
        return (finished ? 0 : 1) + "\t" + job.exit + "\t" + text;
    }

    void write(final int id, final String text) {
        final Job job = jobs.get(id);
        if (job == null || job.session == null || job.done || !job.console) return;
        input.execute(() -> {
            File pid = jobFile(id, ".pid");
            while (!pid.isFile()) {
                if (job.done || job.session != current) return;
                SystemClock.sleep(100);
            }
            if (jobFile(id, ".exit").isFile()) return;
            try {
                FileDescriptor fd = Os.open(jobFile(id, ".in").getPath(), OsConstants.O_RDWR | OsConstants.O_NONBLOCK, 0);
                try {
                    byte[] data = text.getBytes(StandardCharsets.UTF_8);
                    Os.write(fd, data, 0, data.length);
                } finally {
                    Os.close(fd);
                }
            } catch (ErrnoException | InterruptedIOException ignored) {
            }
        });
    }

    synchronized void kill(int id) {
        Job job = jobs.get(id);
        if (job != null && !job.done && job.session == current) send(current, "kill " + id);
    }

    private static String programName(String token) {
        int cut = Math.max(token.lastIndexOf('/'), token.lastIndexOf('\\'));
        return token.substring(cut + 1).toLowerCase(Locale.ROOT);
    }

    static boolean isUserProgram(String[] argv) {
        if (argv.length == 0) return false;
        int i = 0;
        boolean windows = argv[0].length() > 2 && argv[0].charAt(1) == ':' && argv[0].charAt(2) == '\\';
        if (!windows) {
            while (i < argv.length && i < 3 && !LOADERS.contains(programName(argv[i]))) i++;
            if (i >= argv.length || i >= 3) return false;
            while (i < argv.length && LOADERS.contains(programName(argv[i]))) i++;
            if (i >= argv.length) return false;
        }
        String name = programName(argv[i]);
        if (name.equals("msiexec") || name.equals("msiexec.exe")) {
            for (int j = i + 1; j < argv.length; j++) if (argv[j].equalsIgnoreCase("/i")) return true;
            return false;
        }
        return name.endsWith(".exe") && !SYSTEM.contains(name);
    }

    private static int countPrograms() {
        File[] list = new File("/proc").listFiles();
        if (list == null) return 0;
        int count = 0;
        byte[] buf = new byte[4096];
        for (File f : list) {
            String n = f.getName();
            if (n.isEmpty() || n.charAt(0) < '0' || n.charAt(0) > '9') continue;
            try (InputStream in = new FileInputStream(new File(f, "cmdline"))) {
                int len = in.read(buf);
                if (len > 0 && isUserProgram(new String(buf, 0, len, StandardCharsets.UTF_8).split("\0"))) count++;
            } catch (IOException ignored) {
            }
        }
        return count;
    }

    int running() {
        Session s = current;
        if (s == null) return 0;
        int launching = 0;
        long now = SystemClock.uptimeMillis();
        for (Map.Entry<Integer, Job> en : jobs.entrySet()) {
            Job j = en.getValue();
            if (j.session == s && !j.done && !j.console && (!s.ready || now - j.started < LAUNCH_GRACE)) launching++;
        }
        return Math.max(launching, countPrograms());
    }

    private static boolean helperEntry(String name) {
        String n = name.toLowerCase(Locale.ROOT);
        for (String s : new String[] {"uninstall", "desinstal", "readme", "read me", "help", "website", "license", "manual"}) {
            if (n.contains(s)) return true;
        }
        return false;
    }

    private void collect(File dir, Map<String, String> out, int depth) {
        File[] list = dir.listFiles();
        if (list == null || depth > 4) return;
        for (File f : list) {
            if (isLink(f)) continue;
            if (f.isDirectory()) {
                collect(f, out, depth + 1);
                continue;
            }
            String n = f.getName();
            if (!n.toLowerCase(Locale.ROOT).endsWith(".lnk") || out.size() >= 200) continue;
            String name = n.substring(0, n.length() - 4);
            if (!helperEntry(name) && !out.containsKey(name)) out.put(name, "wine:" + guestPath(f.getPath()));
        }
    }

    String apps() {
        migratePrefix();
        Map<String, String> found = new LinkedHashMap<>();
        File c = new File(prefix, "drive_c");
        collect(new File(c, "ProgramData/Microsoft/Windows/Start Menu/Programs"), found, 0);
        File[] users = new File(c, "users").listFiles();
        if (users != null) {
            for (File u : users) collect(new File(u, "AppData/Roaming/Microsoft/Windows/Start Menu/Programs"), found, 0);
            for (File u : users) {
                File desktop = new File(u, "Desktop");
                if (!isLink(desktop)) collect(desktop, found, 0);
            }
        }
        StringBuilder sb = new StringBuilder("ok\n");
        for (Map.Entry<String, String> en : found.entrySet()) {
            sb.append(clean(en.getKey())).append('\t').append(clean(en.getValue())).append('\n');
        }
        return sb.toString();
    }

    private static String clean(String s) {
        return s == null ? "" : s.replace('\t', ' ').replace('\n', ' ');
    }
}
