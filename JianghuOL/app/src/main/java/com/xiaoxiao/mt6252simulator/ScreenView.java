package com.xiaoxiao.mt6252simulator;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.View;

/**
 * LCD present aligned with PixiJS_web:
 * - logical buffer stays 240×400
 * - integer nearest-neighbor upscale in software (HW Canvas often ignores
 *   {@code Paint.setFilterBitmap(false)} and bilinear-filters → sand moiré)
 * - draw the upscaled bitmap 1:1 (no stretch)
 */
public class ScreenView extends View {
    private static final int SCREEN_WIDTH = 240;
    private static final int SCREEN_HEIGHT = 400;

    private final Bitmap lcdBitmap;
    private final int[] lcdPixels = new int[SCREEN_WIDTH * SCREEN_HEIGHT];
    private final Paint paint = new Paint();

    private Bitmap presentBitmap;
    private int[] presentPixels;
    private int presentScale = 0;
    private boolean lcdDirty = true;

    public ScreenView(Context context, AttributeSet attrs) {
        super(context, attrs);
        lcdBitmap = Bitmap.createBitmap(SCREEN_WIDTH, SCREEN_HEIGHT, Bitmap.Config.ARGB_8888);
        /* Prevent DisplayMetrics density from auto-scaling the blit. */
        lcdBitmap.setDensity(Bitmap.DENSITY_NONE);
        paint.setFilterBitmap(false);
        paint.setAntiAlias(false);
        paint.setDither(false);
    }

    public void SetBuff(int[] pixels) {
        if (pixels == null || pixels.length < lcdPixels.length) {
            return;
        }
        System.arraycopy(pixels, 0, lcdPixels, 0, lcdPixels.length);
        lcdBitmap.setPixels(lcdPixels, 0, SCREEN_WIDTH, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        lcdDirty = true;
        invalidate();
    }

    private int chooseScale(int viewW, int viewH) {
        int scale = Math.min(viewW / SCREEN_WIDTH, viewH / SCREEN_HEIGHT);
        return scale < 1 ? 1 : scale;
    }

    private void ensurePresentScale(int scale) {
        if (scale == presentScale && presentBitmap != null && presentPixels != null) {
            return;
        }
        if (presentBitmap != null) {
            presentBitmap.recycle();
            presentBitmap = null;
        }
        presentScale = scale;
        int pw = SCREEN_WIDTH * scale;
        int ph = SCREEN_HEIGHT * scale;
        presentBitmap = Bitmap.createBitmap(pw, ph, Bitmap.Config.ARGB_8888);
        presentBitmap.setDensity(Bitmap.DENSITY_NONE);
        presentPixels = new int[pw * ph];
        lcdDirty = true;
    }

    private void blitNearest() {
        if (presentBitmap == null || presentPixels == null) {
            return;
        }
        int scale = presentScale;
        if (scale <= 1) {
            return;
        }
        int pw = SCREEN_WIDTH * scale;
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            int srcRow = y * SCREEN_WIDTH;
            for (int sy = 0; sy < scale; sy++) {
                int dstRow = (y * scale + sy) * pw;
                for (int x = 0; x < SCREEN_WIDTH; x++) {
                    int c = lcdPixels[srcRow + x];
                    int dst = dstRow + x * scale;
                    for (int sx = 0; sx < scale; sx++) {
                        presentPixels[dst + sx] = c;
                    }
                }
            }
        }
        presentBitmap.setPixels(presentPixels, 0, pw, 0, 0, pw, SCREEN_HEIGHT * scale);
        lcdDirty = false;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int maxWidth = MeasureSpec.getSize(widthMeasureSpec);
        int maxHeight = MeasureSpec.getSize(heightMeasureSpec);
        int widthMode = MeasureSpec.getMode(widthMeasureSpec);
        int heightMode = MeasureSpec.getMode(heightMeasureSpec);

        if (widthMode == MeasureSpec.UNSPECIFIED || maxWidth <= 0) {
            maxWidth = SCREEN_WIDTH;
        }
        if (heightMode == MeasureSpec.UNSPECIFIED || maxHeight <= 0) {
            maxHeight = SCREEN_HEIGHT;
        }

        int scale = chooseScale(maxWidth, maxHeight);
        int measuredWidth = SCREEN_WIDTH * scale;
        int measuredHeight = SCREEN_HEIGHT * scale;
        if (widthMode == MeasureSpec.EXACTLY) {
            measuredWidth = maxWidth;
        }
        if (heightMode == MeasureSpec.EXACTLY) {
            measuredHeight = maxHeight;
        }
        setMeasuredDimension(measuredWidth, measuredHeight);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        int scale = chooseScale(getWidth(), getHeight());
        ensurePresentScale(scale);
        if (scale <= 1) {
            int x = (getWidth() - SCREEN_WIDTH) / 2;
            int y = (getHeight() - SCREEN_HEIGHT) / 2;
            canvas.drawBitmap(lcdBitmap, x, y, paint);
            return;
        }
        if (lcdDirty) {
            blitNearest();
        }
        int drawW = SCREEN_WIDTH * scale;
        int drawH = SCREEN_HEIGHT * scale;
        int x = (getWidth() - drawW) / 2;
        int y = (getHeight() - drawH) / 2;
        canvas.drawBitmap(presentBitmap, x, y, paint);
    }
}
