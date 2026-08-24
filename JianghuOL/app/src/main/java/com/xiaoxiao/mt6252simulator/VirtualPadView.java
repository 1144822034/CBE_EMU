package com.xiaoxiao.mt6252simulator;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PointF;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.MotionEvent;
import android.view.View;

/**
 * Floating stick: appears under the finger and the base follows when dragged
 * past the rim. Covers the full game area while enabled.
 */
public class VirtualPadView extends View {
    public static final int DIR_UP = 1;
    public static final int DIR_DOWN = 2;
    public static final int DIR_LEFT = 4;
    public static final int DIR_RIGHT = 8;

    public interface Listener {
        void onPadDirectionChanged(int directionMask);
    }

    private final Paint basePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ringPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint knobPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final PointF knob = new PointF();
    private Listener listener;
    private boolean touching;
    private int activeMask;
    private float baseRadius;
    private float knobRadius;
    private float deadZone;
    private float centerX;
    private float centerY;

    public VirtualPadView(Context context) {
        super(context);
        init();
    }

    public VirtualPadView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public VirtualPadView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        basePaint.setStyle(Paint.Style.FILL);
        basePaint.setColor(0x66202830);
        ringPaint.setStyle(Paint.Style.STROKE);
        ringPaint.setStrokeWidth(dp(2.5f));
        ringPaint.setColor(0xAAC8D0D8);
        knobPaint.setStyle(Paint.Style.FILL);
        knobPaint.setColor(0xDDE8EEF4);
        setClickable(true);
        setFocusable(true);
        setWillNotDraw(false);
        updateMetrics();
    }

    private float dp(float value) {
        return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
                getResources().getDisplayMetrics());
    }

    private void updateMetrics() {
        baseRadius = dp(52f);
        knobRadius = dp(22f);
        deadZone = dp(16f);
    }

    public void setListener(Listener listener) {
        this.listener = listener;
    }

    public void releasePad() {
        touching = false;
        knob.set(0f, 0f);
        setDirectionMask(0);
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!touching)
            return;
        canvas.drawCircle(centerX, centerY, baseRadius, basePaint);
        canvas.drawCircle(centerX, centerY, baseRadius, ringPaint);
        canvas.drawCircle(centerX + knob.x, centerY + knob.y, knobRadius, knobPaint);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        float x = event.getX();
        float y = event.getY();
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                centerX = x;
                centerY = y;
                touching = true;
                updateFromTouch(x, y);
                getParent().requestDisallowInterceptTouchEvent(true);
                return true;
            case MotionEvent.ACTION_MOVE:
                if (!touching)
                    return false;
                updateFromTouch(x, y);
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                if (!touching)
                    return false;
                getParent().requestDisallowInterceptTouchEvent(false);
                releasePad();
                return true;
            default:
                return super.onTouchEvent(event);
        }
    }

    private void updateFromTouch(float x, float y) {
        float dx = x - centerX;
        float dy = y - centerY;
        float dist = (float) Math.hypot(dx, dy);
        float max = Math.max(1f, baseRadius - knobRadius * 0.35f);

        /* Drag beyond rim: slide the base so the stick follows the finger. */
        if (dist > max && dist > 0.001f) {
            float excess = dist - max;
            float inv = excess / dist;
            centerX += dx * inv;
            centerY += dy * inv;
            clampCenter();
            dx = x - centerX;
            dy = y - centerY;
            dist = (float) Math.hypot(dx, dy);
        }

        if (dist > max && dist > 0.001f) {
            float scale = max / dist;
            dx *= scale;
            dy *= scale;
            dist = max;
        }
        knob.set(dx, dy);

        int mask = 0;
        if (dist >= deadZone) {
            double angle = Math.toDegrees(Math.atan2(-dy, dx));
            if (angle < 0)
                angle += 360.0;
            if (angle >= 337.5 || angle < 22.5)
                mask = DIR_RIGHT;
            else if (angle < 67.5)
                mask = DIR_RIGHT | DIR_UP;
            else if (angle < 112.5)
                mask = DIR_UP;
            else if (angle < 157.5)
                mask = DIR_LEFT | DIR_UP;
            else if (angle < 202.5)
                mask = DIR_LEFT;
            else if (angle < 247.5)
                mask = DIR_LEFT | DIR_DOWN;
            else if (angle < 292.5)
                mask = DIR_DOWN;
            else
                mask = DIR_RIGHT | DIR_DOWN;
        }
        setDirectionMask(mask);
        invalidate();
    }

    private void clampCenter() {
        float pad = baseRadius + dp(4f);
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0)
            return;
        if (centerX < pad)
            centerX = pad;
        if (centerX > w - pad)
            centerX = w - pad;
        if (centerY < pad)
            centerY = pad;
        if (centerY > h - pad)
            centerY = h - pad;
    }

    private void setDirectionMask(int mask) {
        if (mask == activeMask)
            return;
        activeMask = mask;
        if (listener != null)
            listener.onPadDirectionChanged(activeMask);
    }

    @Override
    protected void onDetachedFromWindow() {
        releasePad();
        super.onDetachedFromWindow();
    }
}
