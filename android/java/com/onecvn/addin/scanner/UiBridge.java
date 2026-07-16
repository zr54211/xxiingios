package com.onecvn.addin.scanner;

import android.app.Activity;

// Мост в UI-поток для нативной части компоненты.
// Нативная часть находит класс через IAndroidComponentHelper.FindClass,
// регистрирует nativeRun через RegisterNatives и вызывает post().
public final class UiBridge implements Runnable {

    private final long ctx;

    private UiBridge(long ctx) {
        this.ctx = ctx;
    }

    @Override
    public void run() {
        nativeRun(ctx);
    }

    public static void post(Activity activity, long ctx) {
        activity.runOnUiThread(new UiBridge(ctx));
    }

    private static native void nativeRun(long ctx);
}
