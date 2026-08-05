package com.bluesentinelsec.mog.test;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;

public final class TestActivity extends Activity {
    private static final String TAG = "mog-android-test";

    static {
        System.loadLibrary("mog_android_test");
    }

    private static native int runNativeTests();

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Thread runner = new Thread(() -> {
            int failures = runNativeTests();
            if (failures == 0) {
                Log.i(TAG, "MOG_ANDROID_TESTS: PASS");
            } else {
                Log.e(TAG, "MOG_ANDROID_TESTS: FAIL (" + failures + " failures)");
            }
            finishAndRemoveTask();
        }, "mog-native-tests");
        runner.start();
    }
}
