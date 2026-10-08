package com.xiaoxiao.mt6252simulator;

import android.content.Context;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.View;
import android.widget.LinearLayout;

/**
 * Vertical shell: LCD keeps 240:400 and sits at the top; the keypad eats leftover
 * height so the black letterbox between game and keys disappears.
 */
public class CbeShellLayout extends LinearLayout {
    private static final int SCREEN_W = 240;
    private static final int SCREEN_H = 400;
    private static final float MIN_KEYPAD_DP = 152f;

    public CbeShellLayout(Context context) {
        super(context);
        init();
    }

    public CbeShellLayout(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public CbeShellLayout(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        setOrientation(VERTICAL);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int w = MeasureSpec.getSize(widthMeasureSpec);
        int h = MeasureSpec.getSize(heightMeasureSpec);
        if (getChildCount() >= 2 && w > 0 && h > 0
                && MeasureSpec.getMode(heightMeasureSpec) != MeasureSpec.UNSPECIFIED) {
            int minPad = Math.round(TypedValue.applyDimension(
                    TypedValue.COMPLEX_UNIT_DIP, MIN_KEYPAD_DP, getResources().getDisplayMetrics()));
            /* Prefer an integer multiple of 400px height so ScreenView can
             * nearest-neighbor upscale (avoids sand-tile moiré). */
            int maxGame = Math.max(1, h - minPad);
            int scale = Math.min(w / SCREEN_W, maxGame / SCREEN_H);
            if (scale < 1) {
                scale = 1;
            }
            int gameH = SCREEN_H * scale;
            if (gameH > maxGame) {
                gameH = maxGame;
            }
            int padH = Math.max(minPad, h - gameH);
            if (gameH + padH > h) {
                padH = Math.min(padH, h);
                gameH = Math.max(1, h - padH);
            }
            setExactHeight(getChildAt(0), gameH);
            setExactHeight(getChildAt(1), padH);
        }
        super.onMeasure(widthMeasureSpec, heightMeasureSpec);
    }

    private static void setExactHeight(View child, int height) {
        LayoutParams lp = (LayoutParams) child.getLayoutParams();
        if (lp == null) {
            lp = new LayoutParams(LayoutParams.MATCH_PARENT, height);
            child.setLayoutParams(lp);
            return;
        }
        lp.width = LayoutParams.MATCH_PARENT;
        lp.height = height;
        lp.weight = 0f;
    }
}
