package com.xiaoxiao.mt6252simulator;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.view.View;

public class ScreenView extends View {
    private static final int SCREEN_WIDTH = 240;
    private static final int SCREEN_HEIGHT = 400;
    private Bitmap bitmap;

    public ScreenView(Context context, AttributeSet attrs) {
        super(context, attrs);

        bitmap = Bitmap.createBitmap(SCREEN_WIDTH, SCREEN_HEIGHT, Bitmap.Config.ARGB_8888);
    }
    public void SetBuff(int[] pixels){
        bitmap.setPixels(pixels, 0, SCREEN_WIDTH, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        this.invalidate();
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int maxWidth = MeasureSpec.getSize(widthMeasureSpec);
        int maxHeight = MeasureSpec.getSize(heightMeasureSpec);

        if (MeasureSpec.getMode(widthMeasureSpec) == MeasureSpec.UNSPECIFIED || maxWidth <= 0) {
            maxWidth = SCREEN_WIDTH;
        }
        if (MeasureSpec.getMode(heightMeasureSpec) == MeasureSpec.UNSPECIFIED || maxHeight <= 0) {
            maxHeight = SCREEN_HEIGHT;
        }

        int measuredWidth = maxWidth;
        int measuredHeight = measuredWidth * SCREEN_HEIGHT / SCREEN_WIDTH;
        if (measuredHeight > maxHeight) {
            measuredHeight = maxHeight;
            measuredWidth = measuredHeight * SCREEN_WIDTH / SCREEN_HEIGHT;
        }

        setMeasuredDimension(measuredWidth, measuredHeight);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        /* Do not clear to black first — that flashes between frames when the
         * parent invalidates while the emulator is still composing. */
        Rect src = new Rect(0, 0, bitmap.getWidth(), bitmap.getHeight());
        Rect dst = new Rect(0, 0, getWidth(), getHeight());
        canvas.drawBitmap(bitmap, src, dst, null);
    }

}
