package com.warptempo.gui;

import android.app.NativeActivity;
import android.os.Bundle;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The product's ONE Java class: a NativeActivity subclass. Its body is three
 * calls: setDecorFitsSystemWindows(true), which ASKS for the window's content
 * to be laid out inside the system bars' insets rather than under them, and the
 * two that give the STATUS BAR the product's own title-bar colour.
 *
 * <p>IT DOES NOT SHRINK THE NATIVE SURFACE, and nothing here can. An app
 * window's frame is the whole display by construction on modern Android
 * (FLAG_LAYOUT_IN_SCREEN | FLAG_LAYOUT_INSET_DECOR on the window's own layout
 * params), and "fitting the system windows" is DecorView PADDING -- which a
 * NativeActivity never sees, because it takes the WINDOW's own surface through
 * Window#takeSurface. Measured on the tablet 2026-08-27: frame=[0,0][2304,1440]
 * with this call in place, at targetSdk 35 and again at 34. What this call and
 * the target DO buy is the framework reporting a real CONTENT RECT, whatever
 * the framework decides that rect is -- measured that day as 2304x1387 at
 * (0,53), the STATUS BAR ALONE. One UI's 84 px taskbar was not in it: dumpsys
 * showed `InsetsSource type=navigationBars frame=[0,1356][2304,1440]
 * visible=false` plus a `tappableElement` source on the same frame with
 * visible=true, and `mAppBounds=(0,0-2304,1356)`; the reading was taken with
 * the panel DOZING (cover shut), so the taskbar was not a visible navigation
 * inset at that moment. WHETHER IT OVERLAYS THE BOTTOM ROW ON AN AWAKE PANEL
 * IS OPEN (the architect's next look); if it does, the recorded next step is a
 * `native` method on THIS class handing the native side
 * WindowInsets.Type.tappableElement()'s bottom at each content-rect / config
 * change, for the backend to subtract from the rect. Not done. The rect
 * reaches native code as onContentRectChanged. THE ANDROID BACKEND MAKES THAT
 * RECT THE WINDOW (src/gui/platform_android.cpp; the rule is at origin_x_ in its header): the
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
 * the waveform is plenty tall enough to lose the bars' height.
 *
 * <p>THE STATUS BAR IS THE TITLE BAR (architect 2026-08-27, later the same
 * day): with the bars showing permanently, the one across the top reads as this
 * window's title bar, so it takes the colour the architect's own labwc theme
 * gives a title bar. PROVENANCE, and it is a chain rather than a derivation
 * (~/.config/labwc/themerc-override): `window.active.title.bg.color: #292c30`,
 * which is kRedesignRowGround in src/gui/render.h -- the product's row ground,
 * sampled from the same Breeze source -- with `window.active.label.text.color:
 * #fcfcfc` = kRedesignLabel, which is why the bar's own icons stay light. The
 * system's default painted it ~#212326 (measured on a screencap), near the
 * CONTENT ground and so a shade off the menu row below it. The native side
 * leaves air under the bar in the same ground (kStatusBarAirPx,
 * src/gui/platform_android.cpp), so bar, air and menu row read as one strip.
 * THE TASKBAR IS NOT TOUCHED: the architect -- "the taskbar looks great, it's
 * already the correct color".
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

        // THE WINDOW HAS TO DRAW THE BAR BACKGROUNDS FOR A COLOUR TO LAND ON
        // THEM AT ALL. setStatusBarColor's own documented precondition is
        // FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS set and FLAG_TRANSLUCENT_STATUS
        // clear: the Material and DeviceDefault themes set the first through
        // windowDrawsSystemBarBackgrounds, but this activity's theme is the
        // legacy @android:style/Theme.NoTitleBar (the manifest says why), which
        // does not — so the flag is set here by hand. The second is set by
        // nothing in this app, so there is nothing to clear. Deprecated at
        // API 30 and honoured at the 34 the manifest targets, exactly like the
        // setter below.
        getWindow().addFlags(
                WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);

        // THE TITLE-BAR COLOUR, straight from the labwc themerc-override (the
        // class comment carries the provenance chain): #292c30 is
        // kRedesignRowGround, the ground the menu row under it already paints.
        // setStatusBarColor is deprecated at API 35 -- the jar this compiles
        // against -- and works at 34, which is what the manifest targets and
        // what the platform honours; the replacement is the edge-to-edge
        // arrangement this app deliberately does not use (the targetSdk note
        // above). A second javac deprecation warning is expected.
        getWindow().setStatusBarColor(0xFF292C30);

        // LIGHT ICONS ON IT, by CLEARING the light-background appearance:
        // APPEARANCE_LIGHT_STATUS_BARS means "the bar's background is light,
        // draw its icons dark", so a dark bar is the flag ABSENT -- the mask
        // names the flag and the value clears it. getInsetsController() is
        // declared nullable for a window with no decor view; super.onCreate
        // installed the decor above, so this is the API's contract rather than
        // a fault with a producer here.
        final WindowInsetsController bars = getWindow().getInsetsController();
        if (bars != null) {
            bars.setSystemBarsAppearance(
                    0, WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS);
        }
    }
}
