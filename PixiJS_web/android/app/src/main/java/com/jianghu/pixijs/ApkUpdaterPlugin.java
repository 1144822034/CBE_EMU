package com.jianghu.pixijs;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.util.Log;

import androidx.activity.result.ActivityResult;
import androidx.core.content.FileProvider;

import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.ActivityCallback;
import com.getcapacitor.annotation.CapacitorPlugin;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Download a remote APK into cache and launch the system package installer.
 * Used when login is rejected for codeVersion mismatch ("请更新客户端").
 */
@CapacitorPlugin(name = "ApkUpdater")
public class ApkUpdaterPlugin extends Plugin {
    private static final String TAG = "ApkUpdater";
    private static final String DEFAULT_APK_NAME = "jh_ol.apk";

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final AtomicBoolean downloading = new AtomicBoolean(false);
    private volatile File pendingApkFile;

    @PluginMethod
    public void downloadAndInstall(PluginCall call) {
        String url = call.getString("url", "");
        if (url == null || url.trim().isEmpty()) {
            call.reject("missing url");
            return;
        }
        if (!downloading.compareAndSet(false, true)) {
            call.reject("download already in progress");
            return;
        }

        call.setKeepAlive(true);
        final String apkUrl = url.trim();
        executor.execute(() -> {
            File out = null;
            HttpURLConnection conn = null;
            try {
                File dir = new File(getContext().getCacheDir(), "apk_updates");
                if (!dir.exists() && !dir.mkdirs()) {
                    throw new IllegalStateException("cannot create apk_updates dir");
                }
                out = new File(dir, DEFAULT_APK_NAME);
                if (out.exists() && !out.delete()) {
                    Log.w(TAG, "could not delete old apk before download");
                }

                conn = openGet(apkUrl);
                int code = conn.getResponseCode();
                if (isRedirect(code)) {
                    String loc = conn.getHeaderField("Location");
                    conn.disconnect();
                    if (loc == null || loc.isEmpty()) {
                        throw new IllegalStateException("redirect without Location: HTTP " + code);
                    }
                    conn = openGet(loc);
                    code = conn.getResponseCode();
                }
                if (code < 200 || code >= 300) {
                    throw new IllegalStateException("HTTP " + code);
                }

                long total = conn.getContentLength();
                try (InputStream in = conn.getInputStream();
                     FileOutputStream fos = new FileOutputStream(out)) {
                    byte[] buf = new byte[64 * 1024];
                    long got = 0;
                    int n;
                    int lastPct = -1;
                    while ((n = in.read(buf)) >= 0) {
                        fos.write(buf, 0, n);
                        got += n;
                        if (total > 0) {
                            int pct = (int) Math.min(100L, (got * 100L) / total);
                            if (pct != lastPct && (pct == 100 || pct - lastPct >= 2)) {
                                lastPct = pct;
                                emitProgress(pct, got, total);
                            }
                        } else if (got % (512L * 1024L) < buf.length) {
                            emitProgress(-1, got, total);
                        }
                    }
                    fos.flush();
                }
                emitProgress(100, out.length(), total > 0 ? total : out.length());

                if (out.length() < 1024) {
                    throw new IllegalStateException("downloaded file too small");
                }

                final File apk = out;
                getActivity().runOnUiThread(() -> beginInstall(call, apk));
            } catch (Exception e) {
                Log.e(TAG, "download failed " + apkUrl, e);
                downloading.set(false);
                if (out != null && out.exists()) {
                    //noinspection ResultOfMethodCallIgnored
                    out.delete();
                }
                call.reject("download failed: " + e.getMessage(), e);
            } finally {
                if (conn != null) conn.disconnect();
            }
        });
    }

    private static boolean isRedirect(int code) {
        return code == HttpURLConnection.HTTP_MOVED_PERM
                || code == HttpURLConnection.HTTP_MOVED_TEMP
                || code == HttpURLConnection.HTTP_SEE_OTHER
                || code == 307
                || code == 308;
    }

    private static HttpURLConnection openGet(String apkUrl) throws Exception {
        HttpURLConnection conn = (HttpURLConnection) new URL(apkUrl).openConnection();
        conn.setConnectTimeout(20000);
        conn.setReadTimeout(60000);
        conn.setInstanceFollowRedirects(true);
        conn.setRequestProperty("User-Agent", "JianghuOL-ApkUpdater/1.0");
        conn.connect();
        return conn;
    }

    private void emitProgress(int percent, long bytes, long total) {
        JSObject data = new JSObject();
        data.put("percent", percent);
        data.put("bytes", bytes);
        data.put("total", total);
        notifyListeners("downloadProgress", data);
    }

    private void beginInstall(PluginCall call, File apk) {
        pendingApkFile = apk;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                && !getContext().getPackageManager().canRequestPackageInstalls()) {
            try {
                Intent settings = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES);
                settings.setData(Uri.parse("package:" + getContext().getPackageName()));
                startActivityForResult(call, settings, "installPermission");
            } catch (Exception e) {
                downloading.set(false);
                call.reject("need install permission", e);
            }
            return;
        }
        launchInstaller(call, apk);
    }

    @ActivityCallback
    private void installPermission(PluginCall call, ActivityResult result) {
        File apk = pendingApkFile;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                && !getContext().getPackageManager().canRequestPackageInstalls()) {
            downloading.set(false);
            if (call != null) call.reject("install permission denied");
            return;
        }
        if (apk == null || !apk.exists()) {
            downloading.set(false);
            if (call != null) call.reject("apk missing after permission");
            return;
        }
        launchInstaller(call, apk);
    }

    private void launchInstaller(PluginCall call, File apk) {
        try {
            Uri uri = FileProvider.getUriForFile(
                    getContext(),
                    getContext().getPackageName() + ".fileprovider",
                    apk);
            Intent intent = new Intent(Intent.ACTION_VIEW);
            intent.setDataAndType(uri, "application/vnd.android.package-archive");
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            Activity activity = getActivity();
            if (activity != null) {
                activity.startActivity(intent);
            } else {
                getContext().startActivity(intent);
            }
            JSObject ret = new JSObject();
            ret.put("ok", true);
            ret.put("path", apk.getAbsolutePath());
            if (call != null) call.resolve(ret);
        } catch (Exception e) {
            Log.e(TAG, "install launch failed", e);
            if (call != null) call.reject("install launch failed: " + e.getMessage(), e);
        } finally {
            downloading.set(false);
            pendingApkFile = null;
        }
    }
}
