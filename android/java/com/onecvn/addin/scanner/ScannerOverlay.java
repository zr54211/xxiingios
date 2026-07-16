package com.onecvn.addin.scanner;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Build;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.widget.FrameLayout;

// Полноэкранный оверлей с SurfaceView для превью камеры.
// Все методы вызываются нативной частью в UI-потоке (через UiBridge).
// О появлении/уничтожении поверхности нативная часть узнаёт из onSurface.
public final class ScannerOverlay implements SurfaceHolder.Callback {

    // Камера отдаёт кадры фиксированного размера, SurfaceView масштабирует на экран.
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

    private void showInternal() {
        root = new FrameLayout(activity);
        root.setBackgroundColor(Color.BLACK);

        SurfaceView surfaceView = new SurfaceView(activity);
        surfaceView.getHolder().setFixedSize(BUFFER_WIDTH, BUFFER_HEIGHT);
        surfaceView.getHolder().addCallback(this);

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
}
