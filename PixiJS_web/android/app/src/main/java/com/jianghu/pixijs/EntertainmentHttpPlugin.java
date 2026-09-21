package com.jianghu.pixijs;

import android.app.DownloadManager;
import android.content.Context;
import android.net.Uri;
import android.os.Environment;
import android.util.Log;

import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.security.cert.X509Certificate;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import javax.net.ssl.HostnameVerifier;
import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;

/**
 * Resolve entertainment video API URLs on native HTTP.
 * CapacitorHttp / system TLS reject expired or wrong-host certs on
 * api.qemao.com / ksxjj.txqq.pro, which makes JS fetch always "解析失败".
 * Trust is narrowed to known entertainment API hosts only; redirect targets
 * (CDN mp4) use normal certificate verification.
 */
@CapacitorPlugin(name = "EntertainmentHttp")
public class EntertainmentHttpPlugin extends Plugin {
    private static final String TAG = "EntertainmentHttp";
    private static final int MAX_REDIRECTS = 8;
    private static final int MAX_BODY_CHARS = 65536;
    private static final Pattern MP4_IN_TEXT =
            Pattern.compile("https?://[^\\s\"'<>\\\\]+?\\.(?:mp4|m3u8)[^\\s\"'<>\\\\]*", Pattern.CASE_INSENSITIVE);

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private static volatile SSLSocketFactory laxSocketFactory;
    private static volatile String lastMediaUrl = "";

    /** Remember the latest entertainment audio request seen by the WebView. */
    public static void noteMediaUrl(String raw) {
        if (raw == null) return;
        String url = raw.trim();
        if (!isMusicFileUrl(url)) return;
        lastMediaUrl = url;
    }

    static boolean isMusicFileUrl(String raw) {
        android.net.Uri uri;
        try {
            uri = android.net.Uri.parse(raw);
        } catch (Throwable t) {
            return false;
        }
        String scheme = uri.getScheme();
        if (scheme == null) return false;
        scheme = scheme.toLowerCase(Locale.US);
        if (!scheme.equals("http") && !scheme.equals("https")) return false;
        String host = uri.getHost();
        if (host == null) return false;
        host = host.toLowerCase(Locale.US);
        if (host.equals("localhost") || host.equals("127.0.0.1")) return false;
        String path = uri.getPath() == null ? "" : uri.getPath().toLowerCase(Locale.US);
        if (path.contains("/api/")
                || path.endsWith(".mp4")
                || path.endsWith(".m3u8")
                || path.endsWith(".webm")
                || path.endsWith(".jpg")
                || path.endsWith(".jpeg")
                || path.endsWith(".png")
                || path.endsWith(".gif")
                || path.endsWith(".webp")
                || path.endsWith(".js")
                || path.endsWith(".css")
                || path.endsWith(".html")
                || path.endsWith(".json")
                || path.endsWith(".lrc")) {
            return false;
        }
        if (path.endsWith(".mp3")
                || path.endsWith(".flac")
                || path.endsWith(".m4a")
                || path.endsWith(".aac")
                || path.endsWith(".wav")
                || path.endsWith(".ogg")
                || path.endsWith(".ape")
                || path.endsWith(".wma")) {
            return true;
        }
        String q = uri.getQuery() == null ? "" : uri.getQuery().toLowerCase(Locale.US);
        if (q.contains("format=mp3")
                || q.contains("format=flac")
                || q.contains("format=aac")
                || q.contains("format=m4a")
                || q.contains("type=convert_url")) {
            return true;
        }
        return host.contains("kuwo.cn")
                || host.contains("qqmusic")
                || host.contains("music.126.net")
                || host.contains("kugou.com");
    }

    @PluginMethod
    public void resolve(PluginCall call) {
        String url = call.getString("url", "");
        if (url == null || url.trim().isEmpty()) {
            call.reject("missing url");
            return;
        }
        final boolean preferJson = Boolean.TRUE.equals(call.getBoolean("preferJson", false));
        final int timeoutMs = Math.max(3000, call.getInt("timeoutMs", 10000));
        final String startUrl = url.trim();
        call.setKeepAlive(true);
        executor.execute(() -> {
            try {
                ResolveResult result = resolveInternal(startUrl, preferJson, timeoutMs);
                JSObject out = new JSObject();
                out.put("url", result.url);
                if (result.body != null) out.put("body", result.body);
                Log.i(TAG, "resolve ok preferJson=" + preferJson + " url=" + result.url);
                call.resolve(out);
            } catch (Exception e) {
                Log.w(TAG, "resolve failed url=" + startUrl, e);
                call.reject(e.getMessage() != null ? e.getMessage() : "resolve failed", e);
            }
        });
    }

    /** Enqueue the current playable mp4 into the system DownloadManager. */
    @PluginMethod
    public void download(PluginCall call) {
        String url = call.getString("url", "");
        if (url == null || !(url.startsWith("http://") || url.startsWith("https://"))) {
            call.reject("bad url");
            return;
        }
        String filename = call.getString("filename", "");
        if (filename == null || filename.trim().isEmpty()) {
            filename = "video_" + System.currentTimeMillis() + ".mp4";
        }
        filename = filename.replaceAll("[\\\\/:*?\"<>|]", "_");
        try {
            DownloadManager.Request req = new DownloadManager.Request(Uri.parse(url.trim()));
            req.setTitle(filename);
            req.setDescription(filename.toLowerCase(Locale.US).endsWith(".mp4") ? "娱乐视频" : "娱乐音乐");
            req.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            req.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, filename);
            req.setMimeType(mimeForName(filename));
            DownloadManager dm = (DownloadManager) getContext().getSystemService(Context.DOWNLOAD_SERVICE);
            if (dm == null) {
                call.reject("no download manager");
                return;
            }
            dm.enqueue(req);
            JSObject out = new JSObject();
            out.put("ok", true);
            call.resolve(out);
        } catch (Exception e) {
            Log.w(TAG, "download failed url=" + url, e);
            call.reject(e.getMessage() != null ? e.getMessage() : "download failed", e);
        }
    }

    @PluginMethod
    public void currentMedia(PluginCall call) {
        JSObject out = new JSObject();
        out.put("url", lastMediaUrl == null ? "" : lastMediaUrl);
        call.resolve(out);
    }

    private static String mimeForName(String filename) {
        String n = filename.toLowerCase(Locale.US);
        if (n.endsWith(".mp3")) return "audio/mpeg";
        if (n.endsWith(".flac")) return "audio/flac";
        if (n.endsWith(".m4a") || n.endsWith(".aac")) return "audio/mp4";
        if (n.endsWith(".wav")) return "audio/wav";
        if (n.endsWith(".ogg")) return "audio/ogg";
        if (n.endsWith(".mp4")) return "video/mp4";
        return "application/octet-stream";
    }

    private static ResolveResult resolveInternal(String startUrl, boolean preferJson, int timeoutMs)
            throws Exception {
        String current = startUrl;
        for (int hop = 0; hop < MAX_REDIRECTS; hop++) {
            URL u = new URL(current);
            HttpURLConnection conn = (HttpURLConnection) u.openConnection();
            try {
                conn.setConnectTimeout(timeoutMs);
                conn.setReadTimeout(timeoutMs);
                conn.setInstanceFollowRedirects(false);
                conn.setRequestMethod("GET");
                conn.setRequestProperty("Accept", "*/*");
                conn.setRequestProperty("User-Agent", "JianghuOL-Entertainment/1.0");
                if (!preferJson) {
                    conn.setRequestProperty("Range", "bytes=0-0");
                }
                applyLaxSslIfNeeded(conn, u.getHost());

                int code = conn.getResponseCode();
                String location = conn.getHeaderField("Location");
                String ct = header(conn.getContentType());
                String finalUrl = conn.getURL() != null ? conn.getURL().toString() : current;

                if (code >= 300 && code < 400 && location != null && !location.isEmpty()) {
                    current = new URL(u, location).toString();
                    Log.i(TAG, "redirect " + code + " -> " + current);
                    continue;
                }

                if (preferJson || isTextual(ct)) {
                    String body = readBody(conn, code);
                    String fromJson = pickUrlFromJsonish(body);
                    if (fromJson != null) {
                        return new ResolveResult(fromJson, body);
                    }
                    Matcher m = MP4_IN_TEXT.matcher(body);
                    if (m.find()) {
                        return new ResolveResult(m.group(), body);
                    }
                    if (preferJson) {
                        throw new IllegalStateException("json has no playable url");
                    }
                }

                if (looksLikeMedia(finalUrl, ct) || code == 200 || code == 206) {
                    return new ResolveResult(finalUrl, null);
                }
                throw new IllegalStateException("unexpected http " + code + " ct=" + ct);
            } finally {
                conn.disconnect();
            }
        }
        throw new IllegalStateException("too many redirects");
    }

    private static boolean isTextual(String ct) {
        return ct.contains("json")
                || ct.contains("text/")
                || ct.contains("javascript")
                || ct.contains("xml");
    }

    private static boolean looksLikeMedia(String url, String ct) {
        String u = url.toLowerCase(Locale.US);
        return ct.contains("video")
                || ct.contains("mpegurl")
                || ct.contains("octet-stream")
                || u.contains(".mp4")
                || u.contains(".m3u8");
    }

    private static String header(String v) {
        return v == null ? "" : v.toLowerCase(Locale.US);
    }

    private static String readBody(HttpURLConnection conn, int code) throws Exception {
        InputStream stream;
        try {
            stream = code >= 400 ? conn.getErrorStream() : conn.getInputStream();
        } catch (Exception e) {
            stream = conn.getErrorStream();
        }
        if (stream == null) return "";
        StringBuilder sb = new StringBuilder();
        try (BufferedReader br =
                new BufferedReader(new InputStreamReader(stream, StandardCharsets.UTF_8))) {
            char[] buf = new char[2048];
            int n;
            while ((n = br.read(buf)) >= 0) {
                sb.append(buf, 0, n);
                if (sb.length() >= MAX_BODY_CHARS) break;
            }
        }
        return sb.toString().replace("\uFEFF", "").trim();
    }

    /** Best-effort extract url/msg from JSON or JSON-ish body. */
    private static String pickUrlFromJsonish(String body) {
        if (body == null || body.isEmpty()) return null;
        String plain = body.replace("\\/", "/");
        Matcher msg =
                Pattern.compile(
                                "\"(?:url|msg|video|play_url|playUrl)\"\\s*:\\s*\"([^\"]+)\"",
                                Pattern.CASE_INSENSITIVE)
                        .matcher(plain);
        if (msg.find()) {
            String v = msg.group(1).trim();
            if (v.startsWith("http://") || v.startsWith("https://")) return v;
        }
        Matcher m = MP4_IN_TEXT.matcher(plain);
        return m.find() ? m.group() : null;
    }

    private static void applyLaxSslIfNeeded(HttpURLConnection conn, String host) throws Exception {
        if (!(conn instanceof HttpsURLConnection)) return;
        if (!isEntertainmentApiHost(host)) return;
        HttpsURLConnection https = (HttpsURLConnection) conn;
        https.setSSLSocketFactory(getLaxSocketFactory());
        https.setHostnameVerifier(ENTERTAINMENT_HOSTNAME_VERIFIER);
    }

    static boolean isEntertainmentApiHost(String host) {
        if (host == null || host.isEmpty()) return false;
        String h = host.toLowerCase(Locale.US);
        return h.equals("api.qemao.com")
                || h.endsWith(".qemao.com")
                || h.equals("ksxjj.txqq.pro")
                || h.endsWith(".txqq.pro")
                || h.equals("v.nrzj.vip")
                || h.endsWith(".nrzj.vip")
                || h.equals("smoba.xin")
                || h.endsWith(".smoba.xin");
    }

    private static final HostnameVerifier ENTERTAINMENT_HOSTNAME_VERIFIER =
            (hostname, session) -> isEntertainmentApiHost(hostname);

    private static SSLSocketFactory getLaxSocketFactory() throws Exception {
        SSLSocketFactory cached = laxSocketFactory;
        if (cached != null) return cached;
        TrustManager[] trustAll =
                new TrustManager[] {
                    new X509TrustManager() {
                        @Override
                        public void checkClientTrusted(X509Certificate[] chain, String authType) {}

                        @Override
                        public void checkServerTrusted(X509Certificate[] chain, String authType) {}

                        @Override
                        public X509Certificate[] getAcceptedIssuers() {
                            return new X509Certificate[0];
                        }
                    }
                };
        SSLContext ctx = SSLContext.getInstance("TLS");
        ctx.init(null, trustAll, new SecureRandom());
        cached = ctx.getSocketFactory();
        laxSocketFactory = cached;
        return cached;
    }

    private static final class ResolveResult {
        final String url;
        final String body;

        ResolveResult(String url, String body) {
            this.url = url;
            this.body = body;
        }
    }
}
