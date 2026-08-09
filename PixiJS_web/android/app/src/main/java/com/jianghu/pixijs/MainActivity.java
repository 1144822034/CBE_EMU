package com.jianghu.pixijs;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.provider.Settings;
import android.webkit.WebView;

import com.getcapacitor.BridgeActivity;

/**
 * Keep gateway WebSocket alive when the screen turns off or the activity pauses.
 * Default Capacitor/WebView pauses timers onPause, which drops ws:// connections.
 */
public class MainActivity extends BridgeActivity {
    private static final String PREFS = "pixijs_keepalive";
    private static final String KEY_BATTERY_PROMPT = "battery_opt_prompted";

    private PowerManager.WakeLock wakeLock;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    @Override
    public void onCreate(Bundle savedInstanceState) {
        registerPlugin(PayLauncherPlugin.class);
        super.onCreate(savedInstanceState);
        acquireCpuWakeLock();
        // Defer settings intent: some Android 8 OEMs crash if started during onCreate.
        mainHandler.postDelayed(this::maybeRequestIgnoreBatteryOptimizations, 1500L);
    }

    @Override
    public void onResume() {
        super.onResume();
        acquireCpuWakeLock();
        keepWebViewRunning();
    }

    @Override
    public void onPause() {
        super.onPause();
        // Screen-off / home: re-arm WebView so WS + setInterval keep running.
        keepWebViewRunning();
    }

    @Override
    public void onStop() {
        super.onStop();
        keepWebViewRunning();
    }

    @Override
    public void onDestroy() {
        mainHandler.removeCallbacksAndMessages(null);
        releaseCpuWakeLock();
        super.onDestroy();
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
