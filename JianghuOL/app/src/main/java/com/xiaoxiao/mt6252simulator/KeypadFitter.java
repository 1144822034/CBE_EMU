package com.xiaoxiao.mt6252simulator;

import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

/**
 * Scale D-pad / soft keys to fill {@code controls_panel} after {@link CbeShellLayout}
 * assigns leftover height. Extra space stays under the keys, not between LCD and keys.
 */
public final class KeypadFitter {
    private static final float GAP_DP = 2f;
    private static final float MIN_DPAD_DP = 40f;
    private static final float MIN_SOFT_W_DP = 56f;
    private static volatile float dpadFillScale = 0.90f;

    private KeypadFitter() {}

    /** Overlay / APK: 1 = fill keypad, 0.90 = slightly smaller. Clamped to 0.50–1.00. */
    public static void setDpadFillScale(float scale) {
        if (scale < 0.50f) scale = 0.50f;
        if (scale > 1.00f) scale = 1.00f;
        dpadFillScale = scale;
    }

    public static void attach(
            View panel,
            View[] dpad,
            View ok,
            View[] softKeys,
            View extraDot) {
        if (panel == null) return;
        panel.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> {
            if (r - l == or - ol && b - t == ob - ot) return;
            apply(panel, dpad, ok, softKeys, extraDot);
        });
        panel.post(() -> apply(panel, dpad, ok, softKeys, extraDot));
    }

    static void apply(View panel, View[] dpad, View ok, View[] softKeys, View extraDot) {
        int panelH = panel.getHeight();
        int panelW = panel.getWidth();
        if (panelH <= 0 || panelW <= 0) return;

        float density = panel.getResources().getDisplayMetrics().density;
        int gap = Math.max(1, Math.round(GAP_DP * density));
        int extra = 0;
        if (extraDot != null && extraDot.getVisibility() != View.GONE) {
            extra = extraDot.getLayoutParams() != null
                    ? extraDot.getLayoutParams().height
                    : extraDot.getHeight();
            if (extra <= 0) extra = Math.round(22f * density);
            extra += gap;
        }
        int inner = panelH - panel.getPaddingTop() - panel.getPaddingBottom() - extra;
        if (inner < Math.round(MIN_DPAD_DP * density)) return;
        int centerW = Math.max(1, panelW / 2);
        int dpadSize = Math.min((inner - 2 * gap) / 3, (centerW - 2 * gap) / 3);
        dpadSize = Math.round(dpadSize * dpadFillScale);
        if (dpadSize < 1) return;

        if (dpad != null) {
            float dpadText = dpadSize * 0.30f;
            float okText = dpadSize * 0.24f;
            for (View view : dpad) {
                if (view == null) continue;
                boolean isOk = ok != null && view == ok;
                setBox(view, dpadSize, dpadSize);
                if (view instanceof TextView) {
                    ((TextView) view).setTextSize(TypedValue.COMPLEX_UNIT_PX, isOk ? okText : dpadText);
                }
            }
        }

        int softH = (inner - 2 * gap) / 3;
        int colW = Math.max(1, panelW / 4 - Math.round(8f * density));
        int softW = Math.min(colW, Math.max(Math.round(MIN_SOFT_W_DP * density), Math.round(softH * 1.65f)));
        if (softW > colW) softW = colW;
        float softText = Math.max(softH * 0.28f, 12f * density);
        if (softKeys != null) {
            for (View view : softKeys) {
                if (view == null) continue;
                setBox(view, softW, softH);
                if (view instanceof TextView) {
                    ((TextView) view).setTextSize(TypedValue.COMPLEX_UNIT_PX, softText);
                }
            }
        }
    }

    private static void setBox(View view, int w, int h) {
        ViewGroup.LayoutParams lp = view.getLayoutParams();
        if (lp == null) {
            lp = new ViewGroup.LayoutParams(w, h);
        } else if (lp.width == w && lp.height == h) {
            return;
        } else {
            lp.width = w;
            lp.height = h;
        }
        view.setMinimumWidth(Math.min(w, h));
        view.setMinimumHeight(Math.min(w, h));
        view.setLayoutParams(lp);
    }
}
