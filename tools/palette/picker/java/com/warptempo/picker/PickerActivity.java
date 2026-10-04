package com.warptempo.picker;

import android.app.NativeActivity;
import android.os.Bundle;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

/**
 * The picker's one Java class: a NativeActivity whose only addition is the FULL-SCREEN WINDOW, both system bars
 * hidden sticky-immersive, copied from the product's own sliver (android/app/java/com/warptempo/gui/MainActivity.java,
 * hideSystemBars and its two callers): hidden at onCreate, after super has installed the decor, and again at every
 * focus gain, since the system brings the bars back across the shade, a dialog or a task switch. The native side
 * (src/main_android.cpp) takes the window's surface whole, as the product's does, so the scene's 2304 x 1440 is the
 * panel's.
 */
public class PickerActivity extends NativeActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    private void hideSystemBars() {
        final WindowInsetsController bars = getWindow().getInsetsController();
        if (bars == null) return;
        bars.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        bars.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
    }
}
