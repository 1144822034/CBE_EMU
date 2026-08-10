package com.jianghu.pixijs;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.provider.Settings;
import android.util.Log;
import android.webkit.WebView;

import com.getcapacitor.BridgeActivity;

import java.io.File;

/**
 * Keep gateway WebSocket alive when the screen turns off or the activity pauses.
 * Default Capacitor/WebView pauses timers onPause, which drops ws:// connections.
 *
 * On versionCode change (覆盖安装), wipe WebView site data before the bridge loads
 * so stale localStorage (hangup resume, old gateway override, cached login) cannot
 * poison the new build.
 */
public class MainActivity extends BridgeActivity {
    private static final String TAG = "PixiMain";
    private static final String PREFS = "pixijs_keepalive";
    private static final String PREFS_APP = "pixijs_app";
    private static final String KEY_BATTERY_PROMPT = "battery_opt_prompted";
    private static final String KEY_LAST_VERSION_CODE = "last_version_code";
    private static final long WEBVIEW_REARM_MS = 4000L;

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

    @Override
    public void onCreate(Bundle savedInstanceState) {
        // Must run before Bridge/WebView starts so the first page load is clean.
        wipeWebDataIfAppUpdated();
        registerPlugin(PayLauncherPlugin.class);
        registerPlugin(HangupKeepAlivePlugin.class);
        super.onCreate(savedInstanceState);
        acquireCpuWakeLock();
        // Defer settings intent: some Android 8 OEMs crash if started during onCreate.
        mainHandler.postDelayed(this::maybeRequestIgnoreBatteryOptimizations, 1500L);
    }

    @Override
    public void onResume() {
        super.onResume();
        pausedForBackground = false;
        mainHandler.removeCallbacks(rearmWebViewRunnable);
        acquireCpuWakeLock();
        keepWebViewRunning();
    }

    @Override
    public void onPause() {
        super.onPause();
        // Screen-off / home: re-arm WebView so WS + setInterval keep running.
        pausedForBackground = true;
        keepWebViewRunning();
        mainHandler.removeCallbacks(rearmWebViewRunnable);
        mainHandler.postDelayed(rearmWebViewRunnable, WEBVIEW_REARM_MS);
    }

    @Override
    public void onStop() {
        super.onStop();
        keepWebViewRunning();
    }

    @Override
    public void onDestroy() {
        pausedForBackground = false;
        mainHandler.removeCallbacksAndMessages(null);
        releaseCpuWakeLock();
        super.onDestroy();
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

    /** One-shot system dialog: allow unrestricted background (OEM Doze). */
    private void maybeRequestIgnoreBatteryOptimizations() {
        if (isFinishing()) return;
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return;
        try {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm == null) return;
            if (pm.isIgnoringBatteryOptimizations(getPackageName())) return;

            SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
            if (prefs.getBoolean(KEY_BATTERY_PROMPT, false)) return;
            prefs.edit().putBoolean(KEY_BATTERY_PROMPT, true).apply();

            Intent intent = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
            intent.setData(Uri.parse("package:" + getPackageName()));
            if (intent.resolveActivity(getPackageManager()) == null) return;
            startActivity(intent);
        } catch (Throwable ignored) {
            /* some OEMs block this intent */
        }
    }
}
