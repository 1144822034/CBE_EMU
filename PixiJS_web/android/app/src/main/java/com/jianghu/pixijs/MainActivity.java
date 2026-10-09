package com.jianghu.pixijs;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.net.http.SslError;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.provider.Settings;
import android.util.Log;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.webkit.SslErrorHandler;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.getcapacitor.BridgeActivity;
import com.getcapacitor.BridgeWebViewClient;

import java.io.File;
import java.lang.ref.WeakReference;

/**
 * Keep gateway WebSocket alive when the screen turns off or the activity pauses.
 * Default Capacitor/WebView pauses timers onPause, which drops ws:// connections.
 *
 * On versionCode change (覆盖安装), wipe WebView site data before the bridge loads
 * so stale localStorage (hangup resume, old gateway override, cached login) cannot
 * poison the new build.
 *
 * HangupKeepAliveService also pokes {@link #pokeKeepAliveFromService()} every 2s
 * so Chromium hidden-tab timer throttling cannot starve scene-poll.
 */
public class MainActivity extends BridgeActivity {
    private static final String TAG = "PixiMain";
    private static final String PREFS = "pixijs_keepalive";
    private static final String PREFS_APP = "pixijs_app";
    /** Epoch ms of last battery-opt dialog (replaces old boolean one-shot). */
    private static final String KEY_BATTERY_PROMPT_AT = "battery_opt_prompted_at";
    private static final String KEY_NOTIF_PROMPT = "notif_perm_prompted";
    private static final String KEY_LAST_VERSION_CODE = "last_version_code";
    private static final long WEBVIEW_REARM_MS = 2000L;
    private static final int REQ_POST_NOTIFICATIONS = 19093;

    private static WeakReference<MainActivity> sInstance;

    private PowerManager.WakeLock wakeLock;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private boolean pausedForBackground = false;

    private final Runnable rearmWebViewRunnable = new Runnable() {
        @Override
        public void run() {
            if (!pausedForBackground || isFinishing()) return;
            keepWebViewRunning();
            mainHandler.postDelayed(this, WEBVIEW_REARM_MS);
        }
    };

    /** Called from HangupKeepAliveService on the main thread. */
    public static void pokeKeepAliveFromService() {
        MainActivity activity = sInstance != null ? sInstance.get() : null;
        if (activity == null || activity.isFinishing()) return;
        activity.pokeWebViewKeepAlive();
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        sInstance = new WeakReference<>(this);
        // Must run before Bridge/WebView starts so the first page load is clean.
        wipeWebDataIfAppUpdated();
        registerPlugin(PayLauncherPlugin.class);
        registerPlugin(HangupKeepAlivePlugin.class);
        registerPlugin(ApkUpdaterPlugin.class);
        registerPlugin(EntertainmentHttpPlugin.class);
        registerPlugin(CbeEmuPlugin.class);
        registerPlugin(AppExitPlugin.class);
        super.onCreate(savedInstanceState);
        hideSystemBars();
        acquireCpuWakeLock();
        // Defer settings intents: some Android 8 OEMs crash if started during onCreate.
        mainHandler.postDelayed(this::maybeRequestPostNotifications, 800L);
        mainHandler.postDelayed(this::maybeRequestIgnoreBatteryOptimizations, 1600L);
        // Re-install in case a later bridge reset replaced the client from load().
        mainHandler.post(this::installEntertainmentSslBypass);
    }

    /**
     * Bridge.load() already called loadUrl. Swap the client and pause that load
     * before the looper runs it, so hot-update files win the first real navigation.
     */
    @Override
    protected void load() {
        super.load();
        installEntertainmentSslBypass();
        DistHotUpdate.start(this);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // Dialogs / notification shade can restore system bars; re-hide on focus.
        if (hasFocus) hideSystemBars();
    }

    /**
     * Immersive sticky: hide status + nav bars for the whole app (title menu
     * included). Without this, OEM panels show the system status strip above
     * the Capacitor WebView on splash — looks like a black bar + icons.
     */
    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        try {
            Window window = getWindow();
            if (window == null) return;
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                window.setDecorFitsSystemWindows(false);
                WindowInsetsController controller = window.getInsetsController();
                if (controller != null) {
                    controller.hide(
                            WindowInsets.Type.statusBars()
                                    | WindowInsets.Type.navigationBars());
                    controller.setSystemBarsBehavior(
                            WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            } else {
                View decor = window.getDecorView();
                decor.setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                                | View.SYSTEM_UI_FLAG_FULLSCREEN
                                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
            }
        } catch (Throwable t) {
            Log.w(TAG, "hideSystemBars failed", t);
        }
    }

    @Override
    public void onStart() {
        super.onStart();
        installEntertainmentSslBypass();
    }

    /**
     * Narrow SSL proceed for known entertainment media hosts whose certs are
     * expired/misconfigured. Without this, APK video playback hangs on loading.
     */
    private void installEntertainmentSslBypass() {
        try {
            if (getBridge() == null) return;
            WebView webView = getBridge().getWebView();
            if (webView == null) return;
            webView.setWebViewClient(
                    new BridgeWebViewClient(getBridge()) {
                        @Override
                        public void onReceivedSslError(
                                WebView view, SslErrorHandler handler, SslError error) {
                            String host = null;
                            try {
                                String u = error != null ? error.getUrl() : null;
                                if (u != null) host = Uri.parse(u).getHost();
                            } catch (Throwable ignored) {
                                /* ignore */
                            }
                            if (EntertainmentHttpPlugin.isEntertainmentApiHost(host)) {
                                Log.w(TAG, "SSL proceed entertainment host=" + host);
                                handler.proceed();
                                return;
                            }
                            handler.cancel();
                        }

                        @Override
                        public android.webkit.WebResourceResponse shouldInterceptRequest(
                                WebView view, WebResourceRequest request) {
                            try {
                                if (request != null && request.getUrl() != null) {
                                    EntertainmentHttpPlugin.noteMediaUrl(request.getUrl().toString());
                                }
                            } catch (Throwable ignored) {
                                /* ignore */
                            }
                            android.webkit.WebResourceResponse hot =
                                    DistHotUpdate.intercept(getBridge(), request);
                            if (hot != null) return hot;
                            return super.shouldInterceptRequest(view, request);
                        }

                        @Override
                        public void onPageFinished(WebView view, String url) {
                            super.onPageFinished(view, url);
                            DistHotUpdate.onPageFinished(url);
                        }
                    });
        } catch (Throwable t) {
            Log.w(TAG, "installEntertainmentSslBypass failed", t);
        }
    }

    @Override
    public void onResume() {
        super.onResume();
        sInstance = new WeakReference<>(this);
        pausedForBackground = false;
        mainHandler.removeCallbacks(rearmWebViewRunnable);
        hideSystemBars();
        acquireCpuWakeLock();
        keepWebViewRunning();
    }

    @Override
    public void onPause() {
        // Re-arm BEFORE Capacitor Bridge pauses WebView, then again after.
        pausedForBackground = true;
        keepWebViewRunning();
        super.onPause();
        keepWebViewRunning();
        mainHandler.post(this::keepWebViewRunning);
        mainHandler.removeCallbacks(rearmWebViewRunnable);
        mainHandler.postDelayed(rearmWebViewRunnable, WEBVIEW_REARM_MS);
    }

    @Override
    public void onStop() {
        super.onStop();
        keepWebViewRunning();
        mainHandler.post(this::keepWebViewRunning);
    }

    @Override
    public void onDestroy() {
        pausedForBackground = false;
        mainHandler.removeCallbacksAndMessages(null);
        releaseCpuWakeLock();
        if (sInstance != null && sInstance.get() == this) {
            sInstance.clear();
        }
        DistHotUpdate.onHostDestroy();
        super.onDestroy();
    }

    /**
     * FGS native tick: un-pause WebView timers and inject JS heartbeat.
     * Presence budget is ~30s; tick is 2s so a few failed injects still recover.
     */
    private void pokeWebViewKeepAlive() {
        keepWebViewRunning();
        try {
            if (getBridge() == null) return;
            WebView webView = getBridge().getWebView();
            if (webView == null) return;
            webView.evaluateJavascript(
                    "(function(){try{"
                            + "if(typeof window.__pixiNativeKeepAlive==='function')"
                            + "window.__pixiNativeKeepAlive();"
                            + "return 1;"
                            + "}catch(e){return String(e)}})()",
                    null);
        } catch (Throwable t) {
            Log.w(TAG, "poke WebView keepalive failed", t);
        }
    }

    /**
     * Cover-install upgrade: delete WebView profile dirs (localStorage / cache /
     * cookies). Same versionCode relaunch is a no-op. Fresh install also stamps
     * the current version without meaningful data to delete.
     */
    private void wipeWebDataIfAppUpdated() {
        int versionCode = readVersionCode();
        if (versionCode <= 0) return;

        SharedPreferences prefs = getSharedPreferences(PREFS_APP, MODE_PRIVATE);
        int prev = prefs.getInt(KEY_LAST_VERSION_CODE, -1);
        if (prev == versionCode) return;

        Log.i(TAG, "app versionCode " + prev + " -> " + versionCode + "; wiping WebView data");
        try {
            deleteRecursive(new File(getApplicationInfo().dataDir, "app_webview"));
            deleteRecursive(new File(getApplicationInfo().dataDir, "webview"));
            File cache = getCacheDir();
            if (cache != null) deleteRecursive(cache);
            File codeCache = getCodeCacheDir();
            if (codeCache != null) deleteRecursive(codeCache);
            DistHotUpdate.wipe(this);
            CbeJniHotUpdate.wipe(this);
        } catch (Throwable t) {
            Log.w(TAG, "webview wipe failed", t);
        }
        prefs.edit().putInt(KEY_LAST_VERSION_CODE, versionCode).apply();
    }

    private int readVersionCode() {
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                return (int) info.getLongVersionCode();
            }
            return info.versionCode;
        } catch (PackageManager.NameNotFoundException e) {
            Log.w(TAG, "versionCode read failed", e);
            return -1;
        }
    }

    private static void deleteRecursive(File file) {
        if (file == null || !file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        if (!file.delete()) {
            Log.w(TAG, "delete failed: " + file.getAbsolutePath());
        }
    }

    private void keepWebViewRunning() {
        try {
            if (getBridge() == null) return;
            WebView webView = getBridge().getWebView();
            if (webView == null) return;
            webView.onResume();
            webView.resumeTimers();
        } catch (Throwable ignored) {
            /* bridge may not be ready yet */
        }
    }

    private void acquireCpuWakeLock() {
        if (wakeLock != null && wakeLock.isHeld()) return;
        try {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm == null) return;
            wakeLock = pm.newWakeLock(
                PowerManager.PARTIAL_WAKE_LOCK,
                "jianghu.pixijs:gateway");
            wakeLock.setReferenceCounted(false);
            wakeLock.acquire();
        } catch (Throwable ignored) {
            /* OEM / permission */
        }
    }

    private void releaseCpuWakeLock() {
        try {
            if (wakeLock != null && wakeLock.isHeld()) {
                wakeLock.release();
            }
        } catch (Throwable ignored) {
            /* ignore */
        }
        wakeLock = null;
    }

    /** Android 13+: FGS without notification permission is invisible → OEM kills it. */
    private void maybeRequestPostNotifications() {
        if (isFinishing()) return;
        if (Build.VERSION.SDK_INT < 33) return;
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                == PackageManager.PERMISSION_GRANTED) {
            return;
        }
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        if (prefs.getBoolean(KEY_NOTIF_PROMPT, false)) return;
        prefs.edit().putBoolean(KEY_NOTIF_PROMPT, true).apply();
        ActivityCompat.requestPermissions(
                this,
                new String[] { Manifest.permission.POST_NOTIFICATIONS },
                REQ_POST_NOTIFICATIONS);
    }

    /**
     * Prompt ignore-battery-optimizations while still restricted.
     * Re-ask on later launches if the user has not granted unrestricted yet
     * (old one-shot flag left users stuck after dismissing once).
     */
    private void maybeRequestIgnoreBatteryOptimizations() {
        if (isFinishing()) return;
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return;
        try {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm == null) return;
            if (pm.isIgnoringBatteryOptimizations(getPackageName())) return;

            SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
            long last = prefs.getLong(KEY_BATTERY_PROMPT_AT, 0L);
            long now = System.currentTimeMillis();
            // At most once per 3 days while still restricted.
            if (last > 0L && (now - last) < 3L * 24L * 60L * 60L * 1000L) return;
            prefs.edit().putLong(KEY_BATTERY_PROMPT_AT, now).apply();

            Intent intent = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
            intent.setData(Uri.parse("package:" + getPackageName()));
            if (intent.resolveActivity(getPackageManager()) == null) return;
            startActivity(intent);
        } catch (Throwable ignored) {
            /* some OEMs block this intent */
        }
    }
}
