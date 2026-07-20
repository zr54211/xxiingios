package com.onecvn.addin.scanner;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;

// Полноэкранный оверлей с SurfaceView для превью камеры, рамкой наводки и
// кнопкой закрытия. Все методы вызываются нативной частью в UI-потоке
// (через UiBridge). События поверхности/тапов/закрытия уходят в натив.
public final class ScannerOverlay implements SurfaceHolder.Callback {

    // Камера отдаёт превью фиксированного размера, SurfaceView масштабирует.
    static final int BUFFER_WIDTH = 1280;
    static final int BUFFER_HEIGHT = 720;

    private static ScannerOverlay instance;

    private final Activity activity;
    private FrameLayout root;
    private MarkerView markerView;
    private ImageView frozenView;

    // Четыре скруглённых уголка (как у системного сканера Samsung): в покое —
    // широкая рамка наводки по центру, при находке плавно перетекают к углам
    // кода и следуют за ним, при потере — возвращаются на место.
    private static final class MarkerView extends View {

        private static final float BRACKET_PART = 0.10f; // доля стороны под уголок
        private static final float LERP = 0.35f;        // скорость перетекания за кадр
        private static final float CODE_PADDING_PX = 15f; // отступ рамки от кода наружу

        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private float[] target;
        private float[] current;
        private boolean hasCode;

        MarkerView(Context context, float strokeWidth, float cornerRadius) {
            super(context);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(strokeWidth);
            paint.setStrokeCap(Paint.Cap.ROUND);
            paint.setPathEffect(new android.graphics.CornerPathEffect(cornerRadius));
        }

        void setPoints(float[] normalized) {
            hasCode = normalized != null && normalized.length >= 8;
            target = hasCode ? expandAroundCode(toViewOrder(normalized)) : idleFrame();
            invalidate();
        }

        // Распознаватель отдаёт углы в системе координат кода (его "верхний левый"
        // поворачивается вместе с кодом). Пересортировка по углу вокруг центра
        // приводит их к порядку экрана: каждый уголок бежит к ближайшему углу кода.
        private float[] toViewOrder(float[] p) {
            float cx = 0;
            float cy = 0;

            for (int i = 0; i < 4; i++) {
                cx += p[i * 2] / 4f;
                cy += p[i * 2 + 1] / 4f;
            }

            int[] order = {0, 1, 2, 3};
            double[] angle = new double[4];

            for (int i = 0; i < 4; i++)
                angle[i] = Math.atan2(p[i * 2 + 1] - cy, p[i * 2] - cx);

            for (int i = 0; i < 3; i++) {

                for (int j = i + 1; j < 4; j++) {

                    if (angle[order[j]] < angle[order[i]]) {
                        int t = order[i];
                        order[i] = order[j];
                        order[j] = t;
                    }

                }

            }

            float[] sorted = new float[8];

            for (int i = 0; i < 4; i++) {
                sorted[i * 2] = p[order[i] * 2];
                sorted[i * 2 + 1] = p[order[i] * 2 + 1];
            }

            return sorted;
        }

        // Раздвигает четырёхугольник кода наружу от центра на CODE_PADDING_PX,
        // чтобы рамка была чуть шире самого кода.
        private float[] expandAroundCode(float[] normalized) {
            float w = getWidth();
            float h = getHeight();

            if (w <= 0 || h <= 0)
                return normalized;

            float cx = 0;
            float cy = 0;

            for (int i = 0; i < 4; i++) {
                cx += normalized[i * 2] * w / 4;
                cy += normalized[i * 2 + 1] * h / 4;
            }

            float[] expanded = new float[8];

            for (int i = 0; i < 4; i++) {
                float px = normalized[i * 2] * w;
                float py = normalized[i * 2 + 1] * h;
                float dx = px - cx;
                float dy = py - cy;
                float len = (float) Math.sqrt(dx * dx + dy * dy);

                if (len > 1) {
                    px += dx / len * CODE_PADDING_PX;
                    py += dy / len * CODE_PADDING_PX;
                }

                expanded[i * 2] = px / w;
                expanded[i * 2 + 1] = py / h;
            }

            return expanded;
        }

        // Рамка наводки: квадрат ~70% меньшей стороны по центру (нормированно).
        private float[] idleFrame() {
            float w = getWidth();
            float h = getHeight();

            if (w <= 0 || h <= 0)
                return null;

            float half = Math.min(w, h) * 0.455f;
            float left = (w / 2 - half) / w;
            float right = (w / 2 + half) / w;
            float top = (h / 2 - half) / h;
            float bottom = (h / 2 + half) / h;
            return new float[]{left, top, right, top, right, bottom, left, bottom};
        }

        @Override
        protected void onDraw(Canvas canvas) {
            if (target == null)
                target = idleFrame();

            if (target == null)
                return;

            if (current == null)
                current = target.clone();

            // Плавное перетекание к цели; пока не доехали — перерисовываемся.
            boolean settled = true;

            for (int i = 0; i < 8; i++) {
                current[i] += (target[i] - current[i]) * LERP;

                if (Math.abs(target[i] - current[i]) > 0.001f)
                    settled = false;
            }

            paint.setColor(hasCode ? 0xFFFFD600 : 0xCCFFFFFF);

            float w = getWidth();
            float h = getHeight();

            for (int i = 0; i < 4; i++) {
                float cx = current[i * 2] * w;
                float cy = current[i * 2 + 1] * h;
                float px = current[((i + 3) % 4) * 2] * w;
                float py = current[((i + 3) % 4) * 2 + 1] * h;
                float nx = current[((i + 1) % 4) * 2] * w;
                float ny = current[((i + 1) % 4) * 2 + 1] * h;

                Path bracket = new Path();
                bracket.moveTo(cx + (px - cx) * BRACKET_PART, cy + (py - cy) * BRACKET_PART);
                bracket.lineTo(cx, cy);
                bracket.lineTo(cx + (nx - cx) * BRACKET_PART, cy + (ny - cy) * BRACKET_PART);
                canvas.drawPath(bracket, paint);
            }

            if (!settled)
                postInvalidateOnAnimation();
        }
    }

    private ScannerOverlay(Activity activity) {
        this.activity = activity;
    }

    public static void show(Activity activity) {
        if (instance != null)
            return;
        instance = new ScannerOverlay(activity);
        instance.showInternal();
    }

    public static void hide() {
        if (instance == null)
            return;
        instance.hideInternal();
        instance = null;
    }

    public static boolean hasCameraPermission(Activity activity) {
        if (Build.VERSION.SDK_INT < 23)
            return true;
        return activity.checkSelfPermission(Manifest.permission.CAMERA)
            == PackageManager.PERMISSION_GRANTED;
    }

    public static void requestCameraPermission(Activity activity) {
        if (Build.VERSION.SDK_INT >= 23)
            activity.requestPermissions(new String[]{Manifest.permission.CAMERA}, 18127);
    }

    // Нормированные координаты вида превью: 4 угла найденного кода.
    public static void showMarkers(float[] points) {
        if (instance != null && instance.markerView != null)
            instance.markerView.setPoints(points);
    }

    // Стоп-кадр (ARGB уже в ориентации вида). Вызывается из потока камеры.
    public static void showFrozenFrame(int[] argb, int width, int height) {
        final ScannerOverlay overlay = instance;

        if (overlay == null)
            return;

        final Bitmap bitmap = Bitmap.createBitmap(argb, width, height, Bitmap.Config.ARGB_8888);
        overlay.activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (instance != null && instance.frozenView != null) {
                    instance.frozenView.setImageBitmap(bitmap);
                    instance.frozenView.setVisibility(View.VISIBLE);
                }
            }
        });
    }

    private int dp(float value) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
            activity.getResources().getDisplayMetrics());
    }

    private void showInternal() {
        // Кнопка "Назад" закрывает сканер, не доходя до приложения.
        root = new FrameLayout(activity) {
            @Override
            public boolean dispatchKeyEvent(KeyEvent event) {
                if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
                    if (event.getAction() == KeyEvent.ACTION_UP)
                        onClose();
                    return true;
                }
                return super.dispatchKeyEvent(event);
            }
        };
        root.setBackgroundColor(Color.BLACK);
        root.setFocusableInTouchMode(true);

        SurfaceView surfaceView = new SurfaceView(activity);
        surfaceView.getHolder().setFixedSize(BUFFER_WIDTH, BUFFER_HEIGHT);
        surfaceView.getHolder().addCallback(this);

        // Тап по превью — фокусировка в точке (нормированные координаты вида).
        surfaceView.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View view, MotionEvent event) {
                if (event.getAction() == MotionEvent.ACTION_DOWN && view.getWidth() > 0)
                    onTap(event.getX() / view.getWidth(), event.getY() / view.getHeight());
                return true;
            }
        });

        // Вид с пропорциями кадра камеры (система поворачивает буфер в портрет,
        // поэтому аспект вида — 9:16), остальное закрывает чёрный letterbox.
        DisplayMetrics metrics = activity.getResources().getDisplayMetrics();
        int viewWidth = metrics.widthPixels;
        int viewHeight = viewWidth * BUFFER_WIDTH / BUFFER_HEIGHT;

        if (viewHeight > metrics.heightPixels) {
            viewHeight = metrics.heightPixels;
            viewWidth = viewHeight * BUFFER_HEIGHT / BUFFER_WIDTH;
        }

        root.addView(surfaceView,
            new FrameLayout.LayoutParams(viewWidth, viewHeight, Gravity.CENTER));

        // Стоп-кадр: в паузе после распознавания показывается кадр, на котором код
        // был найден, — рамка садится на него без рассинхрона с живым превью.
        frozenView = new ImageView(activity);
        frozenView.setScaleType(ImageView.ScaleType.FIT_XY);
        frozenView.setVisibility(View.GONE);
        root.addView(frozenView,
            new FrameLayout.LayoutParams(viewWidth, viewHeight, Gravity.CENTER));

        // Уголки: рамка наводки в покое, сопровождение кода при находке.
        markerView = new MarkerView(activity, dp(4), dp(9));
        root.addView(markerView,
            new FrameLayout.LayoutParams(viewWidth, viewHeight, Gravity.CENTER));

        // Кнопка закрытия.
        TextView close = new TextView(activity);
        close.setText("✕");
        close.setTextColor(Color.WHITE);
        close.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
        close.setPadding(dp(16), dp(12), dp(16), dp(12));
        GradientDrawable closeShape = new GradientDrawable();
        closeShape.setColor(0x66000000);
        closeShape.setCornerRadius(dp(24));
        close.setBackground(closeShape);
        close.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                onClose();
            }
        });
        FrameLayout.LayoutParams closeParams = new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.TOP | Gravity.END);
        closeParams.setMargins(0, dp(24), dp(16), 0);
        root.addView(close, closeParams);

        // Кнопка фонарика (слева сверху, симметрично кнопке закрытия).
        TextView torch = new TextView(activity);
        torch.setText("🔦");
        torch.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
        torch.setPadding(dp(14), dp(12), dp(14), dp(12));
        GradientDrawable torchShape = new GradientDrawable();
        torchShape.setColor(0x66000000);
        torchShape.setCornerRadius(dp(24));
        torch.setBackground(torchShape);
        torch.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                onTorch();
            }
        });
        FrameLayout.LayoutParams torchParams = new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.TOP | Gravity.START);
        torchParams.setMargins(dp(16), dp(24), 0, 0);
        root.addView(torch, torchParams);

        activity.addContentView(root, new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        root.requestFocus();
    }

    private void hideInternal() {
        if (root != null && root.getParent() instanceof ViewGroup)
            ((ViewGroup) root.getParent()).removeView(root);
        root = null;
        markerView = null;
        frozenView = null;
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        // Поверхность готова и имеет запрошенный фиксированный размер.
        onSurface(holder.getSurface());
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        onSurface(null);
    }

    private static native void onSurface(Surface surface);

    private static native void onTap(float nx, float ny);

    private static native void onClose();

    private static native void onTorch();
}
