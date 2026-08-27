package com.warptempo.gui;

import android.app.NativeActivity;
import android.os.Bundle;

/**
 * The product's ONE Java class: a NativeActivity subclass. Its body today is a
 * SINGLE call, setDecorFitsSystemWindows(true), which ASKS for the window's
 * content to be laid out inside the system bars' insets rather than under them.
 *
 * <p>IT DOES NOT SHRINK THE NATIVE SURFACE, and nothing here can. An app
 * window's frame is the whole display by construction on modern Android
 * (FLAG_LAYOUT_IN_SCREEN | FLAG_LAYOUT_INSET_DECOR on the window's own layout
 * params), and "fitting the system windows" is DecorView PADDING -- which a
 * NativeActivity never sees, because it takes the WINDOW's own surface through
 * Window#takeSurface. Measured on the tablet 2026-08-27: frame=[0,0][2304,1440]
 * with this call in place, at targetSdk 35 and again at 34. What this call and
 * the target DO buy is the framework reporting a real CONTENT RECT --
 * mAppBounds 2304x1356, status bar 53 px, taskbar 84 px -- which reaches native
 * code as onContentRectChanged. THE ANDROID BACKEND MAKES THAT RECT THE WINDOW
 * (src/gui/platform_android.cpp; the rule is at origin_x_ in its header): the
 * GUI is told the rect's size, the origin is added at the blit and subtracted
 * at the touch decode, and nothing above the seam knows either exists.
 *
 * <p>targetSdk is 34 for the neighbouring reason (android/toolchain/00_env.sh
 * owns the number): at 35 Android 15's edge-to-edge enforcement lays the window
 * out over the bars whatever it asks for, and the opt-out there is a theme
 * attribute needing a res/ this APK does not have.
 *
 * <p>IMMERSIVE MODE IS RETIRED (architect 2026-08-27). Both bars were hidden
 * here -- sticky-immersive at create and re-applied at every focus gain -- and
 * they now show permanently, like any ordinary app. On the glass a swipe
 * brought the taskbar's icons up OVER the app with no background of their own
 * (Android's transient bars behaving as designed, and reading as a bug), and
 * the waveform is plenty tall enough to lose the bars' height. The bars' colors
 * and theme are the system's and are not expected to match the app.
 *
 * <p>EVERY LATER JAVA NEED JOINS THIS CLASS, as a method -- never as a second
 * class. Three are already known: the SAF picker's onActivityResult (which is
 * exactly why a subclass is required at all, NativeActivity never forwarding
 * it), the system clipboard (ClipboardManager is a Java object with no NDK
 * surface, so copy and paste stop at the process edge until it lands) and the
 * key-repeat cadence (hard-coded from labwc's numbers in platform_android.cpp
 * because nothing native reports it). The first one that declares a `native`
 * method must also add the `static { System.loadLibrary("warptempo_gui"); }`
 * initialiser -- NativeActivity's own dlopen of the library does NOT register
 * it for name-based JNI resolution -- and nothing here needs it yet.
 */
public class MainActivity extends NativeActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // AFTER super, not before: NativeActivity's own onCreate installs the
        // decor (setContentView), which is where the platform's own default is
        // applied, so this has to land on top of it rather than under it. One
        // javac deprecation warning is expected -- the method is deprecated in
        // the API 35 jar this compiles against, which is the platform we have
        // installed rather than the level the manifest declares.
        getWindow().setDecorFitsSystemWindows(true);
    }
}
