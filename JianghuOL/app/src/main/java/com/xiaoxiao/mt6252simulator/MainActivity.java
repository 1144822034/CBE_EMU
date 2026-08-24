package com.xiaoxiao.mt6252simulator;


import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Message;
import android.os.PowerManager;
import android.provider.Settings;
import android.system.Os;
import android.text.InputFilter;
import android.text.InputType;
import android.view.KeyEvent;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.PopupMenu;
import android.widget.TextView;
import android.widget.Toast;

import com.xiaoxiao.mt6252simulator.R;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends Activity {
    private static final String TAG = "JianghuOL";
    private static final String SD_ROOT_DIR_NAME = "JHOnline";
    private static final String PREFS_NAME = "jianghu_asset_state";
    private static final String PREF_ASSETS_EXTRACTED = "assets_extracted";
    private static final String PREF_ASSET_VERSION = "asset_version";
    private static final String PREF_INSTALLED_VERSION_CODE = "installed_version_code";
    private static final String PREF_SELECTED_SERVER = "selected_server_name";
    private static final String PREF_VIRTUAL_PAD_ENABLED = "virtual_pad_enabled";
    private static final String PREF_BATTERY_OPT_PROMPTED = "battery_opt_prompted";
    private static final String DEFAULT_ACCOUNT_WEB_URL = "https://jh.cbhub.top";
    private static final int REQ_POST_NOTIFICATIONS = 1001;
    /* Same Android button ids as bindKeyButton(up/down/left/right). */
    private static final int PAD_KEY_UP = 14;
    private static final int PAD_KEY_DOWN = 15;
    private static final int PAD_KEY_LEFT = 16;
    private static final int PAD_KEY_RIGHT = 17;
    private static final int ASSET_VERSION = 3;
    private static final String[] REQUIRED_ASSET_ROOTS = {
            "font_gb.uc3", "CBE", "JHOnlineData", "servers.conf",
            /* Host codeVersion override; JNI reads /sdcard/JHOnline/client_code_version.txt */
            "client_code_version.txt"
    };
    private static final String[] OPTIONAL_ASSET_ROOTS = {"net_mocks"};
    /* On APK versionCode upgrade, force-refresh these trees under /sdcard/JHOnline. */
    private static final String[] VERSION_UPGRADE_FORCE_ROOTS = {
            "JHOnlineData", "servers.conf", "client_code_version.txt"
    };
    private static final int TOUCH_MOVE_SLOP = 3;
    private static final int TOUCH_MOVE_MIN_INTERVAL_MS = 45;
    private static final ExecutorService ASSET_EXECUTOR = Executors.newSingleThreadExecutor();
    private String sdPath;
    private String accountWebUrl = DEFAULT_ACCOUNT_WEB_URL;
    private String selectedServerName = "";
    private ScreenView screenView;
    private View assetLoadingOverlay;
    private TextView assetLoadingText;
    private boolean uiInitialized;
    private boolean assetPreparationRunning;
    private boolean emulatorStarted;
    private boolean keepAliveStarted;
    private String lastAssetPrepError;
    private int timerCount;
    private final Handler textInputHandler = new Handler();
    private AlertDialog textInputDialog;
    private int activeTextInputSerial;
    private boolean textInputPolling;
    private boolean screenTouchActive;
    private int lastScreenTouchX;
    private int lastScreenTouchY;
    private long lastScreenTouchMoveTimeMs;

    private TextView log;
    private int timerCount2;
    private int timerCount3;
    private boolean isPermissionRequestStart;
    private Button left,right,up,down,ok,cancel,confirm,btnChat,btnMap,btnAuto,btnExit,manageAccount,toggleVirtualPad;
    private View dpadUpRow;
    private View dpadDownRow;
    private View extraToolsDot;
    private VirtualPadView virtualPad;
    private boolean virtualPadEnabled;
    private int virtualPadDirectionMask;
    private boolean nativeLogPollingEnabled;

    private Handler m_screenDrawHandler = new Handler(){
        @Override
        public void handleMessage(Message msg) {
            int[] pixels = nativeGetPixels();
            if(screenView!=null) screenView.SetBuff(pixels);
            super.handleMessage(msg);
        }
    };
    private Handler m_printHandler = new Handler(){
        @Override
        public void handleMessage(Message msg) {
            if (!nativeLogPollingEnabled)
                return;
            String text = getPrintBuffer();
            if (text != null && text.length() > 0)
                System.out.println(text);
            super.handleMessage(msg);
        }
    };
    private Handler m_cpuState = new Handler(){
        @Override
        public void handleMessage(Message msg) {
            /* startEmu() also returns on clean host quit / onDestroy — not a crash. */
            if (isFinishing() || isDestroyed() || !emulatorStarted)
                return;
            try {
                new AlertDialog.Builder(MainActivity.this)
                        .setMessage("当前Cpu状态异常，建议重启")
                        .setTitle("提示")
                        .setPositiveButton("确定", null)
                        .show();
            } catch (Exception e) {
                Log.w(TAG, "cpu state dialog skipped", e);
            }
            super.handleMessage(msg);
        }
    };
    private final Runnable textInputPollRunnable = new Runnable() {
        @Override
        public void run() {
            pollTextInputDialog();
            pollAccountWebOpenRequest();
            if (textInputPolling) {
                textInputHandler.postDelayed(this, 150);
            }
        }
    };
    static {
        System.loadLibrary("mtksim");
    }
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        CheckAppPermission();
    }
    protected Context getMContext(){
        return this;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        boolean granted = grantResults.length > 0;
        for (int result : grantResults) {
            if (result != PackageManager.PERMISSION_GRANTED) {
                granted = false;
                break;
            }
        }

        if (requestCode == REQ_POST_NOTIFICATIONS) {
            if (emulatorStarted)
                startGameKeepAlive();
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
            return;
        }

        if(!granted){
            AlertDialog.Builder builder=new AlertDialog.Builder(this);
            builder.setMessage("储存权限申请失败");
            builder.setTitle("储存权限申请失败");
            builder.setPositiveButton("去授权", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface dialogInterface, int i) {
                    CheckAppPermission();
                }
            });
            builder.show();
        }else{
            CheckAppPermission();
        }
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }
    private void CheckAppPermission(){
        if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.R){
            boolean hasPermission = Environment.isExternalStorageManager();
            if(!hasPermission){
                AlertDialog.Builder builder = new AlertDialog.Builder(this);
                builder.setTitle("需要储存权限");
                builder.setMessage("需要允许管理所有文件，才能把资源解压到 /sdcard/JHOnline 并从该目录运行。");
                builder.setPositiveButton("去授权", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialogInterface, int i) {
                        openManageStorageSettings();
                    }
                });
                builder.setNegativeButton("退出", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialogInterface, int i) {
                        finish();
                    }
                });
                builder.show();
            }else Init();
        }else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if(checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED){
                requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE,Manifest.permission.WRITE_EXTERNAL_STORAGE},0);
            }else Init();
        }else Init();
    }

    private void openManageStorageSettings(){
        isPermissionRequestStart = true;
        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
        intent.setData(Uri.parse("package:" + getPackageName()));
        try {
            startActivity(intent);
        } catch (Exception e) {
            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if(isPermissionRequestStart) {
            isPermissionRequestStart = false;
            CheckAppPermission();
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    private void Init(){
        if(uiInitialized){
            prepareAssetsAsync();
            return;
        }
        uiInitialized = true;
        setContentView(R.layout.activity_main);

        assetLoadingOverlay = findViewById(R.id.asset_loading_overlay);
        assetLoadingText = findViewById(R.id.asset_loading_text);
        screenView = findViewById(R.id.screen);
        left = findViewById(R.id.left);
        right = findViewById(R.id.right);
        up = findViewById(R.id.up);
        down = findViewById(R.id.down);
        cancel = findViewById(R.id.cancel);
        confirm = findViewById(R.id.confirm);
        btnChat = findViewById(R.id.btn_chat);
        btnMap = findViewById(R.id.btn_map);
        btnAuto = findViewById(R.id.btn_auto);
        btnExit = findViewById(R.id.btn_exit);
        manageAccount = findViewById(R.id.manage_account);
        ok = findViewById(R.id.ok);
        extraToolsDot = findViewById(R.id.extra_tools_dot);
        findViewById(R.id.reboot).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                RunCpu();
            }
        });
        findViewById(R.id.restart).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                clearCacheAndExit();
            }
        });
        findViewById(R.id.dump).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                AlertDialog.Builder builder = new AlertDialog.Builder(getMContext());
                builder.setTitle("Cpu寄存器信息");
                builder.setMessage(getCpuInfo());
                builder.show();
            }
        });

        log = findViewById(R.id.log);

        bindKeyButton(left, 16);
        bindKeyButton(right, 17);
        bindKeyButton(up, 14);
        bindKeyButton(down, 15);
        bindKeyButton(ok, 18);
        bindKeyButton(cancel, 21);
        bindKeyButton(confirm, 20);
        /* Digit keys: keyEvent maps '0'..'9' -> skey 0..9 (chat=6, map=3, auto=1). */
        bindKeyButton(btnChat, '6');
        bindKeyButton(btnMap, '3');
        bindKeyButton(btnAuto, '1');

        manageAccount.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                openAccountWebsite();
            }
        });

        if (extraToolsDot != null) {
            extraToolsDot.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View view) {
                    showExtraToolsMenu(view);
                }
            });
        }

        virtualPad = findViewById(R.id.virtual_pad);
        toggleVirtualPad = findViewById(R.id.toggle_virtual_pad);
        dpadUpRow = findViewById(R.id.dpad_up_row);
        dpadDownRow = findViewById(R.id.dpad_down_row);
        virtualPadEnabled = getAssetPrefs().getBoolean(PREF_VIRTUAL_PAD_ENABLED, false);
        if (virtualPad != null) {
            virtualPad.setListener(new VirtualPadView.Listener() {
                @Override
                public void onPadDirectionChanged(int directionMask) {
                    applyVirtualPadDirection(directionMask);
                }
            });
        }
        if (toggleVirtualPad != null) {
            toggleVirtualPad.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View view) {
                    setVirtualPadEnabled(!virtualPadEnabled, true);
                }
            });
        }
        if (btnExit != null) {
            btnExit.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View view) {
                    exitGame();
                }
            });
        }
        applyVirtualPadEnabledUi();

        sdPath = new File(Environment.getExternalStorageDirectory(), SD_ROOT_DIR_NAME).getAbsolutePath();
        Log.i(TAG, "runtime root: " + sdPath);
        final MainActivity activity = this;
        screenView.setOnTouchListener((view, motionEvent) -> {
            int action = motionEvent.getActionMasked();
            if (action != MotionEvent.ACTION_DOWN &&
                    action != MotionEvent.ACTION_MOVE &&
                    action != MotionEvent.ACTION_UP &&
                    action != MotionEvent.ACTION_CANCEL) {
                return true;
            }

            float fx = view.getWidth();
            float fy = view.getHeight();
            if (fx <= 0 || fy <= 0) return true;

            int toX = (int)((motionEvent.getX() / fx) * 240);
            int toY = (int)((motionEvent.getY() / fy) * 400);
            int nativeAction = action == MotionEvent.ACTION_CANCEL ? MotionEvent.ACTION_UP : action;
            if (nativeAction == MotionEvent.ACTION_DOWN) {
                screenTouchActive = true;
                lastScreenTouchX = toX;
                lastScreenTouchY = toY;
                lastScreenTouchMoveTimeMs = motionEvent.getEventTime();
                activity.touchEvent(nativeAction, toX, toY);
                return true;
            }
            if (nativeAction == MotionEvent.ACTION_MOVE) {
                if (!screenTouchActive) return true;
                int dx = Math.abs(toX - lastScreenTouchX);
                int dy = Math.abs(toY - lastScreenTouchY);
                long eventTime = motionEvent.getEventTime();
                if (dx < TOUCH_MOVE_SLOP && dy < TOUCH_MOVE_SLOP) {
                    return true;
                }
                if (eventTime - lastScreenTouchMoveTimeMs < TOUCH_MOVE_MIN_INTERVAL_MS) {
                    return true;
                }
                lastScreenTouchX = toX;
                lastScreenTouchY = toY;
                lastScreenTouchMoveTimeMs = eventTime;
                activity.touchEvent(nativeAction, toX, toY);
                return true;
            }
            if (!screenTouchActive) return true;
            screenTouchActive = false;
            activity.touchEvent(nativeAction, toX, toY);
            return true;
        });
        prepareAssetsAsync();
    }

    private void prepareAssetsAsync(){
        if(assetPreparationRunning || emulatorStarted) return;
        assetPreparationRunning = true;
        showAssetLoading("正在解压游戏资源，请稍候…");
        ASSET_EXECUTOR.execute(new Runnable() {
            @Override
            public void run() {
                final boolean prepared = PrepareAssets();
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        if(isFinishing() || isDestroyed()) return;
                        assetPreparationRunning = false;
                        if(prepared){
                            emulatorStarted = true;
                            showAssetLoading(null);
                            ensureNotificationPermission();
                            StartRun();
                            startGameKeepAlive();
                            maybePromptBatteryOptimization();
                        }else{
                            showAssetLoading("资源解压失败，请重试");
                            showAssetPreparationError();
                        }
                    }
                });
            }
        });
    }

    private void showAssetLoading(String message){
        if(assetLoadingOverlay == null) return;
        if(message == null){
            assetLoadingOverlay.setVisibility(View.GONE);
            return;
        }
        if(assetLoadingText != null) assetLoadingText.setText(message);
        assetLoadingOverlay.setVisibility(View.VISIBLE);
    }

    private void showAssetPreparationError(){
        String detail = (lastAssetPrepError != null && lastAssetPrepError.length() > 0)
                ? lastAssetPrepError
                : "未知错误";
        new AlertDialog.Builder(this)
                .setTitle("资源解压失败")
                .setMessage("无法将游戏资源写入 /sdcard/JHOnline。\n"
                        + "请确认已授予「所有文件访问」权限、磁盘空间充足，然后重试。\n\n"
                        + "详情：" + detail)
                .setPositiveButton("重试", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialogInterface, int i) {
                        prepareAssetsAsync();
                    }
                })
                .setNegativeButton("退出", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialogInterface, int i) {
                        finish();
                    }
                })
                .setCancelable(false)
                .show();
    }

    private void bindKeyButton(View button, int key){
        final boolean[] pressed = {false};

        button.setOnTouchListener((view, evt) -> {
            switch (evt.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    if (pressed[0]) return true;
                    pressed[0] = true;
                    view.setPressed(true);
                    keyEvent(1, key);
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    if (!pressed[0]) return true;
                    pressed[0] = false;
                    view.setPressed(false);
                    keyEvent(0, key);
                    return true;
                default:
                    return true;
            }
        });
    }

    private void showExtraToolsMenu(View anchor) {
        if (anchor == null || isFinishing() || isDestroyed())
            return;
        PopupMenu menu = new PopupMenu(this, anchor);
        menu.getMenu().add(0, 1, 0, virtualPadEnabled ? "轮盘：开" : "轮盘：关");
        menu.getMenu().add(0, 2, 1, "清缓存");
        menu.getMenu().add(0, 3, 2, "退出");
        menu.setOnMenuItemClickListener(new PopupMenu.OnMenuItemClickListener() {
            @Override
            public boolean onMenuItemClick(android.view.MenuItem item) {
                int id = item.getItemId();
                if (id == 1) {
                    setVirtualPadEnabled(!virtualPadEnabled, true);
                    return true;
                }
                if (id == 2) {
                    confirmClearCache();
                    return true;
                }
                if (id == 3) {
                    exitGame();
                    return true;
                }
                return false;
            }
        });
        menu.show();
    }

    private void confirmClearCache() {
        if (isFinishing() || isDestroyed())
            return;
        new AlertDialog.Builder(this)
                .setTitle("清缓存")
                .setMessage("将删除本地游戏资源并退出，下次启动会重新解压。确定？")
                .setPositiveButton("确定", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        clearCacheAndExit();
                    }
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void clearCacheAndExit() {
        stopGameKeepAlive();
        clearAssetExtractionState();
        if (sdPath != null)
            deleteRecursive(new File(sdPath));
        finishAffinity();
        System.exit(0);
    }

    private void exitGame() {
        stopGameKeepAlive();
        finishAffinity();
        System.exit(0);
    }

    private void setVirtualPadEnabled(boolean enabled, boolean persist) {
        virtualPadEnabled = enabled;
        if (!enabled)
            applyVirtualPadDirection(0);
        if (virtualPad != null) {
            if (!enabled)
                virtualPad.releasePad();
            virtualPad.setVisibility(enabled ? View.VISIBLE : View.GONE);
        }
        int dpadVis = enabled ? View.GONE : View.VISIBLE;
        if (dpadUpRow != null)
            dpadUpRow.setVisibility(dpadVis);
        if (dpadDownRow != null)
            dpadDownRow.setVisibility(dpadVis);
        if (left != null)
            left.setVisibility(dpadVis);
        if (right != null)
            right.setVisibility(dpadVis);
        if (toggleVirtualPad != null)
            toggleVirtualPad.setText(enabled ? "轮盘开" : "轮盘");
        if (persist)
            getAssetPrefs().edit().putBoolean(PREF_VIRTUAL_PAD_ENABLED, enabled).apply();
    }

    private void applyVirtualPadEnabledUi() {
        setVirtualPadEnabled(virtualPadEnabled, false);
    }

    private void applyVirtualPadDirection(int directionMask) {
        syncVirtualPadKey(VirtualPadView.DIR_UP, PAD_KEY_UP, directionMask);
        syncVirtualPadKey(VirtualPadView.DIR_DOWN, PAD_KEY_DOWN, directionMask);
        syncVirtualPadKey(VirtualPadView.DIR_LEFT, PAD_KEY_LEFT, directionMask);
        syncVirtualPadKey(VirtualPadView.DIR_RIGHT, PAD_KEY_RIGHT, directionMask);
        virtualPadDirectionMask = directionMask;
    }

    private void syncVirtualPadKey(int bit, int key, int directionMask) {
        boolean wasDown = (virtualPadDirectionMask & bit) != 0;
        boolean nowDown = (directionMask & bit) != 0;
        if (wasDown == nowDown)
            return;
        keyEvent(nowDown ? 1 : 0, key);
    }

    private void openAccountWebsite(){
        /*
         * No modal AlertDialog: it steals focus and the on-screen pad stops
         * working the moment the prompt appears.  Copy URL, Toast, and open an
         * external browser task so MainActivity keeps receiving input.
         */
        if (isFinishing() || isDestroyed())
            return;
        refreshAccountWebUrlFromNative();
        final String url = (accountWebUrl != null && accountWebUrl.length() > 0)
                ? accountWebUrl : DEFAULT_ACCOUNT_WEB_URL;
        copyTextToClipboard("account_web_url", url);
        final Context app = getApplicationContext();
        /*
         * Delay browser so the emulator can finish the Wpay cancel-key
         * sequence while still in the foreground; opening immediately was
         * racing the guest UI and looked like a freeze.
         */
        Toast.makeText(app, "充值链接已复制，即将打开浏览器", Toast.LENGTH_SHORT).show();
        new Handler().postDelayed(new Runnable() {
            @Override
            public void run() {
                if (isFinishing() || isDestroyed())
                    return;
                launchExternalBrowser(url);
                if (screenView != null) {
                    screenView.setFocusable(true);
                    screenView.setFocusableInTouchMode(true);
                    screenView.requestFocus();
                }
            }
        }, 800);
    }

    private void copyTextToClipboard(String label, String text){
        try{
            ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm != null)
                cm.setPrimaryClip(ClipData.newPlainText(label, text));
        }catch (Exception e){
            Log.w(TAG, "clipboard failed", e);
        }
    }

    private void launchExternalBrowser(String url){
        Intent view = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
        view.addCategory(Intent.CATEGORY_BROWSABLE);
        /* NEW_TASK only — MULTIPLE_TASK was splitting/killing the game task. */
        view.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);

        Context app = getApplicationContext();
        PackageManager pm = app.getPackageManager();
        String[] preferred = {
                "com.android.chrome",
                "com.chrome.beta",
                "com.android.browser",
                "com.sec.android.app.sbrowser",
                "com.mi.globalbrowser",
                "com.opera.browser",
                "org.mozilla.firefox"
        };
        try{
            for (String pkg : preferred){
                view.setPackage(pkg);
                if (view.resolveActivity(pm) != null){
                    app.startActivity(view);
                    Toast.makeText(app, "已打开浏览器，多任务可切回游戏", Toast.LENGTH_LONG).show();
                    return;
                }
            }
            view.setPackage(null);
            Intent chooser = Intent.createChooser(view, "选择浏览器打开");
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            app.startActivity(chooser);
            Toast.makeText(app, "已打开浏览器，多任务可切回游戏", Toast.LENGTH_LONG).show();
        }catch (Exception e){
            Log.e(TAG, "unable to open account website", e);
            if (isFinishing() || isDestroyed())
                return;
            try {
                new AlertDialog.Builder(this)
                        .setTitle("无法打开网页")
                        .setMessage("请手动在浏览器粘贴：\n" + url)
                        .setPositiveButton("确定", null)
                        .show();
            } catch (Exception ignored) {
            }
        }
    }

    private void refreshAccountWebUrlFromNative(){
        try{
            String preferred = nativeGetPreferredServerName();
            if(preferred != null && preferred.length() > 0){
                List<ServerEntry> servers = loadServersConf();
                for(ServerEntry entry : servers){
                    if(preferred.equals(entry.name)){
                        selectedServerName = entry.name;
                        accountWebUrl = (entry.accountWeb != null &&
                                entry.accountWeb.length() > 0)
                                ? entry.accountWeb : DEFAULT_ACCOUNT_WEB_URL;
                        return;
                    }
                }
            }
        }catch (Exception e){
            Log.w(TAG, "refreshAccountWebUrlFromNative failed", e);
        }
    }

    private static final class ServerEntry {
        final String name;
        final String endpoint;
        final String accountWeb;

        ServerEntry(String name, String endpoint, String accountWeb) {
            this.name = name;
            this.endpoint = endpoint;
            this.accountWeb = accountWeb;
        }
    }

    private void showServerSelectThenStart(){
        final List<ServerEntry> servers = loadServersConf();
        if(servers.isEmpty()){
            Log.w(TAG, "servers.conf empty; using default endpoint/account web");
            applySelectedServer(null);
            StartRun();
            return;
        }

        final String[] labels = new String[servers.size()];
        int defaultIndex = 0;
        String remembered = getAssetPrefs().getString(PREF_SELECTED_SERVER, "");
        for(int i = 0; i < servers.size(); ++i){
            ServerEntry entry = servers.get(i);
            labels[i] = entry.name + "  (" + entry.endpoint + ")";
            if(remembered != null && remembered.equals(entry.name))
                defaultIndex = i;
        }
        final int[] chosen = {defaultIndex};

        new AlertDialog.Builder(this)
                .setTitle("选择区服")
                .setSingleChoiceItems(labels, defaultIndex, new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        chosen[0] = which;
                    }
                })
                .setPositiveButton("进入", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        int index = chosen[0];
                        if(index < 0 || index >= servers.size())
                            index = 0;
                        applySelectedServer(servers.get(index));
                        StartRun();
                    }
                })
                .setCancelable(false)
                .show();
    }

    private void applySelectedServer(ServerEntry entry){
        if(entry == null){
            selectedServerName = "";
            accountWebUrl = DEFAULT_ACCOUNT_WEB_URL;
            try{
                Os.setenv("CBE_NO_SERVER_SELECT", "1", true);
            }catch (Exception e){
                Log.w(TAG, "setenv CBE_NO_SERVER_SELECT failed", e);
            }
            return;
        }

        selectedServerName = entry.name;
        accountWebUrl = (entry.accountWeb != null && entry.accountWeb.length() > 0)
                ? entry.accountWeb : DEFAULT_ACCOUNT_WEB_URL;
        getAssetPrefs().edit().putString(PREF_SELECTED_SERVER, entry.name).apply();
        try{
            Os.setenv("CBE_SERVER", entry.name, true);
            Os.setenv("CBE_NO_SERVER_SELECT", "1", true);
            Log.i(TAG, "selected server=" + entry.name
                    + " endpoint=" + entry.endpoint
                    + " account_web=" + accountWebUrl);
        }catch (Exception e){
            Log.e(TAG, "setenv CBE_SERVER failed", e);
        }
    }

    private List<ServerEntry> loadServersConf(){
        List<ServerEntry> servers = new ArrayList<>();
        String current = "";
        File[] candidates = new File[]{
                new File(sdPath, "servers.conf"),
                new File(sdPath, "JHOnlineData/servers.conf")
        };
        BufferedReader reader = null;
        for(File file : candidates){
            if(!file.exists() || file.length() <= 0)
                continue;
            try{
                reader = new BufferedReader(new InputStreamReader(
                        new FileInputStream(file), StandardCharsets.UTF_8));
                String line;
                while((line = reader.readLine()) != null){
                    line = line.trim();
                    if(line.isEmpty() || line.startsWith("#") || line.startsWith(";"))
                        continue;
                    if(line.charAt(0) == '\uFEFF')
                        line = line.substring(1).trim();
                    int eq = line.indexOf('=');
                    if(eq <= 0)
                        continue;
                    String key = line.substring(0, eq).trim();
                    String value = line.substring(eq + 1).trim();
                    if(key.isEmpty() || value.isEmpty())
                        continue;
                    if("current".equals(key)){
                        current = value;
                        continue;
                    }
                    String endpoint = value;
                    String accountWeb = "";
                    int comma = value.indexOf(',');
                    if(comma >= 0){
                        String web = value.substring(comma + 1).trim();
                        if(web.startsWith("http://") || web.startsWith("https://")){
                            endpoint = value.substring(0, comma).trim();
                            accountWeb = web;
                        }
                    }
                    if(endpoint.isEmpty())
                        continue;
                    servers.add(new ServerEntry(key, endpoint, accountWeb));
                }
                reader.close();
                reader = null;
                if(!servers.isEmpty()){
                    if(current.length() > 0){
                        // Prefer current= as the remembered default when no prefs yet.
                        SharedPreferences prefs = getAssetPrefs();
                        if(!prefs.contains(PREF_SELECTED_SERVER))
                            prefs.edit().putString(PREF_SELECTED_SERVER, current).apply();
                    }
                    Log.i(TAG, "loaded servers.conf path=" + file.getAbsolutePath()
                            + " count=" + servers.size());
                    return servers;
                }
            }catch (Exception e){
                Log.w(TAG, "failed reading " + file.getAbsolutePath(), e);
            }finally{
                if(reader != null){
                    try{ reader.close(); }catch (Exception ignored){}
                    reader = null;
                }
            }
        }
        return servers;
    }

    private boolean PrepareAssets(){
        lastAssetPrepError = null;
        try{
            File root = new File(sdPath);
            if(!root.exists() && !root.mkdirs()) {
                throw new IllegalStateException("mkdir failed: " + root.getAbsolutePath());
            }

            int apkVersionCode = getAppVersionCode();
            int lastAppliedVersionCode = getAssetPrefs().getInt(PREF_INSTALLED_VERSION_CODE, -1);
            boolean versionUpgrade = apkVersionCode > 0 && apkVersionCode != lastAppliedVersionCode;
            boolean extractionReady = isAssetsExtractionReady(root);

            if (extractionReady && !versionUpgrade) {
                Log.i(TAG, "assets already extracted apkVersion=" + apkVersionCode);
                return true;
            }

            if (!extractionReady) {
                boolean forceOverwrite =
                        getAssetPrefs().getInt(PREF_ASSET_VERSION, -1) != ASSET_VERSION;
                copyAppAssets(root, forceOverwrite);
                String missing = findMissingRequiredAsset(root);
                if (missing != null)
                    throw new IllegalStateException("required asset missing: " + missing);
                markAssetsExtracted(root);
                Log.i(TAG, "assets extracted to " + root.getAbsolutePath()
                        + " forceOverwrite=" + forceOverwrite);
            } else {
                /*
                 * APK upgraded but extraction marker still valid: only refresh
                 * JHOnlineData (+ root servers.conf) so player saves outside
                 * that tree are left alone.
                 */
                forceRefreshUpgradeAssets(root);
                Log.i(TAG, "JHOnlineData refreshed for apkVersion "
                        + lastAppliedVersionCode + " -> " + apkVersionCode);
            }

            getAssetPrefs().edit()
                    .putInt(PREF_INSTALLED_VERSION_CODE, apkVersionCode)
                    .commit();
            return true;
        }catch (Exception e){
            lastAssetPrepError = e.getClass().getSimpleName() + ": " + e.getMessage();
            Log.e(TAG, "asset preparation failed", e);
            e.printStackTrace();
            return false;
        }
    }

    private int getAppVersionCode() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                return (int) getPackageManager()
                        .getPackageInfo(getPackageName(), 0).getLongVersionCode();
            }
            return getPackageManager().getPackageInfo(getPackageName(), 0).versionCode;
        } catch (Exception e) {
            Log.w(TAG, "getAppVersionCode failed", e);
            return -1;
        }
    }

    private void forceRefreshUpgradeAssets(File root) throws Exception {
        for (String entry : VERSION_UPGRADE_FORCE_ROOTS) {
            try {
                copyAssetPath(entry, root, true);
            } catch (Exception e) {
                Log.w(TAG, "upgrade force copy failed: " + entry, e);
                throw e;
            }
        }
        String missing = findMissingRequiredAsset(root);
        if (missing != null)
            throw new IllegalStateException(
                    "required asset missing after upgrade refresh: " + missing);
    }

    private SharedPreferences getAssetPrefs(){
        return getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
    }

    private boolean isAssetsExtractionReady(File root){
        SharedPreferences prefs = getAssetPrefs();
        return prefs.getBoolean(PREF_ASSETS_EXTRACTED, false)
                && prefs.getInt(PREF_ASSET_VERSION, -1) == ASSET_VERSION
                && new File(root, ".assets_ready_v" + ASSET_VERSION).exists()
                && hasRequiredAssets(root);
    }

    private boolean hasRequiredAssets(File root){
        return findMissingRequiredAsset(root) == null;
    }

    private String findMissingRequiredAsset(File root){
        String[] required = {
                "font_gb.uc3",
                "CBE/江湖OL.cbe",
                "JHOnlineData/mmGameMstarWqvga.cbm",
                "client_code_version.txt"
        };
        for (String path : required) {
            if (!hasFile(root, path))
                return path;
        }
        return null;
    }

    private boolean hasFile(File root, String path){
        File file = new File(root, path);
        return file.exists() && file.length() > 0;
    }

    private void markAssetsExtracted(File root) throws Exception{
        File marker = new File(root, ".assets_ready_v" + ASSET_VERSION);
        FileOutputStream markerOut = new FileOutputStream(marker, false);
        markerOut.write(("version=" + ASSET_VERSION + "\n").getBytes("UTF-8"));
        markerOut.flush();
        markerOut.close();
        getAssetPrefs()
                .edit()
                .putBoolean(PREF_ASSETS_EXTRACTED, true)
                .putInt(PREF_ASSET_VERSION, ASSET_VERSION)
                .commit();
    }

    private void clearAssetExtractionState(){
        getAssetPrefs()
                .edit()
                .remove(PREF_ASSETS_EXTRACTED)
                .remove(PREF_ASSET_VERSION)
                .remove(PREF_INSTALLED_VERSION_CODE)
                .commit();
    }

    private void copyAppAssets(File root, boolean forceOverwrite) throws Exception{
        for(String entry : REQUIRED_ASSET_ROOTS){
            copyAssetPath(entry, root, forceOverwrite);
        }
        for(String entry : OPTIONAL_ASSET_ROOTS){
            try{
                copyAssetPath(entry, root, forceOverwrite);
            }catch (Exception e){
                Log.i(TAG, "optional asset not copied: " + entry);
            }
        }
    }

    private void copyAssetPath(String assetPath, File root, boolean forceOverwrite) throws Exception{
        AssetManager assets = getAssets();
        String[] children = assets.list(assetPath);
        File out = new File(root, assetPath);
        if(children != null && children.length > 0){
            if(!out.exists() && !out.mkdirs()) throw new IllegalStateException(out.getAbsolutePath());
            for(String child : children){
                copyAssetPath(assetPath + "/" + child, root, forceOverwrite);
            }
            return;
        }
        if(!forceOverwrite && out.exists() && out.length() > 0) return;
        File parent = out.getParentFile();
        if(parent != null && !parent.exists() && !parent.mkdirs()) throw new IllegalStateException(parent.getAbsolutePath());
        InputStream ins = assets.open(assetPath);
        FileOutputStream fs = new FileOutputStream(out, false);
        byte[] cache = new byte[102400];
        for(;;){
            int read_len = ins.read(cache);
            if(read_len > 0) fs.write(cache,0,read_len);
            else break;
        }
        fs.flush();
        ins.close();fs.close();
    }

    private void deleteRecursive(File file){
        if(file == null || !file.exists()) return;
        if(file.isDirectory()){
            File[] children = file.listFiles();
            if(children != null){
                for(File child : children) deleteRecursive(child);
            }
        }
        file.delete();
    }
    public void RunCpu(){

        //开启CPU运行
        new Thread(new Runnable() {
            @Override
            public void run() {
                startEmu();
                Message m = new Message();
                m.what = 0;
                m_cpuState.sendMessage(m);
            }
        }).start();
    }
    private void ensureNotificationPermission() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU)
            return;
        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                == PackageManager.PERMISSION_GRANTED)
            return;
        requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},
                REQ_POST_NOTIFICATIONS);
    }

    private void startGameKeepAlive() {
        if (keepAliveStarted)
            return;
        try {
            GameKeepAliveService.start(getApplicationContext());
            keepAliveStarted = true;
            Log.i(TAG, "game keepalive service started");
        } catch (Exception e) {
            Log.e(TAG, "failed to start keepalive service", e);
        }
    }

    private void stopGameKeepAlive() {
        if (!keepAliveStarted && !emulatorStarted)
            return;
        try {
            GameKeepAliveService.stop(getApplicationContext());
        } catch (Exception e) {
            Log.w(TAG, "failed to stop keepalive service", e);
        }
        keepAliveStarted = false;
    }

    private void maybePromptBatteryOptimization() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M)
            return;
        SharedPreferences prefs = getAssetPrefs();
        if (prefs.getBoolean(PREF_BATTERY_OPT_PROMPTED, false))
            return;
        PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
        if (pm == null)
            return;
        if (pm.isIgnoringBatteryOptimizations(getPackageName())) {
            prefs.edit().putBoolean(PREF_BATTERY_OPT_PROMPTED, true).apply();
            return;
        }
        prefs.edit().putBoolean(PREF_BATTERY_OPT_PROMPTED, true).apply();
        try {
            new AlertDialog.Builder(this)
                    .setTitle("保持后台连接")
                    .setMessage("为避免锁屏或切后台后掉线，请允许本应用忽略电池优化，以便持续与服务器通信。")
                    .setPositiveButton("去设置", new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            openBatteryOptimizationSettings();
                        }
                    })
                    .setNegativeButton("稍后", null)
                    .show();
        } catch (Exception e) {
            Log.w(TAG, "battery optimization dialog skipped", e);
        }
    }

    private void openBatteryOptimizationSettings() {
        try {
            Intent intent = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
            intent.setData(Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception e) {
            try {
                startActivity(new Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS));
            } catch (Exception e2) {
                Log.w(TAG, "unable to open battery optimization settings", e2);
            }
        }
    }

    public void StartRun(){
        applyNativeLogMode();
        //传入rom固件目录
        int openResult = mtk_open(sdPath);
        Log.i(TAG, "mtk_open result: " + openResult + " log_mode=" + nativeGetLogMode());
        startTextInputPolling();
        
        //开启屏幕更新等任务
        new Thread(new Runnable() {
            @Override
            public void run() {
                Log.i(TAG, "taskListRun thread start");
                taskListRun();
            }
        }).start();
        new Thread(new Runnable() {
            @Override
            public void run() {
                while (true){

                    if(timerCount++ > 20){
                        timerCount=0;
                        //通知刷新屏幕渲染
                        m_screenDrawHandler.sendMessage(new Message());
                    }
                    if(nativeLogPollingEnabled && timerCount2++>10){
                        timerCount2=0;
                        m_printHandler.sendMessage(new Message());
                    }
                    if(timerCount3++>1000){
                        timerCount3=0;
                    }

                    try {
                        Thread.sleep(1);

                    }catch (Exception e){

                    }
                }
            }
        }).start();
        RunCpu();
    }

    private void startTextInputPolling(){
        if(textInputPolling) return;
        textInputPolling = true;
        textInputHandler.post(textInputPollRunnable);
    }

    private void stopTextInputPolling(){
        textInputPolling = false;
        textInputHandler.removeCallbacksAndMessages(null);
        if(textInputDialog != null && textInputDialog.isShowing()){
            textInputDialog.dismiss();
        }
        textInputDialog = null;
        activeTextInputSerial = 0;
    }

    private void pollTextInputDialog(){
        if(!nativeIsTextInputOpen()){
            activeTextInputSerial = 0;
            if(textInputDialog != null && textInputDialog.isShowing()){
                textInputDialog.dismiss();
            }
            textInputDialog = null;
            return;
        }

        int serial = nativeGetTextInputSerial();
        if(serial == 0 || serial == activeTextInputSerial){
            return;
        }

        activeTextInputSerial = serial;
        showTextInputDialog(serial);
    }

    private void pollAccountWebOpenRequest(){
        int serial = nativeConsumeAccountWebOpenRequest();
        if(serial == 0)
            return;
        Log.i(TAG, "native requested account web open serial=" + serial);
        openAccountWebsite();
    }

    private void showTextInputDialog(final int serial){
        if(textInputDialog != null && textInputDialog.isShowing()){
            textInputDialog.dismiss();
        }

        final boolean password = nativeIsTextInputPassword();
        int maxLen = nativeGetTextInputMaxLen();
        String prompt = nativeGetTextInputPrompt();
        String initialText = nativeGetTextInputText();
        if(prompt == null || prompt.length() == 0){
            prompt = password ? "请输入密码" : "请输入文本";
        }
        if(initialText == null){
            initialText = "";
        }

        final EditText input = new EditText(this);
        input.setSingleLine(true);
        input.setSelectAllOnFocus(false);
        input.setInputType(resolveDialogInputType(nativeGetTextInputType(), password));
        input.setImeOptions(EditorInfo.IME_ACTION_DONE);
        input.setText(initialText);
        input.setSelection(input.getText().length());
        if(maxLen > 1){
            input.setFilters(new InputFilter[]{new InputFilter.LengthFilter(maxLen - 1)});
        }

        FrameLayout container = new FrameLayout(this);
        int horizontalPadding = dp(20);
        container.setPadding(horizontalPadding, dp(4), horizontalPadding, 0);
        container.addView(input, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.WRAP_CONTENT));

        final boolean[] finished = {false};
        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle(prompt)
                .setView(container)
                .setPositiveButton("确定", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialogInterface, int i) {
                        finished[0] = true;
                        if(serial == activeTextInputSerial){
                            nativeSubmitTextInput(input.getText().toString(), false);
                        }
                    }
                })
                .setNegativeButton("取消", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialogInterface, int i) {
                        finished[0] = true;
                        if(serial == activeTextInputSerial){
                            nativeSubmitTextInput("", true);
                        }
                    }
                })
                .create();
        input.setOnEditorActionListener(new TextView.OnEditorActionListener() {
            @Override
            public boolean onEditorAction(TextView textView, int actionId, KeyEvent event) {
                boolean enterUp = event != null &&
                        event.getKeyCode() == KeyEvent.KEYCODE_ENTER &&
                        event.getAction() == KeyEvent.ACTION_UP;
                if(actionId == EditorInfo.IME_ACTION_DONE || enterUp){
                    finished[0] = true;
                    if(serial == activeTextInputSerial){
                        nativeSubmitTextInput(input.getText().toString(), false);
                    }
                    dialog.dismiss();
                    return true;
                }
                return false;
            }
        });
        dialog.setCanceledOnTouchOutside(false);
        dialog.setOnCancelListener(new DialogInterface.OnCancelListener() {
            @Override
            public void onCancel(DialogInterface dialogInterface) {
                if(!finished[0] && serial == activeTextInputSerial){
                    nativeSubmitTextInput("", true);
                }
            }
        });
        dialog.setOnDismissListener(new DialogInterface.OnDismissListener() {
            @Override
            public void onDismiss(DialogInterface dialogInterface) {
                if(textInputDialog == dialog){
                    textInputDialog = null;
                }
            }
        });
        dialog.setOnShowListener(new DialogInterface.OnShowListener() {
            @Override
            public void onShow(DialogInterface dialogInterface) {
                input.requestFocus();
                if(dialog.getWindow() != null){
                    dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
                }
                input.postDelayed(new Runnable() {
                    @Override
                    public void run() {
                        InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
                        if(imm != null){
                            imm.showSoftInput(input, InputMethodManager.SHOW_IMPLICIT);
                        }
                    }
                }, 120);
            }
        });

        textInputDialog = dialog;
        dialog.show();
    }

    private int resolveDialogInputType(int nativeInputType, boolean password){
        int inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
        if(password){
            return inputType | InputType.TYPE_TEXT_VARIATION_PASSWORD;
        }
        return inputType | InputType.TYPE_TEXT_VARIATION_NORMAL;
    }

    private int dp(int value){
        return (int)(value * getResources().getDisplayMetrics().density + 0.5f);
    }

    @Override
    protected void onPause() {
        if (virtualPad != null)
            virtualPad.releasePad();
        applyVirtualPadDirection(0);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        stopTextInputPolling();
        if (virtualPad != null)
            virtualPad.releasePad();
        applyVirtualPadDirection(0);
        boolean wasStarted = emulatorStarted;
        emulatorStarted = false;
        stopGameKeepAlive();
        if (wasStarted) {
            onExit();
        }
        super.onDestroy();
    }

    public native String getPrintBuffer();
    public native int mtk_open(String sdPath);
    public native int[] nativeGetPixels();
    public native void startEmu();
    public native void taskListRun();
    public native String getCpuInfo();
    public native void onExit();
    public native void nativeSetLogMode(String mode);
    public native String nativeGetLogMode();
    public native boolean nativeIsTextInputOpen();
    public native int nativeGetTextInputSerial();
    public native boolean nativeIsTextInputPassword();
    public native int nativeGetTextInputMaxLen();
    public native int nativeGetTextInputType();
    public native String nativeGetTextInputText();
    public native String nativeGetTextInputPrompt();
    public native void nativeSubmitTextInput(String text, boolean cancelled);
    public native int nativeConsumeAccountWebOpenRequest();
    public native String nativeGetPreferredServerName();

    public native void keyEvent(int isPress,int key);
    public native void touchEvent(int touchType,int x,int y);

    private void applyNativeLogMode() {
        SharedPreferences prefs = getSharedPreferences("cbe_runtime", MODE_PRIVATE);
        String mode = prefs.getString("log_mode", null);
        if (mode == null || mode.length() == 0)
            mode = BuildConfig.DEBUG ? "debug" : "production";
        nativeSetLogMode(mode);
        nativeLogPollingEnabled = "debug".equals(mode) || "verbose".equals(mode) || "all".equals(mode);
    }
}
