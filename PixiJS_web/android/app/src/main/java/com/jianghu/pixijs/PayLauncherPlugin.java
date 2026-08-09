package com.jianghu.pixijs;

import android.content.ActivityNotFoundException;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Base64;
import android.util.Log;
import android.widget.Toast;

import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;

import java.io.OutputStream;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;

/**
 * Pay URL launcher.
 * Alipay V免签 returns https://qr.alipay.com/fkx… — open as-is via ACTION_VIEW
 * (no setPackage). Album-scan of that QR on some Alipay builds lands on 名片;
 * Intent VIEW of the https URL is the working path (matches account-center
 * 「打开支付链接」).
 */
@CapacitorPlugin(name = "PayLauncher")
public class PayLauncherPlugin extends Plugin {
    private static final String TAG = "PayLauncher";
    private static final String PKG_WECHAT = "com.tencent.mm";

    private static boolean isSafePayUrl(String url) {
        if (url == null) return false;
        String s = url.trim();
        if (s.isEmpty() || s.indexOf('\n') >= 0 || s.indexOf('\r') >= 0) {
            return false;
        }
        String lower = s.toLowerCase();
        return lower.startsWith("http://")
                || lower.startsWith("https://")
                || lower.startsWith("wxp://")
                || lower.startsWith("weixin://")
                || lower.startsWith("alipays://")
                || lower.startsWith("alipay://")
                || lower.startsWith("alipayqr://");
    }

    private static String schemeOf(String url) {
        int i = url.indexOf(':');
        if (i <= 0) return "";
        return url.substring(0, i).toLowerCase();
    }

    private static String urlPreview(String url) {
        if (url == null) return "";
        return url.length() <= 120 ? url : url.substring(0, 120) + "…";
    }

    private boolean openExternalView(String url) {
        Context app = getContext().getApplicationContext();
        Intent view = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
        view.addCategory(Intent.CATEGORY_BROWSABLE);
        view.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            app.startActivity(view);
            return true;
        } catch (ActivityNotFoundException e) {
            Log.w(TAG, "ActivityNotFound " + urlPreview(url));
            return false;
        } catch (Exception e) {
            Log.w(TAG, "startActivity failed " + urlPreview(url), e);
            return false;
        }
    }

    private boolean openWeChatPayUrl(String url) {
        Context app = getContext().getApplicationContext();
        Intent targeted = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
        targeted.setPackage(PKG_WECHAT);
        targeted.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            app.startActivity(targeted);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "wechat targeted failed", e);
        }
        return openExternalView(url);
    }

    /**
     * Alipay: 1) https as-is  2) official-style saId=10000007&qrcode= with ?_s=web-other
     * Never bare alipays:// and never setPackage.
     */
    private boolean openAlipayPayUrl(String url) {
        String lower = url.toLowerCase();
        if (lower.startsWith("http://") || lower.startsWith("https://")) {
            if (openExternalView(url)) {
                return true;
            }
            try {
                String qr = url;
                if (!qr.contains("?")) {
                    qr = qr + "?_s=web-other";
                } else if (!qr.contains("_s=")) {
                    qr = qr + "&_s=web-other";
                }
                String encoded = URLEncoder.encode(qr, StandardCharsets.UTF_8.name());
                String deep =
                        "alipays://platformapi/startapp?saId=10000007&qrcode=" + encoded;
                Log.i(TAG, "alipay deep fallback " + urlPreview(deep));
                return openExternalView(deep);
            } catch (Exception e) {
                Log.w(TAG, "alipay deep encode failed", e);
                return false;
            }
        }
        /* Already alipays/alipay — open as-is, no package. */
        return openExternalView(url);
    }

    @PluginMethod
    public void open(PluginCall call) {
        String url = call.getString("url");
        if (!isSafePayUrl(url)) {
            call.reject("invalid pay url");
            return;
        }
        url = url.trim();
        String scheme = schemeOf(url);
        boolean wxp = "wxp".equals(scheme);
        boolean wechat = wxp || "weixin".equals(scheme);
        boolean alipay = "alipays".equals(scheme)
                || "alipay".equals(scheme)
                || "alipayqr".equals(scheme)
                || url.toLowerCase().contains("alipay.com")
                || url.toLowerCase().contains("alipay");

        boolean opened = false;
        if (wxp) {
            opened = false;
        } else if (wechat) {
            opened = openWeChatPayUrl(url);
        } else if (alipay || "http".equals(scheme) || "https".equals(scheme)) {
            opened = alipay || url.toLowerCase().contains("qr.alipay.com")
                    ? openAlipayPayUrl(url)
                    : openExternalView(url);
        } else {
            opened = openExternalView(url);
        }

        boolean needQr = wxp || !opened;
        if (!opened && !wxp) {
            toast("无法自动打开支付，请用二维码");
        }

        JSObject ret = new JSObject();
        ret.put("opened", opened);
        ret.put("needQr", needQr);
        ret.put("scheme", scheme);
        Log.i(TAG, "open scheme=" + scheme
                + " opened=" + opened
                + " needQr=" + needQr
                + " url=" + urlPreview(url));
        call.resolve(ret);
    }

    @PluginMethod
    public void savePngToGallery(PluginCall call) {
        String b64 = call.getString("base64");
        if (b64 == null || b64.isEmpty()) {
            call.reject("missing base64");
            return;
        }
        int comma = b64.indexOf(',');
        if (b64.startsWith("data:") && comma > 0) {
            b64 = b64.substring(comma + 1);
        }
        byte[] bytes;
        try {
            bytes = Base64.decode(b64, Base64.DEFAULT);
        } catch (Exception e) {
            call.reject("bad base64", e);
            return;
        }
        if (bytes == null || bytes.length == 0) {
            call.reject("empty image");
            return;
        }
        Bitmap bmp = BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
        if (bmp == null) {
            call.reject("decode failed");
            return;
        }

        Context ctx = getContext().getApplicationContext();
        String name = "jh_pay_qr_" + System.currentTimeMillis() + ".png";
        try {
            ContentResolver resolver = ctx.getContentResolver();
            ContentValues values = new ContentValues();
            values.put(MediaStore.Images.Media.DISPLAY_NAME, name);
            values.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                values.put(
                        MediaStore.Images.Media.RELATIVE_PATH,
                        Environment.DIRECTORY_PICTURES + "/JianghuPay");
                values.put(MediaStore.Images.Media.IS_PENDING, 1);
            }
            Uri saved = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
            if (saved == null) {
                call.reject("insert failed");
                return;
            }
            OutputStream out = resolver.openOutputStream(saved);
            if (out == null) {
                call.reject("open stream failed");
                return;
            }
            try {
                if (!bmp.compress(Bitmap.CompressFormat.PNG, 100, out)) {
                    call.reject("compress failed");
                    return;
                }
                out.flush();
            } finally {
                out.close();
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                values.clear();
                values.put(MediaStore.Images.Media.IS_PENDING, 0);
                resolver.update(saved, values, null, null);
            }
            toast("二维码已保存到相册");
            JSObject ret = new JSObject();
            ret.put("saved", true);
            ret.put("uri", saved.toString());
            call.resolve(ret);
        } catch (Exception e) {
            Log.e(TAG, "savePngToGallery", e);
            toast("保存相册失败");
            call.reject("save failed", e);
        } finally {
            bmp.recycle();
        }
    }

    @PluginMethod
    public void openWeChatScan(PluginCall call) {
        Context app = getContext().getApplicationContext();
        boolean opened = false;
        for (String u : new String[] {"weixin://scanqrcode", "weixin://dl/scan"}) {
            Intent targeted = new Intent(Intent.ACTION_VIEW, Uri.parse(u));
            targeted.setPackage(PKG_WECHAT);
            targeted.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            try {
                app.startActivity(targeted);
                opened = true;
                break;
            } catch (Exception ignored) {
                /* try next */
            }
        }
        if (!opened) {
            Intent launch = app.getPackageManager().getLaunchIntentForPackage(PKG_WECHAT);
            if (launch != null) {
                launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                try {
                    app.startActivity(launch);
                    opened = true;
                } catch (Exception ignored) {
                    /* missing */
                }
            }
        }
        toast(opened ? "微信：扫一扫 → 相册 → 选刚保存的码" : "未安装微信");
        JSObject ret = new JSObject();
        ret.put("opened", opened);
        call.resolve(ret);
    }

    /** Scanner only — not used for Alipay payUrl (album path opens 名片 on some builds). */
    @PluginMethod
    public void openAlipayScan(PluginCall call) {
        boolean opened = openExternalView(
                "alipays://platformapi/startapp?saId=10000007");
        toast(opened ? "支付宝：扫一扫 → 相册 → 选刚保存的码" : "未安装支付宝");
        JSObject ret = new JSObject();
        ret.put("opened", opened);
        call.resolve(ret);
    }

    private void toast(String msg) {
        try {
            Toast.makeText(getContext().getApplicationContext(), msg, Toast.LENGTH_LONG).show();
        } catch (Exception ignored) {
            /* OEM */
        }
    }
}
