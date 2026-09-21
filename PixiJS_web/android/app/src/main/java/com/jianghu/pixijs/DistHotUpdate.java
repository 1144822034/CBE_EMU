package com.jianghu.pixijs;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.ViewGroup;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;
import android.widget.FrameLayout;
import android.widget.TextView;

import com.getcapacitor.Bridge;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Startup dist hot update.
 *
 * The APK assets stay the baseline. On each cold start the client fetches
 * {@code BASE_URL/hot-manifest.json} (sha256 per relative path, written by
 * {@code scripts/write-hot-manifest.mjs} into dist). Paths whose hash differs
 * are downloaded into filesDir and served ahead of the bundled copy.
 * Network failure keeps the last good overlay plus the APK assets.
 */
public final class DistHotUpdate {
    private static final String TAG = "DistHotUpdate";
    /** Published Vite dist root (index.html, assets/, JHOnlineData/, hot-manifest.json). */
    public static final String BASE_URL = "http://106.54.59.92:8081";
    private static final String MANIFEST_NAME = "hot-manifest.json";
    private static final String ASSET_MANIFEST = "public/" + MANIFEST_NAME;
    private static final int MANIFEST_MAX_BYTES = 12 * 1024 * 1024;
    private static final int CONNECT_TIMEOUT_MS = 10000;
    private static final int MANIFEST_READ_TIMEOUT_MS = 20000;
    private static final int FILE_READ_TIMEOUT_MS = 90000;
    private static final int DOWNLOAD_PARALLELISM = 3;
    private static final long OVERLAY_HIDE_FALLBACK_MS = 20000L;

    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static final AtomicBoolean STARTED = new AtomicBoolean(false);

    private static volatile Map<String, String> serveIndex = Collections.emptyMap();
    private static volatile boolean bootReady = false;
    private static File filesRoot;
    private static FrameLayout overlay;
    private static TextView statusView;

    private DistHotUpdate() {}

    /** Drop overlay files when versionCode changes so the new APK is the baseline. */
    public static void wipe(Activity activity) {
        deleteRecursive(new File(activity.getFilesDir(), "dist-hot"));
        serveIndex = Collections.emptyMap();
    }

    /**
     * Call once after the Capacitor bridge exists, before the first page is allowed to run.
     * Stops the automatic load, syncs, then loads the app URL.
     */
    public static void start(MainActivity activity) {
        if (!STARTED.compareAndSet(false, true)) return;
        Bridge bridge = activity.getBridge();
        WebView webView = bridge != null ? bridge.getWebView() : null;
        if (webView == null) {
            Log.w(TAG, "no webview; skip hot update");
            return;
        }
        try {
            webView.stopLoading();
            // Replace the queued app load so bundled JS cannot run before the sync finishes.
            webView.loadUrl("about:blank");
        } catch (Throwable t) {
            Log.w(TAG, "stopLoading failed", t);
        }
        showStatus(activity, "正在检查资源…");
        ExecutorService worker = Executors.newSingleThreadExecutor();
        worker.execute(() -> {
            String status = null;
            try {
                int downloaded = sync(activity);
                if (downloaded > 0) status = "资源已更新";
            } catch (Throwable t) {
                Log.w(TAG, "hot update failed; using local assets", t);
                status = statusForFailure(t);
            } finally {
                worker.shutdown();
            }
            final String banner = status;
            activity.runOnUiThread(() -> finishBoot(activity, webView, banner));
        });
    }

    public static void onPageFinished(String url) {
        if (!bootReady || url == null || url.startsWith("about:")) return;
        MAIN.post(DistHotUpdate::hideOverlay);
    }

    public static void onHostDestroy() {
        hideOverlay();
    }

    /**
     * Prefer a verified hot file for the app origin. Null lets Capacitor serve the APK asset.
     */
    public static WebResourceResponse intercept(Bridge bridge, WebResourceRequest request) {
        try {
            if (bridge == null || request == null || request.getUrl() == null) return null;
            String method = request.getMethod();
            if (method != null && !method.equalsIgnoreCase("GET")) return null;
            android.net.Uri uri = request.getUrl();
            String host = uri.getHost();
            if (host == null || !host.equalsIgnoreCase(bridge.getHost())) return null;
            String scheme = uri.getScheme();
            if (scheme == null || !scheme.equalsIgnoreCase(bridge.getScheme())) return null;
            String rel = relativePath(uri.getPath());
            if (rel == null || rel.startsWith("_capacitor")) return null;
            Map<String, String> index = serveIndex;
            if (!index.containsKey(rel)) return null;
            File root = filesRoot;
            if (root == null) return null;
            File file = new File(root, rel);
            if (!file.isFile()) return null;
            String mime = mimeType(rel);
            InputStream in = new FileInputStream(file);
            if (rel.endsWith(".html")) {
                in = bridge.getLocalServer().getJavaScriptInjectedStream(in);
            }
            Map<String, String> headers = new HashMap<>();
            headers.put("Cache-Control", "no-cache");
            // Omit Content-Length: HTML may be prefixed with the Capacitor bootstrap.
            String encoding = isText(rel) ? "utf-8" : null;
            return new WebResourceResponse(mime, encoding, 200, "OK", headers, in);
        } catch (Throwable t) {
            Log.w(TAG, "intercept failed", t);
            return null;
        }
    }

    private static void finishBoot(MainActivity activity, WebView webView, String banner) {
        if (activity.isFinishing()) return;
        bootReady = true;
        if (banner != null) showStatus(activity, banner);
        try {
            Bridge bridge = activity.getBridge();
            String url = bridge != null ? bridge.getAppUrl() : null;
            if (url != null) webView.loadUrl(url);
        } catch (Throwable t) {
            Log.w(TAG, "loadUrl failed", t);
        }
        MAIN.postDelayed(DistHotUpdate::hideOverlay, OVERLAY_HIDE_FALLBACK_MS);
    }

    /** @return number of files downloaded */
    private static int sync(Activity activity) throws Exception {
        File hotRoot = new File(activity.getFilesDir(), "dist-hot");
        File root = new File(hotRoot, "files");
        File staging = new File(hotRoot, "staging");
        ensureDir(root);
        filesRoot = root;
        deleteRecursive(staging);
        ensureDir(staging);

        Map<String, String> bundled = readAssetManifest(activity);
        if (bundled.isEmpty()) {
            Log.w(TAG, "bundled hot-manifest missing; skip download");
            serveIndex = Collections.emptyMap();
            return 0;
        }
        Map<String, String> applied = readApplied(new File(hotRoot, "applied.json"));
        healApplied(root, applied);
        writeApplied(new File(hotRoot, "applied.json"), applied);
        serveIndex = Collections.unmodifiableMap(new HashMap<>(applied));

        Map<String, String> remote = fetchRemoteManifest();
        if (remote.isEmpty()) {
            Log.w(TAG, "remote hot-manifest empty; keep local");
            return 0;
        }

        List<String> toDownload = new ArrayList<>();
        for (Map.Entry<String, String> entry : remote.entrySet()) {
            String rel = entry.getKey();
            if (!isSafeRelative(rel) || MANIFEST_NAME.equals(rel)) continue;
            String want = entry.getValue();
            if (want == null || want.isEmpty()) continue;
            // Matches packaged asset → keep APK copy; prune any stale overlay later.
            if (want.equalsIgnoreCase(bundled.get(rel))) continue;
            String have = null;
            if (applied.containsKey(rel) && new File(root, rel).isFile()) {
                have = applied.get(rel);
            }
            if (have == null) have = bundled.get(rel);
            if (!want.equalsIgnoreCase(have)) toDownload.add(rel);
        }
        if (toDownload.isEmpty() && sameOverlay(applied, bundled, remote)) {
            Log.i(TAG, "dist matches hot manifest (" + remote.size() + " files)");
            return 0;
        }

        if (!toDownload.isEmpty()) {
            Log.i(TAG, "downloading " + toDownload.size() + " file(s) from " + BASE_URL);
            downloadAll(activity, staging, remote, toDownload);
        } else {
            Log.i(TAG, "pruning overlay to match remote (" + remote.size() + " files)");
        }

        Map<String, String> next = new HashMap<>();
        for (Map.Entry<String, String> entry : remote.entrySet()) {
            String rel = entry.getKey();
            if (!isSafeRelative(rel) || MANIFEST_NAME.equals(rel)) continue;
            String want = entry.getValue();
            String bundledHash = bundled.get(rel);
            if (want.equalsIgnoreCase(bundledHash)) continue;
            File staged = new File(staging, rel);
            File dest = new File(root, rel);
            if (staged.isFile()) {
                moveReplace(staged, dest);
            } else if (!(dest.isFile() && want.equalsIgnoreCase(applied.get(rel)))) {
                throw new IOException("missing staged file " + rel);
            }
            next.put(rel, want.toLowerCase());
        }
        prune(root, "", next);
        writeApplied(new File(hotRoot, "applied.json"), next);
        deleteRecursive(staging);
        serveIndex = Collections.unmodifiableMap(next);
        Log.i(TAG, "hot overlay files=" + next.size() + " downloaded=" + toDownload.size());
        return toDownload.size();
    }

    private static boolean sameOverlay(
            Map<String, String> applied, Map<String, String> bundled, Map<String, String> remote) {
        for (String rel : applied.keySet()) {
            String want = remote.get(rel);
            String base = bundled.get(rel);
            if (want == null || want.equalsIgnoreCase(base)) return false;
            if (!want.equalsIgnoreCase(applied.get(rel))) return false;
        }
        return true;
    }

    private static void downloadAll(
            Activity activity, File staging, Map<String, String> remote, List<String> toDownload)
            throws Exception {
        if (toDownload.isEmpty()) return;
        ExecutorService pool = Executors.newFixedThreadPool(DOWNLOAD_PARALLELISM);
        AtomicInteger finished = new AtomicInteger();
        AtomicReference<Exception> failure = new AtomicReference<>();
        int total = toDownload.size();
        List<Future<?>> jobs = new ArrayList<>();
        for (String rel : toDownload) {
            jobs.add(pool.submit(() -> {
                if (failure.get() != null) return;
                try {
                    String hash = remote.get(rel);
                    File dest = new File(staging, rel);
                    downloadVerified(rel, hash, dest);
                    int n = finished.incrementAndGet();
                    activity.runOnUiThread(() -> showStatus(activity, "正在下载资源 " + n + "/" + total));
                } catch (Exception e) {
                    failure.compareAndSet(null, e);
                }
            }));
        }
        for (Future<?> job : jobs) {
            try {
                job.get();
            } catch (Exception e) {
                failure.compareAndSet(null, e);
            }
        }
        pool.shutdownNow();
        Exception err = failure.get();
        if (err != null) throw err;
    }

    private static void downloadVerified(String rel, String expectHash, File dest) throws Exception {
        ensureDir(dest.getParentFile());
        File tmp = new File(dest.getParentFile(), dest.getName() + ".part");
        HttpURLConnection conn = open(fileUrl(rel), FILE_READ_TIMEOUT_MS);
        try {
            int code = conn.getResponseCode();
            if (code != HttpURLConnection.HTTP_OK) {
                throw new IOException("HTTP " + code + " " + rel);
            }
            try (InputStream in = conn.getInputStream(); FileOutputStream out = new FileOutputStream(tmp)) {
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) >= 0) {
                    out.write(buf, 0, n);
                }
            }
        } finally {
            conn.disconnect();
        }
        String got = sha256(tmp);
        if (!got.equalsIgnoreCase(expectHash)) {
            //noinspection ResultOfMethodCallIgnored
            tmp.delete();
            throw new IOException("hash mismatch " + rel);
        }
        if (dest.exists() && !dest.delete()) throw new IOException("replace " + dest);
        if (!tmp.renameTo(dest)) {
            // Same-volume rename can still fail on some OEMs; fall back to copy.
            try (InputStream in = new FileInputStream(tmp); FileOutputStream out = new FileOutputStream(dest)) {
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) >= 0) out.write(buf, 0, n);
            }
            //noinspection ResultOfMethodCallIgnored
            tmp.delete();
        }
    }

    private static Map<String, String> fetchRemoteManifest() throws Exception {
        HttpURLConnection conn = open(BASE_URL + "/" + MANIFEST_NAME, MANIFEST_READ_TIMEOUT_MS);
        try {
            int code = conn.getResponseCode();
            if (code != HttpURLConnection.HTTP_OK) {
                throw new IOException("manifest HTTP " + code);
            }
            try (InputStream in = conn.getInputStream()) {
                return parseManifest(readFully(in, MANIFEST_MAX_BYTES));
            }
        } finally {
            conn.disconnect();
        }
    }

    private static HttpURLConnection open(String url, int readTimeoutMs) throws IOException {
        HttpURLConnection conn = (HttpURLConnection) new URL(url).openConnection();
        conn.setConnectTimeout(CONNECT_TIMEOUT_MS);
        conn.setReadTimeout(readTimeoutMs);
        conn.setInstanceFollowRedirects(true);
        conn.setUseCaches(false);
        conn.setRequestProperty("Accept-Encoding", "identity");
        conn.setRequestProperty("Cache-Control", "no-cache");
        return conn;
    }

    private static String fileUrl(String rel) throws Exception {
        StringBuilder sb = new StringBuilder(BASE_URL);
        for (String part : rel.split("/")) {
            sb.append('/').append(URLEncoder.encode(part, "UTF-8").replace("+", "%20"));
        }
        return sb.toString();
    }

    private static Map<String, String> readAssetManifest(Activity activity) {
        try (InputStream in = activity.getAssets().open(ASSET_MANIFEST)) {
            return parseManifest(readFully(in, MANIFEST_MAX_BYTES));
        } catch (Exception e) {
            Log.w(TAG, "bundled manifest unreadable", e);
            return new HashMap<>();
        }
    }

    private static Map<String, String> readApplied(File file) {
        if (!file.isFile()) return new HashMap<>();
        try (InputStream in = new FileInputStream(file)) {
            return parseManifest(readFully(in, MANIFEST_MAX_BYTES));
        } catch (Exception e) {
            Log.w(TAG, "applied manifest unreadable", e);
            return new HashMap<>();
        }
    }

    private static Map<String, String> parseManifest(String text) throws Exception {
        JSONObject root = new JSONObject(text);
        JSONObject files = root.optJSONObject("files");
        Map<String, String> out = new HashMap<>();
        if (files == null) return out;
        Iterator<String> keys = files.keys();
        while (keys.hasNext()) {
            String key = keys.next();
            if (!isSafeRelative(key)) continue;
            String hash = files.optString(key, "");
            if (hash.length() == 64) out.put(key, hash.toLowerCase());
        }
        return out;
    }

    private static void writeApplied(File file, Map<String, String> applied) throws IOException {
        JSONObject files = new JSONObject();
        for (Map.Entry<String, String> entry : applied.entrySet()) {
            try {
                files.put(entry.getKey(), entry.getValue());
            } catch (Exception e) {
                throw new IOException(e);
            }
        }
        JSONObject root = new JSONObject();
        try {
            root.put("files", files);
        } catch (Exception e) {
            throw new IOException(e);
        }
        File tmp = new File(file.getParentFile(), "applied.json.tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(root.toString().getBytes(StandardCharsets.UTF_8));
        }
        if (file.exists() && !file.delete()) throw new IOException("delete " + file);
        if (!tmp.renameTo(file)) throw new IOException("rename applied.json");
    }

    /** Drop overlay entries whose bytes no longer match the stored hash. */
    private static void healApplied(File root, Map<String, String> applied) {
        Iterator<Map.Entry<String, String>> it = applied.entrySet().iterator();
        while (it.hasNext()) {
            Map.Entry<String, String> entry = it.next();
            File file = new File(root, entry.getKey());
            try {
                if (!file.isFile() || !sha256(file).equalsIgnoreCase(entry.getValue())) {
                    if (file.exists() && !file.delete()) Log.w(TAG, "heal delete failed " + file);
                    it.remove();
                }
            } catch (Exception e) {
                Log.w(TAG, "heal " + entry.getKey(), e);
                if (file.exists()) file.delete();
                it.remove();
            }
        }
    }

    private static void prune(File dir, String prefix, Map<String, String> keep) {
        File[] kids = dir.listFiles();
        if (kids == null) return;
        for (File kid : kids) {
            String rel = prefix.isEmpty() ? kid.getName() : prefix + "/" + kid.getName();
            if (kid.isDirectory()) {
                prune(kid, rel, keep);
                String[] left = kid.list();
                if (left != null && left.length == 0) kid.delete();
            } else if (!keep.containsKey(rel)) {
                kid.delete();
            }
        }
    }

    private static void moveReplace(File from, File to) throws IOException {
        ensureDir(to.getParentFile());
        if (to.exists() && !to.delete()) throw new IOException("delete " + to);
        if (!from.renameTo(to)) {
            try (InputStream in = new FileInputStream(from); FileOutputStream out = new FileOutputStream(to)) {
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) >= 0) out.write(buf, 0, n);
            }
            if (!from.delete()) Log.w(TAG, "staging leftover " + from);
        }
    }

    /**
     * Parallel downloads share parents such as JHOnlineData/. Another thread may
     * create the directory between exists() and mkdirs(); mkdirs() then returns
     * false even though the path is valid — that used to abort the whole sync as
     * a fake "network" failure.
     */
    private static void ensureDir(File dir) throws IOException {
        if (dir == null) return;
        if (dir.isDirectory()) return;
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        if (!dir.isDirectory()) throw new IOException("mkdir " + dir);
    }

    private static String statusForFailure(Throwable t) {
        for (Throwable c = t; c != null; c = c.getCause()) {
            String name = c.getClass().getSimpleName();
            String msg = c.getMessage() != null ? c.getMessage() : "";
            if (c instanceof java.net.SocketTimeoutException
                    || name.contains("Timeout")
                    || msg.toLowerCase().contains("timed out")) {
                return "网络超时，使用本地资源";
            }
            if (c instanceof java.net.ConnectException
                    || c instanceof java.net.UnknownHostException
                    || msg.contains("Failed to connect")
                    || msg.contains("Unable to resolve")) {
                return "网络不可用，使用本地资源";
            }
            if (msg.contains("hash mismatch")) {
                return "资源校验失败，使用本地资源";
            }
            if (msg.startsWith("HTTP ") || msg.contains("manifest HTTP")) {
                return "热更服务器错误，使用本地资源";
            }
            if (msg.startsWith("mkdir ") || msg.startsWith("rename ") || msg.startsWith("replace ")) {
                return "本地存储失败，使用本地资源";
            }
        }
        return "资源更新失败，使用本地资源";
    }

    private static String sha256(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new FileInputStream(file)) {
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) >= 0) digest.update(buf, 0, n);
        }
        byte[] raw = digest.digest();
        StringBuilder sb = new StringBuilder(raw.length * 2);
        for (byte value : raw) sb.append(String.format("%02x", value & 0xff));
        return sb.toString();
    }

    private static String readFully(InputStream in, int max) throws IOException {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int total = 0;
        int n;
        while ((n = in.read(buf)) >= 0) {
            total += n;
            if (total > max) throw new IOException("body exceeds " + max);
            bos.write(buf, 0, n);
        }
        return bos.toString(StandardCharsets.UTF_8.name());
    }

    private static boolean isSafeRelative(String rel) {
        if (rel == null || rel.isEmpty() || rel.startsWith("/") || rel.indexOf('\\') >= 0) return false;
        if (rel.indexOf('\0') >= 0 || rel.contains("..")) return false;
        return true;
    }

    private static String relativePath(String path) {
        if (path == null || path.isEmpty() || "/".equals(path)) return "index.html";
        String rel = path.startsWith("/") ? path.substring(1) : path;
        if (rel.isEmpty()) return "index.html";
        if (!isSafeRelative(rel)) return null;
        return rel;
    }

    private static boolean isText(String rel) {
        return rel.endsWith(".html")
                || rel.endsWith(".js")
                || rel.endsWith(".mjs")
                || rel.endsWith(".css")
                || rel.endsWith(".json")
                || rel.endsWith(".txt")
                || rel.endsWith(".svg");
    }

    private static String mimeType(String rel) {
        if (rel.endsWith(".js") || rel.endsWith(".mjs")) return "application/javascript";
        if (rel.endsWith(".css")) return "text/css";
        if (rel.endsWith(".html")) return "text/html";
        if (rel.endsWith(".json")) return "application/json";
        if (rel.endsWith(".wasm")) return "application/wasm";
        if (rel.endsWith(".svg")) return "image/svg+xml";
        if (rel.endsWith(".txt")) return "text/plain";
        if (rel.endsWith(".png")) return "image/png";
        if (rel.endsWith(".jpg") || rel.endsWith(".jpeg")) return "image/jpeg";
        if (rel.endsWith(".gif")) return "image/gif";
        if (rel.endsWith(".webp")) return "image/webp";
        if (rel.endsWith(".woff2")) return "font/woff2";
        if (rel.endsWith(".woff")) return "font/woff";
        if (rel.endsWith(".ttf")) return "font/ttf";
        return "application/octet-stream";
    }

    private static void showStatus(Activity activity, String text) {
        Runnable apply = () -> {
            if (activity.isFinishing()) return;
            ViewGroup content = activity.findViewById(android.R.id.content);
            if (content == null) return;
            if (overlay == null || overlay.getParent() != content) {
                FrameLayout frame = new FrameLayout(activity);
                frame.setBackgroundColor(0xFF05070A);
                frame.setClickable(true);
                TextView tv = new TextView(activity);
                tv.setTextColor(0xFFC4A35A);
                tv.setTextSize(16f);
                tv.setGravity(Gravity.CENTER);
                int pad = (int) (24 * activity.getResources().getDisplayMetrics().density);
                tv.setPadding(pad, pad, pad, pad);
                FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT,
                        Gravity.CENTER);
                frame.addView(tv, lp);
                content.addView(
                        frame,
                        new ViewGroup.LayoutParams(
                                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
                overlay = frame;
                statusView = tv;
            }
            if (statusView != null) statusView.setText(text);
        };
        if (Looper.myLooper() == Looper.getMainLooper()) apply.run();
        else activity.runOnUiThread(apply);
    }

    private static void hideOverlay() {
        Runnable apply = () -> {
            if (overlay != null) {
                ViewGroup parent = (ViewGroup) overlay.getParent();
                if (parent != null) parent.removeView(overlay);
            }
            overlay = null;
            statusView = null;
        };
        if (Looper.myLooper() == Looper.getMainLooper()) apply.run();
        else MAIN.post(apply);
    }

    private static void deleteRecursive(File file) {
        if (file == null || !file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) deleteRecursive(child);
            }
        }
        if (!file.delete()) Log.w(TAG, "delete failed: " + file.getAbsolutePath());
    }
}
