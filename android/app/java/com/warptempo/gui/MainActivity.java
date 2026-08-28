package com.warptempo.gui;

import android.app.NativeActivity;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The product's ONE Java class: a NativeActivity subclass. Its body is
 * setDecorFitsSystemWindows(true), which ASKS for the window's content to be
 * laid out inside the system bars' insets rather than under them, the window
 * calls that give the STATUS BAR the product's own title-bar colour and the
 * band under the TASKBAR the product's content ground, and -- since the car's
 * arc -- the MediaSession that hands the head unit's buttons down to the
 * render player and its state back up (the block at the end of this comment).
 *
 * <p>IT DOES NOT SHRINK THE NATIVE SURFACE, and nothing here can. An app
 * window's frame is the whole display by construction on modern Android
 * (FLAG_LAYOUT_IN_SCREEN | FLAG_LAYOUT_INSET_DECOR on the window's own layout
 * params), and "fitting the system windows" is DecorView PADDING -- which a
 * NativeActivity never sees, because it takes the WINDOW's own surface through
 * Window#takeSurface. Measured on the tablet 2026-08-27: frame=[0,0][2304,1440]
 * with this call in place, at targetSdk 35 and again at 34. What this call and
 * the target DO buy is the framework reporting a real CONTENT RECT, and THAT
 * RECT ALREADY EXCLUDES BOTH BARS. Measured on an AWAKE panel under the
 * architect's own Screen zoom (override density 320): `window 2304x1268 at
 * (0,76) of surface 2304x1440` -- a 60 px status bar plus the 16 px of air the
 * native side adds above it, and a 96 px taskbar below. Nothing here has to
 * measure or subtract a bar. (History, one line: an earlier reading of
 * 2304x1387 at (0,53) showed the status bar alone, because the taskbar
 * reported no inset while the panel DOZED with the cover shut -- dumpsys had
 * it as a `tappableElement` and as `mAppBounds`, with
 * `type=navigationBars ... visible=false`. The next step recorded from that
 * reading -- a `native` method handing the backend
 * WindowInsets.Type.tappableElement()'s bottom to subtract -- IS RETIRED: on
 * an awake panel it would subtract the taskbar twice.) The rect
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
 * THE TASKBAR'S ICONS ARE THE LAUNCHER'S AND NOTHING HERE TOUCHES THEM: the
 * architect -- "the taskbar looks great, it's already the correct color". THE
 * BAND UNDER THEM IS OURS, and has been since the bar-backgrounds flag landed:
 * the window draws BOTH bars' backgrounds, so that strip took the inherited
 * navigationBarColor (measured on the device at (33,35,38) -- the system
 * default, and the same #202326 we would have picked). It is now set on
 * purpose to kRedesignContentGround, the ground the taskbar's icons already
 * sit on.
 *
 * <p>EVERY LATER JAVA NEED JOINS THIS CLASS, as a method -- never as a second
 * top-level class (the MediaSession.Callback below is an INNER class of this
 * one and is not a second class in that sense: it is the session's own
 * listener shape and can be nothing else). Two more are known: the SAF
 * picker's onActivityResult (which is exactly why a subclass is required at
 * all, NativeActivity never forwarding it) and the system clipboard
 * (ClipboardManager is a Java object with no NDK surface, so copy and paste
 * stop at the process edge until it lands). The key-repeat cadence stays
 * hard-coded from labwc's numbers in platform_android.cpp because nothing
 * native reports it.
 *
 * <p>THE CAR (architect design 2026-08-28, section 3): the head unit's buttons
 * reach an app over Bluetooth AVRCP as media-button events delivered to
 * whichever app holds an ACTIVE MediaSession, and the head unit's display
 * reads that session's metadata and playback state. This class creates ONE
 * session in onCreate (on the UI thread, so its callbacks land there) and
 * releases it in onDestroy; it is ACTIVE ONLY WHILE THE RENDER PLAYER STANDS,
 * which the native side says through mediaState(...). EACH CALLBACK IS ONE
 * INTEGER DOWN through nativeMediaCommand -- the native side queues it and
 * wakes its own loop, then turns it into the player's OWN KEYS (Space, Page
 * Up / Page Down, Left / Right), so every car button is a chord the player
 * already binds and there is no second dispatch road. onMediaButtonEvent is
 * deliberately NOT overridden: the framework's default maps KEYCODE_MEDIA_*
 * onto onPlay / onPause / onSkipToNext / ... itself, splitting the toggle key
 * by the PUBLISHED playback state, which is why every push keeps that state
 * honest. Audio focus is REQUESTED when a push says playing and none is held
 * and ABANDONED when a push says inactive; a loss pauses the player through
 * the same command road ("Android's one imposed interrupt"), a refused
 * request is logged and playback proceeds (the AAudio stream is already
 * running; focus decides who else ducks, not whether we sound).
 *
 * <p>THIS PHASE IS A MediaSession ALONE, BY RULING: no notification, no
 * foreground service, no background playback, no lock-screen transport. The
 * tablet is a kiosk on a stand -- the native side keeps the screen on -- with
 * the app in the foreground and the head unit reading the session over AVRCP,
 * and none of the machinery those would need (a res/, a service, the
 * FOREGROUND_SERVICE and POST_NOTIFICATIONS permissions) is added to the
 * manifest or the build.
 */
public class MainActivity extends NativeActivity {

    private static final String TAG = "warptempo";

    // THE LIBRARY MUST BE REGISTERED FOR NAME-BASED JNI RESOLUTION: the
    // NativeActivity dlopens libwarptempo_gui.so for android_main, but that
    // load does not make its Java_* exports findable for a `native` method
    // declared here. This initialiser is what does, and this class's one
    // native method below is why it exists.
    static {
        System.loadLibrary("warptempo_gui");
    }

    // THE COMMAND TABLE, SHARED WITH THE NATIVE SIDE BY NUMBER: these are
    // GuiMediaCommand::Kind's enumerator values (src/gui/gui_media.h), in that
    // order, 0-based, and MEDIA_KIND_COUNT is its kGuiMediaCommandKindCount. A
    // new kind is added at the END on both sides. THERE IS NO PLAY_PAUSE ROW:
    // the default onMediaButtonEvent splits the toggle key into onPlay /
    // onPause by the published state (the class comment), so nothing here
    // could ever send one -- ACTION_PLAY_PAUSE stays DECLARED below because
    // that split only happens for a declared action.
    private static final int MEDIA_PLAY                 = 0;
    private static final int MEDIA_PAUSE                = 1;
    private static final int MEDIA_STOP                 = 2;
    private static final int MEDIA_NEXT                 = 3;
    private static final int MEDIA_PREVIOUS             = 4;
    private static final int MEDIA_FAST_FORWARD         = 5;
    private static final int MEDIA_REWIND               = 6;
    private static final int MEDIA_SEEK_TO              = 7;
    private static final int MEDIA_FOCUS_LOST           = 8;
    private static final int MEDIA_FOCUS_LOST_TRANSIENT = 9;
    private static final int MEDIA_FOCUS_GAINED         = 10;
    private static final int MEDIA_KIND_COUNT           = 11;

    // THE ONE ROAD DOWN (Java_com_warptempo_gui_MainActivity_nativeMediaCommand,
    // src/gui/platform_android.cpp): lock, push, wake. Called on the UI thread
    // by the session's callbacks and the focus listener; safe before the
    // native loop's init and after its shutdown, where the native side drops
    // the command.
    private static native void nativeMediaCommand(int kind, long positionMs);

    // The actions the session declares, always all of them: the framework's
    // default media-button routing dispatches a key only when its action is
    // declared, and the native side decides what each one means.
    private static final long SESSION_ACTIONS =
            PlaybackState.ACTION_PLAY
            | PlaybackState.ACTION_PAUSE
            | PlaybackState.ACTION_PLAY_PAUSE
            | PlaybackState.ACTION_STOP
            | PlaybackState.ACTION_SKIP_TO_NEXT
            | PlaybackState.ACTION_SKIP_TO_PREVIOUS
            | PlaybackState.ACTION_FAST_FORWARD
            | PlaybackState.ACTION_REWIND
            | PlaybackState.ACTION_SEEK_TO;

    // The session and the focus machine. `this` is the one lock: mediaState
    // runs on the native loop's thread while the callbacks, the focus listener
    // and onDestroy run on the UI thread.
    private MediaSession      session;
    private AudioManager      audioManager;
    private AudioFocusRequest focusRequest;
    private boolean           focusHeld;
    private boolean           released;

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

        // THE TASKBAR'S BAND IS OURS TOO, and stating it is the point. The
        // FLAG above makes this window draw BOTH bars' backgrounds, so the
        // strip under the taskbar has been painted by us since that flag
        // landed -- with the INHERITED navigationBarColor, measured on the
        // device at (33,35,38), which is the system default and is also
        // #202326, so nothing visibly changed by luck rather than by
        // intention. This is the intention: #202326 is
        // kRedesignContentGround (the palette block in src/gui/render.h), the
        // product's content ground and the ground the taskbar's icons already
        // sit on. It is NOT a labwc colour -- the taskbar itself is the
        // launcher's and we paint none of it; we own only the band beneath.
        // Its ICONS are left exactly as they are: nothing here touches
        // APPEARANCE_LIGHT_NAVIGATION_BARS. setNavigationBarColor is
        // deprecated at API 35 -- the jar this compiles against -- and
        // honoured at the 34 the manifest targets, the same note the status
        // colour above carries, and a third javac deprecation warning is
        // expected.
        getWindow().setNavigationBarColor(0xFF202326);

        // LIGHT ICONS ON THE STATUS BAR, by CLEARING the light-background
        // appearance (the NAVIGATION bar's own appearance bit is deliberately
        // not touched -- the taskbar's icons are the launcher's):
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

        // THE MEDIA SESSION, PER PROCESS-LIFE OF THIS ACTIVITY: created here
        // on the UI thread so its callbacks are delivered on this thread's
        // Looper (the session takes the creating thread's), released in
        // onDestroy. INACTIVE until the render player stands -- the native
        // side's first push activates it -- so the head unit's buttons reach
        // nothing while the waveform is being edited. The state is seeded
        // STOPPED with the full action set so the framework's default
        // media-button routing has actions to dispatch against from the first
        // activation.
        session = new MediaSession(this, TAG);
        session.setCallback(new TransportCallback());
        session.setPlaybackState(new PlaybackState.Builder()
                .setActions(SESSION_ACTIONS)
                .setState(PlaybackState.STATE_STOPPED, 0L, 1.0f)
                .build());
        session.setActive(false);

        // AUDIO FOCUS: the request carries the attributes the AAudio stream
        // opens with (USAGE_MEDIA / CONTENT_TYPE_MUSIC, playback_aaudio.cpp),
        // so the system ranks this app's sound the way the stream declares
        // it, and the listener is pinned to the MAIN LOOPER explicitly rather
        // than left to the requesting thread's, since the request is made from
        // the native loop's thread (mediaState) and that thread has no Looper.
        // Ducking is left to the framework's default (the system lowers the
        // volume itself for AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK and the listener
        // is not called for it), so a navigation prompt ducks the music rather
        // than pausing it.
        audioManager = (AudioManager) getSystemService(AUDIO_SERVICE);
        focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build())
                .setOnAudioFocusChangeListener(
                        new FocusListener(), new Handler(Looper.getMainLooper()))
                .build();
    }

    // THE FIRST LIFECYCLE OVERRIDE BESIDE onCreate, and it exists for the
    // session: a MediaSession is a system-side object that outlives a
    // released-without-release() activity and keeps its media-button claim,
    // so it is released here. super.onDestroy() FIRST: NativeActivity's own
    // onDestroy posts APP_CMD_DESTROY and JOINS the native loop's thread, so
    // by the time it returns no mediaState call can still be in flight and
    // the session may be taken down under the one lock with nothing racing
    // it. Focus is abandoned with it -- the native side abandons it on the
    // player's close, but a process ending with the player standing (the
    // system destroying the activity) never reached that push.
    @Override
    protected void onDestroy() {
        super.onDestroy();
        synchronized (this) {
            released = true;
            if (focusHeld && audioManager != null && focusRequest != null) {
                audioManager.abandonAudioFocusRequest(focusRequest);
                focusHeld = false;
            }
            if (session != null) {
                session.setActive(false);
                session.release();
                session = null;
            }
        }
    }

    // THE ROAD UP (GuiPlatform::publish_media_state, src/gui/platform_android.cpp),
    // called on the native loop's thread at every edge where what the head
    // unit shows changes -- never per tick: a PLAYING state advances on the
    // head unit's own clock from `positionMs` at speed 1.0, which is what the
    // (state, position, speed) triple means. Metadata: TITLE is the wav's
    // spelling with its folder, ARTIST and ALBUM are the project's name,
    // DURATION the item's length. State: PLAYING / PAUSED with an item,
    // STOPPED with none (an empty title). setActive follows the player's
    // open and close. Every setter here is a binder call and is callable from
    // any attached thread; the lock is against onDestroy's release on the UI
    // thread. Focus: requested when a push says playing and none is held;
    // abandoned when a push says inactive.
    public synchronized void mediaState(boolean active, boolean playing,
                                        String title, String artist,
                                        long durationMs, long positionMs) {
        if (released || session == null) return;

        final MediaMetadata.Builder meta = new MediaMetadata.Builder()
                .putString(MediaMetadata.METADATA_KEY_TITLE, title)
                .putString(MediaMetadata.METADATA_KEY_ARTIST, artist)
                .putString(MediaMetadata.METADATA_KEY_ALBUM, artist)
                .putLong(MediaMetadata.METADATA_KEY_DURATION, durationMs);
        session.setMetadata(meta.build());

        final int state;
        if (!active || title.isEmpty()) {
            state = PlaybackState.STATE_STOPPED;
        } else if (playing) {
            state = PlaybackState.STATE_PLAYING;
        } else {
            state = PlaybackState.STATE_PAUSED;
        }
        session.setPlaybackState(new PlaybackState.Builder()
                .setActions(SESSION_ACTIONS)
                .setState(state, positionMs, 1.0f)
                .build());
        session.setActive(active);

        if (active && playing && !focusHeld) {
            // A REFUSED REQUEST IS LOGGED AND PLAYBACK PROCEEDS: the AAudio
            // stream is already running and focus decides who else ducks,
            // not whether this app sounds. DELAYED is not asked for (the
            // builder's default), so the answer is GRANTED or FAILED.
            final int result = audioManager.requestAudioFocus(focusRequest);
            if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
                focusHeld = true;
            } else {
                Log.w(TAG, "audio focus request refused (" + result
                        + "); playing without it");
            }
        } else if (!active && focusHeld) {
            audioManager.abandonAudioFocusRequest(focusRequest);
            focusHeld = false;
        }
    }

    // THE HEAD UNIT'S BUTTONS, one integer each. onMediaButtonEvent is NOT
    // overridden: its default maps KEYCODE_MEDIA_* onto these, splitting the
    // play/pause key by the published state (the class comment). Every
    // callback runs on the UI thread (the session's creating Looper).
    private final class TransportCallback extends MediaSession.Callback {
        @Override public void onPlay()           { nativeMediaCommand(MEDIA_PLAY, 0L); }
        @Override public void onPause()          { nativeMediaCommand(MEDIA_PAUSE, 0L); }
        @Override public void onStop()           { nativeMediaCommand(MEDIA_STOP, 0L); }
        @Override public void onSkipToNext()     { nativeMediaCommand(MEDIA_NEXT, 0L); }
        @Override public void onSkipToPrevious() { nativeMediaCommand(MEDIA_PREVIOUS, 0L); }
        @Override public void onFastForward()    { nativeMediaCommand(MEDIA_FAST_FORWARD, 0L); }
        @Override public void onRewind()         { nativeMediaCommand(MEDIA_REWIND, 0L); }
        @Override public void onSeekTo(long pos) { nativeMediaCommand(MEDIA_SEEK_TO, pos); }
    }

    // THE FOCUS MACHINE'S OTHER HALF: a permanent LOSS releases the hold (the
    // system took it; the next playing push requests again), a transient loss
    // keeps it (GAIN returns it), and each is forwarded so the native side
    // pauses; GAIN is forwarded and the native side does nothing with it --
    // NOTHING RECOVERS BY ITSELF, the user presses play.
    private final class FocusListener
            implements AudioManager.OnAudioFocusChangeListener {
        @Override
        public void onAudioFocusChange(int change) {
            switch (change) {
                case AudioManager.AUDIOFOCUS_LOSS:
                    synchronized (MainActivity.this) { focusHeld = false; }
                    nativeMediaCommand(MEDIA_FOCUS_LOST, 0L);
                    break;
                case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
                case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
                    nativeMediaCommand(MEDIA_FOCUS_LOST_TRANSIENT, 0L);
                    break;
                case AudioManager.AUDIOFOCUS_GAIN:
                    synchronized (MainActivity.this) { focusHeld = true; }
                    nativeMediaCommand(MEDIA_FOCUS_GAINED, 0L);
                    break;
                default:
                    break;
            }
        }
    }
}
