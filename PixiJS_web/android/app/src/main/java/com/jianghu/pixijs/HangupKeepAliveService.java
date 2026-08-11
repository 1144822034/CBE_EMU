package com.jianghu.pixijs;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;

/**
 * Foreground service while the player is in-world (or hangup-armed) so OEM Doze
 * / background WebView freeze is less likely to kill scene-poll. Complements
 * MainActivity WakeLock + resumeTimers.
 *
 * Critical: Chromium still throttles setInterval while document.hidden even with
 * resumeTimers(). A native Handler tick (held by FGS + WakeLock) re-arms the
 * WebView and injects JS poll/ping so server presence (~30s) stays fresh.
 */
public class HangupKeepAliveService extends Service {
    private static final String TAG = "PixiHangupKeepAlive";
    public static final String ACTION_START = "com.jianghu.pixijs.action.HANGUP_KEEPALIVE_START";
    public static final String ACTION_STOP = "com.jianghu.pixijs.action.HANGUP_KEEPALIVE_STOP";
    public static final String EXTRA_MODE = "keepalive_mode";
    public static final String MODE_ONLINE = "online";
    public static final String MODE_HANGUP = "hangup";

    private static final String CHANNEL_ID = "pixijs_hangup_keepalive";
    private static final int NOTIFICATION_ID = 19092;
    /** Under server presence budget (~30s); also re-arms OEM-paused WebView. */
    private static final long NATIVE_TICK_MS = 2000L;

    private PowerManager.WakeLock wakeLock;
    private WifiManager.WifiLock wifiLock;
    private String mode = MODE_HANGUP;
    private final Handler tickHandler = new Handler(Looper.getMainLooper());
    private boolean tickArmed = false;

    private final Runnable nativeTickRunnable = new Runnable() {
        @Override
        public void run() {
            if (!tickArmed)
                return;
            MainActivity.pokeKeepAliveFromService();
            tickHandler.postDelayed(this, NATIVE_TICK_MS);
        }
    };

    public static void start(Context context) {
        start(context, MODE_HANGUP);
    }

    public static void start(Context context, String mode) {
        Intent intent = new Intent(context, HangupKeepAliveService.class);
        intent.setAction(ACTION_START);
        intent.putExtra(EXTRA_MODE, mode != null ? mode : MODE_HANGUP);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }

    public static void stop(Context context) {
        Intent intent = new Intent(context, HangupKeepAliveService.class);
        intent.setAction(ACTION_STOP);
        try {
            context.startService(intent);
        } catch (Exception e) {
            Log.w(TAG, "stop service failed", e);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent != null ? intent.getAction() : ACTION_START;
        if (ACTION_STOP.equals(action)) {
            stopNativeTick();
            releaseLocks();
            stopForeground(true);
            stopSelf();
            return START_NOT_STICKY;
        }

        if (intent != null) {
            String next = intent.getStringExtra(EXTRA_MODE);
            if (next != null && next.length() > 0) {
                mode = next;
            }
        }

        ensureNotificationChannel();
        startForegroundTyped(buildNotification());
        acquireLocks();
        startNativeTick();
        Log.i(TAG, "keepalive foreground active mode=" + mode);
        return START_STICKY;
    }

    @Override
    public void onTimeout(int startId) {
        restartAfterTimeout(startId, -1);
    }

    @Override
    public void onTimeout(int startId, int fgsType) {
        restartAfterTimeout(startId, fgsType);
    }

    private void restartAfterTimeout(int startId, int fgsType) {
        // API 34+ dataSync budget; we prefer specialUse but still recover.
        Log.w(TAG, "FGS onTimeout startId=" + startId + " type=" + fgsType + "; restarting");
        stopNativeTick();
        stopForeground(true);
        stopSelf(startId);
        start(getApplicationContext(), mode);
    }

    @Override
    public void onDestroy() {
        stopNativeTick();
        releaseLocks();
        Log.i(TAG, "keepalive stopped");
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void startNativeTick() {
        tickArmed = true;
        tickHandler.removeCallbacks(nativeTickRunnable);
        tickHandler.post(nativeTickRunnable);
    }

    private void stopNativeTick() {
        tickArmed = false;
        tickHandler.removeCallbacks(nativeTickRunnable);
    }

    private void startForegroundTyped(Notification notification) {
        if (Build.VERSION.SDK_INT >= 34) {
            // specialUse: no 6h dataSync cap (overnight hangup). Sideload APK OK.
            startForeground(
                    NOTIFICATION_ID,
                    notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                    NOTIFICATION_ID,
                    notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
    }

    private void ensureNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O)
            return;
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (nm == null)
            return;
        if (nm.getNotificationChannel(CHANNEL_ID) != null)
            return;
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                getString(R.string.hangup_keepalive_channel_name),
                NotificationManager.IMPORTANCE_LOW);
        channel.setDescription(getString(R.string.hangup_keepalive_channel_desc));
        channel.setShowBadge(false);
        nm.createNotificationChannel(channel);
    }

    private Notification buildNotification() {
        Intent launch = new Intent(this, MainActivity.class);
        launch.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        int pendingFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
            pendingFlags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent contentIntent = PendingIntent.getActivity(this, 0, launch, pendingFlags);

        boolean hangup = MODE_HANGUP.equals(mode);
        int titleRes = hangup
                ? R.string.hangup_keepalive_notification_title
                : R.string.online_keepalive_notification_title;
        int textRes = hangup
                ? R.string.hangup_keepalive_notification_text
                : R.string.online_keepalive_notification_text;

        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder = new Notification.Builder(this, CHANNEL_ID);
        } else {
            builder = new Notification.Builder(this);
        }
        builder.setContentTitle(getString(titleRes))
                .setContentText(getString(textRes))
                .setSmallIcon(R.drawable.ic_stat_hangup_keepalive)
                .setContentIntent(contentIntent)
                .setOngoing(true)
                .setOnlyAlertOnce(true);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            builder.setCategory(Notification.CATEGORY_SERVICE);
            builder.setVisibility(Notification.VISIBILITY_PUBLIC);
        }
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            builder.setPriority(Notification.PRIORITY_LOW);
        }
        return builder.build();
    }

    private void acquireLocks() {
        if (wakeLock == null) {
            PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
            if (pm != null) {
                wakeLock = pm.newWakeLock(
                        PowerManager.PARTIAL_WAKE_LOCK,
                        "jianghu.pixijs:HangupKeepAlive");
                wakeLock.setReferenceCounted(false);
            }
        }
        if (wakeLock != null && !wakeLock.isHeld()) {
            wakeLock.acquire();
            Log.i(TAG, "partial wake lock acquired");
        }

        if (wifiLock == null) {
            WifiManager wifi = (WifiManager) getApplicationContext()
                    .getSystemService(Context.WIFI_SERVICE);
            if (wifi != null) {
                wifiLock = wifi.createWifiLock(
                        WifiManager.WIFI_MODE_FULL_HIGH_PERF,
                        "jianghu.pixijs:HangupWifi");
                wifiLock.setReferenceCounted(false);
            }
        }
        if (wifiLock != null && !wifiLock.isHeld()) {
            try {
                wifiLock.acquire();
                Log.i(TAG, "wifi lock acquired");
            } catch (Exception e) {
                Log.w(TAG, "wifi lock acquire failed", e);
            }
        }
    }

    private void releaseLocks() {
        if (wifiLock != null && wifiLock.isHeld()) {
            try {
                wifiLock.release();
            } catch (Exception e) {
                Log.w(TAG, "wifi lock release failed", e);
            }
        }
        if (wakeLock != null && wakeLock.isHeld()) {
            try {
                wakeLock.release();
            } catch (Exception e) {
                Log.w(TAG, "wake lock release failed", e);
            }
        }
    }
}
