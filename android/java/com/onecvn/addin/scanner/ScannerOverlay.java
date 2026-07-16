package com.onecvn.addin.scanner;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
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

    private int dp(float value) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
            activity.getResources().getDisplayMetrics());
    }

    private void showInternal() {
        root = new FrameLayout(activity);
        root.setBackgroundColor(Color.BLACK);

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

        // Рамка наводки по центру.
        View frame = new View(activity);
        GradientDrawable frameShape = new GradientDrawable();
        frameShape.setColor(Color.TRANSPARENT);
        frameShape.setStroke(dp(2), 0xCCFFFFFF);
        frameShape.setCornerRadius(dp(12));
        frame.setBackground(frameShape);
        int frameSize = (int) (Math.min(viewWidth, viewHeight) * 0.7f);
        root.addView(frame,
            new FrameLayout.LayoutParams(frameSize, frameSize, Gravity.CENTER));

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
    }

    private void hideInternal() {
        if (root != null && root.getParent() instanceof ViewGroup)
            ((ViewGroup) root.getParent()).removeView(root);
        root = null;
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
