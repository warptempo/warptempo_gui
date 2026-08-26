package com.warptempo.gui;

import android.app.NativeActivity;
import android.os.Bundle;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

/**
 * The product's ONE Java class: a NativeActivity subclass whose whole job today
 * is immersive mode. It exists at all because the platform gives native code no
 * door to the system bars -- hiding the navigation bar / taskbar is Java-only
 * (WindowInsetsController, API 30+) -- and the taskbar owns the INPUT of the
 * band the product paints its transport and its modal surface into, so without
 * this class the bottom row is unreachable on glass.
 *
 * <p>Both bars are hidden with BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE (a swipe
 * from an edge shows them transiently and they retreat again on their own), and
 * setDecorFitsSystemWindows(false) hands the native window the whole panel. The
 * hide is re-applied at every focus gain because the system restores the bars
 * whenever the activity loses focus.
 *
 * <p>EVERY LATER JAVA NEED JOINS THIS CLASS, as a method -- never as a second
 * class: the SAF picker's onActivityResult (which is exactly why a subclass is
 * required, NativeActivity never forwarding it) and the system clipboard are the
 * two the port already knows about. The first one that declares a `native`
 * method must also add the `static { System.loadLibrary("warptempo_gui"); }`
 * initialiser -- NativeActivity's own dlopen of the library does NOT register it
 * for name-based JNI resolution -- and nothing here needs it yet.
 */
public class MainActivity extends NativeActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setDecorFitsSystemWindows(false);
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    private void hideSystemBars() {
        WindowInsetsController controller = getWindow().getInsetsController();
        if (controller == null) {
            return;
        }
        controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
        controller.setSystemBarsBehavior(
                WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
    }
}
