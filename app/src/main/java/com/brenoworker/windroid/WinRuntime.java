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
            "https://github.com/BrenoWorker/windows-on-android/releases/download/runtime-v2/windroid-runtime-arm64.tar.gz";
    private static final int MAX_OUTPUT = 256 * 1024;
    private static final String DISPLAY = ":1";

    private static final class Job {
        Process process;
        final StringBuilder out = new StringBuilder();
        volatile boolean done;
        volatile int exit;
    }

    private final Context context;
    private final File base;
    private final File rootfs;
    private final File tmp;
    private final File shm;
    private final File prootTmp;
    private final File drive;
    private final File libDir;
    private final File proot;
    private final File loader;
    private final Map<Integer, Job> jobs = new ConcurrentHashMap<>();
    private final AtomicInteger nextJob = new AtomicInteger(1);
    private volatile String state = "idle";
    private volatile long done;
    private volatile long total;
    private volatile String message = "";
    private volatile int messageSeq;
    private volatile boolean cancel;
    private volatile Job xvfb;
    private volatile String session = "off";
    private volatile int screenW = 1280;
    private volatile int screenH = 720;

    WinRuntime(Context context) {
        this.context = context;
        base = new File(context.getFilesDir(), "wine");
        rootfs = new File(base, "rootfs");
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
        Map<String, String> ini = installed() ? readIni() : new HashMap<String, String>();
        int progress = total > 0 ? (int) Math.min(1000, done * 1000 / total) : -1;
        String msg = message;
        return (proot.isFile() && loader.isFile() ? 1 : 0) + "\t" + (installed() ? 1 : 0) + "\t" + state + "\t" + progress + "\t"
                + clean(ini.containsKey("version") ? ini.get("version") : "") + "\t" + clean(msg) + "\t" + done + "\t" + messageSeq
                + "\t" + session + "\t" + screenW + "\t" + screenH;
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
                    stopSession();
                    deleteTree(rootfs);
                    deleteTree(new File(base, "runtime"));
                    if (!staging.renameTo(rootfs)) throw new IOException("Couldn't finish the installation");
                    if (!"2".equals(readIni().get("format"))) {
                        deleteTree(rootfs);
                        throw new IOException("This runtime package is from an older version of WinDroid");
                    }
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
        stopSession();
        deleteTree(rootfs);
        deleteTree(new File(base, "runtime"));
        deleteTree(new File(base, "prefix"));
        deleteTree(tmp);
        deleteTree(shm);
        setMessage("Runtime removed");
    }

    void resetPrefix() {
        stopSession();
        deleteTree(new File(rootfs, "root/.wine"));
        setMessage("Windows settings were reset");
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
        File f = new File(rootfs, "windroid-runtime.ini");
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
        File dos = new File(rootfs, "root/.wine/dosdevices");
        if (!dos.isDirectory()) return;
        File d = new File(dos, "d:");
        try {
            Os.lstat(d.getPath());
        } catch (Exception missing) {
            try {
                Os.symlink("/mnt/c", d.getPath());
            } catch (Exception ignored) {
            }
        }
    }

    private String guestPath(String hostPath) {
        String c = drive.getPath();
        if (hostPath.equals(c)) return "/mnt/c";
        if (hostPath.startsWith(c + "/")) return "/mnt/c" + hostPath.substring(c.length());
        String r = rootfs.getPath();
        if (hostPath.startsWith(r + "/")) return hostPath.substring(r.length());
        return hostPath;
    }

    private ProcessBuilder guest(List<String> inner, String cwd) {
        tmp.mkdirs();
        shm.mkdirs();
        prootTmp.mkdirs();
        drive.mkdirs();
        new File(rootfs, "mnt/c").mkdirs();
        new File(rootfs, "root").mkdirs();
        List<String> cmd = new ArrayList<>();
        cmd.add(proot.getPath());
        cmd.add("--kill-on-exit");
        cmd.add("--link2symlink");
        cmd.add("-r");
        cmd.add(rootfs.getPath());
        for (String b : new String[] {"/dev", "/proc", "/sys"}) {
            cmd.add("-b");
            cmd.add(b);
        }
        cmd.add("-b");
        cmd.add(tmp.getPath() + ":/tmp");
        cmd.add("-b");
        cmd.add(shm.getPath() + ":/dev/shm");
        cmd.add("-b");
        cmd.add(drive.getPath() + ":/mnt/c");
        cmd.add("-w");
        cmd.add(cwd == null ? "/root" : cwd);
        cmd.add("/usr/bin/env");
        cmd.add("-i");
        cmd.add("HOME=/root");
        cmd.add("USER=root");
        cmd.add("PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin");
        cmd.add("TMPDIR=/tmp");
        cmd.add("DISPLAY=" + DISPLAY);
        cmd.add("LANG=C.UTF-8");
        cmd.add("WINEPREFIX=/root/.wine");
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
        return null;
    }

    private Job spawn(int id, ProcessBuilder pb) {
        final Job job = new Job();
        jobs.put(id, job);
        try {
            job.process = pb.start();
        } catch (IOException e) {
            append(job, "Couldn't start: " + e.getMessage() + "\n");
            job.exit = -1;
            job.done = true;
            return job;
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
        }, "guest-" + id).start();
        return job;
    }

    synchronized void startSession(int w, int h) {
        if (xvfb != null && !xvfb.done && (session.equals("ready") || session.equals("starting"))) return;
        if (check() != null || !proot.canExecute() && !proot.setExecutable(true)) {
            session = "failed";
            setMessage(check() == null ? "Couldn't start the Windows display" : check());
            return;
        }
        screenW = Math.max(640, Math.min(w, 2560)) & ~7;
        screenH = Math.max(400, Math.min(h, 1600)) & ~7;
        session = "starting";
        new File(tmp, ".X1-lock").delete();
        new File(tmp, ".X11-unix/X1").delete();
        new File(tmp, "Xvfb_screen0").delete();
        new File(tmp, ".X11-unix").mkdirs();
        Map<String, String> ini = readIni();
        String server = ini.containsKey("xvfb") ? ini.get("xvfb") : "/usr/bin/Xvfb";
        List<String> inner = new ArrayList<>();
        inner.add(server);
        inner.add(DISPLAY);
        inner.add("-screen");
        inner.add("0");
        inner.add(screenW + "x" + screenH + "x24");
        inner.add("-fbdir");
        inner.add("/tmp");
        inner.add("-nolisten");
        inner.add("tcp");
        inner.add("-ac");
        inner.add("-dpi");
        inner.add("96");
        final int id = nextJob.getAndIncrement();
        final Job job = spawn(id, guest(inner, "/root"));
        xvfb = job;
        new Thread(new Runnable() {
            @Override
            public void run() {
                File sock = new File(tmp, ".X11-unix/X1");
                File fb = new File(tmp, "Xvfb_screen0");
                for (int i = 0; i < 300 && !job.done; i++) {
                    if (sock.exists() && fb.isFile() && fb.length() > 0) {
                        session = "ready";
                        return;
                    }
                    try {
                        Thread.sleep(100);
                    } catch (InterruptedException e) {
                        return;
                    }
                }
                session = "failed";
                String log;
                synchronized (job.out) {
                    log = job.out.toString().trim();
                }
                setMessage("The Windows display didn't start" + (log.isEmpty() ? "" : ": " + log.substring(Math.max(0, log.length() - 200))));
                if (job.process != null) job.process.destroy();
            }
        }, "xvfb-wait").start();
    }

    synchronized void stopSession() {
        for (Job j : jobs.values())
            if (j.process != null && !j.done) j.process.destroy();
        Job x = xvfb;
        if (x != null && x.process != null) x.process.destroy();
        xvfb = null;
        session = "off";
    }

    int run(String exe, String args, boolean gui) {
        int id = nextJob.getAndIncrement();
        String problem = check();
        if (problem != null) {
            Job job = new Job();
            jobs.put(id, job);
            return fail(job, id, problem);
        }
        Map<String, String> ini = readIni();
        String wine = ini.get("wine");
        String box64 = ini.containsKey("box64") ? ini.get("box64") : "/usr/local/bin/box64";
        if (wine == null) {
            Job job = new Job();
            jobs.put(id, job);
            return fail(job, id, "The runtime package is missing Wine.");
        }
        linkDrive();
        List<String> inner = new ArrayList<>();
        inner.add(box64);
        inner.add(wine);
        if (gui) {
            inner.add("explorer");
            inner.add("/desktop=WinDroid," + screenW + "x" + screenH);
            inner.add("start");
            inner.add("/unix");
        }
        String cwd = "/root";
        if (exe != null && !exe.isEmpty()) {
            inner.add(guestPath(exe));
            File parent = new File(exe).getParentFile();
            if (parent != null) cwd = guestPath(parent.getPath());
        }
        inner.addAll(splitArgs(args));
        spawn(id, guest(inner, cwd));
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
