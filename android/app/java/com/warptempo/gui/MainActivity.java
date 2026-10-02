package com.warptempo.gui;

import android.app.NativeActivity;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.BatteryManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;
import android.view.Display;
import android.view.KeyEvent;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import java.nio.charset.StandardCharsets;

/**
 * The product's ONE Java class: a NativeActivity subclass. Its body is the
 * FULL-SCREEN WINDOW (both system bars hidden, the block below), the
 * MediaSession that hands the head unit's buttons down to the render player
 * and its state back up (the block at the end of this comment), the system
 * clipboard, and the battery broadcast the menu row's legend reads.
 *
 * <p>FULL SCREEN, ALWAYS: NO STATUS BAR AND NO TASKBAR (architect 2026-10-01:
 * "change the shim so that it's always going to be full screen with no
 * taskbar, no status bar"). Both bars are hidden through the window's
 * WindowInsetsController — hide(statusBars() | navigationBars()) under
 * BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE, sticky immersive — at onCreate and
 * again at EVERY FOCUS GAIN (onWindowFocusChanged), since the system brings the
 * bars back across a dialog, the shade or a task switch. A swipe from an edge
 * shows them TRANSIENTLY, translucent over the app, and they slide away again
 * by themselves; he accepts that reveal ("the gallery does the same"). THE
 * NATIVE SURFACE IS THE WINDOW: an app window's frame is the whole display by
 * construction (FLAG_LAYOUT_IN_SCREEN | FLAG_LAYOUT_INSET_DECOR), a
 * NativeActivity takes that WINDOW's own surface through Window#takeSurface,
 * and the Android backend hands the GUI the surface whole
 * (src/gui/platform_android.cpp, adopt_window), reading no content rect and
 * painting no band, so nothing here sizes, colours or measures a bar.
 * setDecorFitsSystemWindows IS NOT CALLED: its one use was shaping the content
 * rect the native side no longer reads, and with both bars hidden there is
 * nothing for it to fit around. Nor is FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS set or
 * the bars' appearance touched: they existed to put the native side's own
 * bands under permanently shown bars, and a transient bar draws its own
 * translucent scrim over whatever the app shows.
 *
 * <p>targetSdk is 34 (android/toolchain/00_env.sh owns the number and says
 * why it stays where it is now that its edge-to-edge reason is inert).
 *
 * <p>EVERY LATER JAVA NEED JOINS THIS CLASS, as a method -- never as a second
 * top-level class (the MediaSession.Callback and the battery receiver below
 * are INNER classes of this one and are not second classes in that sense:
 * each is its framework's own listener shape and can be nothing else). THREE
 * HAVE LANDED: the car's MediaSession (the block at the end of this comment),
 * on 2026-09-03 THE SYSTEM CLIPBOARD -- clipboardSet / clipboardGet,
 * ClipboardManager being a Java object with no NDK surface, so copy and paste
 * reach every other app on the tablet over the same JNI road the session
 * opened -- and on 2026-10-01 THE BATTERY, the sticky ACTION_BATTERY_CHANGED
 * broadcast handed down through nativeBatteryState for the menu row's legend
 * (the block at batteryReceiver). One need is still known and unbuilt: the SAF picker's onActivityResult, which is
 * exactly why a subclass is required at all, NativeActivity never forwarding
 * it. The key-repeat cadence stays hard-coded from labwc's numbers in
 * platform_android.cpp because nothing native reports it.
 *
 * <p>THE CAR (architect design 2026-08-28, section 3): the head unit's buttons
 * reach an app over Bluetooth AVRCP as media-button events delivered to
 * whichever app holds an ACTIVE MediaSession, and the head unit's display
 * reads that session's metadata and playback state. This class holds ONE
 * SESSION AT A TIME (createSessionLocked, its one creator, called on the UI
 * thread so the callbacks land there -- by onCreate and again by onStart's
 * rebuild) and releases it in onDestroy; it is ACTIVE FROM THE FIRST TICK OF
 * THE FIRST PROJECT UNTIL THAT onDestroy (architect 2026-09-17), which the
 * native side says through mediaState(...) -- the same push carrying
 * THE CONSOLE'S THREE LINES, the project as the album on both sides of the
 * fork below, and beneath it either the render player's picture (the folder as
 * the artist and the bare name of the playing or highlighted file as the
 * title) or the main window's (the view as the artist and the undo position
 * around the save as the title)
 * (architect 2026-09-12: the Accord lays the album line above the title, dim,
 * and the artist line below it, so the dim top line takes the project, the
 * least important of the three). IT WAS ACTIVE ONLY WHILE THE RENDER PLAYER
 * STOOD until 2026-09-17, the player's close pushing the one inactive state;
 * that push is deleted, because the head unit's buttons now drive the
 * project's own transport whenever the player is closed and a session that
 * went away between plays could not carry them. EACH CALLBACK IS ONE
 * INTEGER DOWN through nativeMediaCommand -- the native side queues it, wakes
 * its own loop and ACTS ON IT DIRECTLY, THE CAR BEING AN INTERFACE OF ITS OWN
 * (architect 2026-09-12): the wheel's three buttons are three acts of the
 * product's own, the native side forking them on whether the render player
 * stands -- ITS toggle between the item and silence and its playlist walk
 * with an up-a-folder exit, or, with it closed, the main window's play/pause
 * (which loops the trim) and UNDO / REDO on the outer two -- and no key is
 * synthesized either way. A BARE PLAY KEY IS NOT THAT TOGGLE ON EITHER SIDE
 * OF THE FORK, THOUGH (architect 2026-09-18): a head unit sends it about a
 * second after the Bluetooth link comes up as its autoplay, so the native
 * side answers it with the audio stream's silent reopen and starts nothing --
 * PAUSE and the undivided PLAY_PAUSE are the toggle instead, which is why
 * onMediaButtonEvent below keeps MEDIA_PLAY a kind of its own rather than
 * folding it into PLAY_PAUSE (the tables are at
 * GuiCarTransport::on_media_command and GuiRenderPlayer::on_media_command on
 * the native side). (Each command pressed one of
 * the player's keys until that day: Space, Home / End, Left / Right, and
 * Page Up / Page Down for the skips before 2026-08-31.) onMediaButtonEvent IS
 * OVERRIDDEN and the keycodes are mapped here, at once, rather than left to
 * the framework's default (the reasons are at the override). Audio focus is
 * REQUESTED when a push says playing and none is held and ABANDONED when a
 * push says inactive or at onDestroy -- and since the session no longer goes
 * inactive while the app runs, the abandon that happens in practice is
 * onDestroy's; a loss pauses whatever is sounding through
 * the same command road ("Android's one imposed interrupt"), a refused
 * request is logged and playback proceeds (the AAudio stream is already
 * running; focus decides who else ducks, not whether we sound).
 *
 * <p>THAT LIFETIME HAS ONE EXCEPTION: THE SESSION STEPS ASIDE WHILE ANOTHER
 * APP HOLDS THE SCREEN (architect 2026-09-18). With this app running behind
 * something else -- his own case is MPV on AirPods -- a pause and then a play
 * on the earbuds started THIS app's loop instead of resuming the app he was
 * watching, because the framework hands the media buttons to the app that is
 * still playing audio and this app's AAudio stream never stops while a project
 * is open. So onStop RELEASES the session -- setActive(false) is not the lever
 * and the machinery, all four links of it, is recorded at that override -- and
 * onStart builds a new one and re-seeds it with the last push. THE GATE IS THE
 * DISPLAY BEING ON, not the wakefulness, and it is deliberately conservative:
 * a wrong KEEP leaves a corner of the earbud bug standing, while a wrong
 * RELEASE takes the head unit away for a whole drive -- in the car this
 * activity is never left and the one lifecycle event of a drive is the cover
 * closing, over a screen that has already gone off. The absence is invisible
 * to the native side: a push that lands while the session is away is CACHED
 * for onStart to re-seed, and its audio-focus arms still run, focus following
 * the sound rather than the session.
 *
 * <p>THIS PHASE IS A MediaSession ALONE, BY RULING: no notification, no
 * foreground service, no background playback, no lock-screen transport. The
 * tablet is a kiosk on a stand -- the native side keeps the screen on -- with
 * the app in the foreground and the head unit reading the session over AVRCP,
 * and none of the machinery those would need (a notification's res/ drawable,
 * a service, the FOREGROUND_SERVICE and POST_NOTIFICATIONS permissions) is
 * added to the manifest or the build.
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
    // order, 0-based, and MEDIA_KIND_COUNT is its kGuiMediaCommandKindCount.
    // THE TWO TABLES ARE ONE LIST AND ARE EDITED IN ONE ACT -- the numbers are
    // an in-build identity and nothing persists them. THERE IS A PLAY_PAUSE
    // ROW because onMediaButtonEvent below maps the keycodes itself: the
    // undivided toggle key goes down as its own kind and the native side
    // answers it with its own toggle, rather than the framework guessing a
    // direction from the published state. ACTION_PLAY_PAUSE stays declared
    // below all the same -- the routing dispatches a key only for a declared
    // action, and that is true of the override's keys too.
    private static final int MEDIA_PLAY                 = 0;
    private static final int MEDIA_PAUSE                = 1;
    private static final int MEDIA_PLAY_PAUSE           = 2;
    private static final int MEDIA_STOP                 = 3;
    private static final int MEDIA_NEXT                 = 4;
    private static final int MEDIA_PREVIOUS             = 5;
    private static final int MEDIA_FAST_FORWARD         = 6;
    private static final int MEDIA_REWIND               = 7;
    private static final int MEDIA_SEEK_TO              = 8;
    private static final int MEDIA_FOCUS_LOST           = 9;
    private static final int MEDIA_FOCUS_LOST_TRANSIENT = 10;
    private static final int MEDIA_FOCUS_GAINED         = 11;
    private static final int MEDIA_KIND_COUNT           = 12;

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
    // and the lifecycle overrides that touch them run on the UI thread.
    private MediaSession      session;
    private AudioManager      audioManager;
    private AudioFocusRequest focusRequest;
    private boolean           focusHeld;
    private boolean           released;

    // THE STEP-ASIDE LATCH: true exactly while there is no session BECAUSE
    // ANOTHER APP HAS THE SCREEN. onStop's gate is its only writer and
    // onStart's rebuild its only clearer, so it is what tells a null `session`
    // apart from the process ending (`released`) and from the moments before
    // onCreate has built one. A push that lands while it stands touches no
    // session and cannot resurrect one: the two creators are onCreate and
    // onStart, both on the UI thread, because a session delivers its callbacks
    // on its creating thread's Looper.
    private boolean steppedAside;

    // THE LAST PUSH, FOR EXACTLY ONE READER: onStart's re-seed, a fresh session
    // carrying no metadata of its own while the native side's publisher pushes
    // only on CHANGE. EVERY push records itself here, including one that lands
    // while the session is away. `playing` is NOT among them and needs no
    // field: it is read for the audio focus alone, the focus arms run on the
    // push itself in either state, and the re-seed asks nothing of it.
    private boolean havePushed;
    private boolean lastActive;
    private String  lastTitle;
    private String  lastArtist;
    private String  lastAlbum;
    private long    lastDurationMs;
    private long    lastPositionMs;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // FULL SCREEN FROM THE FIRST FRAME (the rule is at the head of this
        // class), AFTER super: NativeActivity's own onCreate installs the
        // decor (setContentView), and the insets controller is the decor's.
        hideSystemBars();

        // THE BATTERY, for the menu row's legend: the registration answers the
        // sticky ACTION_BATTERY_CHANGED intent at once, so the native side's
        // cold answer (level and plug unknown) is replaced within the
        // activity's first moments, and the receiver keeps it current (the block at batteryReceiver). No
        // permission is needed, and no export flag: the action is a protected
        // system broadcast, which a context-registered receiver may take
        // without one.
        final Intent sticky = registerReceiver(batteryReceiver,
                new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (sticky != null) pushBattery(sticky);

        // THE MEDIA SESSION, through its ONE owner below and on the UI thread,
        // which is why the call is here and not on the native side's road: a
        // session delivers its callbacks on its creating thread's Looper. The
        // lock is taken because super.onCreate above has already started the
        // native loop's thread, and that thread's mediaState is the other
        // writer of every field the owner touches.
        synchronized (this) {
            createSessionLocked();
        }

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

    // THE BARS COME BACK AT EVERY FOCUS GAIN, so they are hidden again here
    // (the full-screen rule is at the head of this class): the system shows
    // them across the shade, a dialog or a task switch, and a window that
    // regains focus re-asserts its own mode. super FIRST: NativeActivity's
    // override is what hands the focus edge to the native side
    // (APP_CMD_GAINED_FOCUS / APP_CMD_LOST_FOCUS).
    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    // BOTH BARS HIDDEN, STICKY IMMERSIVE (architect 2026-10-01): a swipe from
    // an edge shows them transiently over the app and they hide again by
    // themselves. getInsetsController() is declared nullable for a window with
    // no decor view; super.onCreate installed the decor before the first call,
    // so this is the API's contract rather than a fault with a producer here.
    private void hideSystemBars() {
        final WindowInsetsController bars = getWindow().getInsetsController();
        if (bars == null) return;
        bars.setSystemBarsBehavior(
                WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        bars.hide(WindowInsets.Type.statusBars()
                | WindowInsets.Type.navigationBars());
    }

    // THE BATTERY, THE THIRD JNI ROAD DOWN (architect 2026-10-01): one call per
    // ACTION_BATTERY_CHANGED, the car's road (nativeMediaCommand) rather than a
    // native read up into Java at each refresh, because the broadcast IS the
    // change (the reasoning is at GuiPlatform::battery_status,
    // src/gui/platform_android.h). The native side stores the reading in one
    // atomic word, so the call is safe before the native loop's init and after
    // its shutdown and needs no lock here. `percent` is 0..100, or -1 for a
    // level the intent did not carry (a missing or negative EXTRA_LEVEL, or no
    // usable EXTRA_SCALE); `plugged` is EXTRA_PLUGGED as carried -- 0 on
    // battery, any positive power source plugged, -1 for a missing extra,
    // which the legend shows as its unknown glyph.
    private static native void nativeBatteryState(boolean present, int percent,
                                                  int plugged);

    private void pushBattery(Intent intent) {
        final boolean present =
                intent.getBooleanExtra(BatteryManager.EXTRA_PRESENT, true);
        final int level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
        final int scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
        final int percent = (level >= 0 && scale > 0) ? level * 100 / scale : -1;
        final int plugged = intent.getIntExtra(BatteryManager.EXTRA_PLUGGED, -1);
        nativeBatteryState(present, percent, plugged);
    }

    // THE RECEIVER keeps the reading current for the activity's life:
    // registered in onCreate (whose registration also answers the sticky
    // intent) and unregistered in onDestroy. onReceive runs on the UI thread.
    private final BroadcastReceiver batteryReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent != null) pushBattery(intent);
        }
    };

    // THE SESSION'S ONE CREATOR, with TWO callers -- onCreate's and onStart's
    // rebuild after a step-aside -- and both are on the UI thread, which is the
    // whole reason this is a method and not a native-side road: a MediaSession
    // delivers its callbacks on its CREATING thread's Looper, so a session
    // built anywhere else would hand the head unit's buttons to a thread with
    // no Looper to run them on. INACTIVE as it leaves here: the native side's
    // push is what activates it, which on a first create comes on the first
    // tick of the first project and then stands for the app's life (architect
    // 2026-09-17) -- it stayed inactive until the render player stood until
    // that day, and the head unit's buttons reached nothing while the waveform
    // was being edited, which is exactly what changed: they drive the main
    // window's transport now. The state is seeded STOPPED with the full action
    // set so the framework's default media-button routing has actions to
    // dispatch against from the first activation. SPEED 0: nothing is playing,
    // and the speed a state carries is the RATE OF PLAYBACK, off which a
    // controller extrapolates the position from the moment of the push (the
    // rule at mediaState).
    private void createSessionLocked() {
        session = new MediaSession(this, TAG);
        session.setCallback(new TransportCallback());
        session.setPlaybackState(new PlaybackState.Builder()
                .setActions(SESSION_ACTIONS)
                .setState(PlaybackState.STATE_STOPPED, 0L, 0.0f)
                .build());
        session.setActive(false);
    }

    // THE OTHER HALF OF THE STEP-ASIDE: the session comes back when this
    // activity is in front again. A FRESH SESSION CARRIES NO METADATA AND NO
    // STATE OF ITS OWN, and the native side's publisher is a per-tick
    // COMPARATOR that pushes only when a field changes (GuiCarTransport::tick;
    // the render player publishes at the edges of its own display), so without
    // this re-seed the head unit would show nothing at all until something
    // happened to change. It is applied exactly as a push is, through the one
    // publisher below. THE CACHED POSITION IS STALE by however long the step
    // aside lasted; the next change-push corrects it, and the car never reaches
    // this path at all -- the session is not released there (the gate is at
    // onStop).
    @Override
    protected void onStart() {
        super.onStart();
        synchronized (this) {
            if (released || !steppedAside) return;
            steppedAside = false;
            createSessionLocked();
            Log.i(TAG, "onStart rebuilt the media session (cached push "
                    + (havePushed ? "re-seeded)" : "none yet)"));
            if (havePushed) {
                publishToSessionLocked(lastActive, lastTitle, lastArtist,
                                       lastAlbum, lastDurationMs,
                                       lastPositionMs);
            }
        }
    }

    // THE SESSION STEPS ASIDE WHILE ANOTHER APP HOLDS THE SCREEN (architect
    // 2026-09-18). THE SYMPTOM: with this app running behind MPV on AirPods, a
    // pause and then a play on the earbuds started THIS app's loop instead of
    // resuming MPV, "even though the GUI is not focused, and the most recent
    // thing I was using is MPV". Before the car transport drove the project's
    // own playback the same press did nothing at all.
    //
    // THE MECHANISM IS FOUR LINKS, and each one was read rather than assumed
    // (reproduced on the tablet with `dumpsys media_session`, which named
    // `com.warptempo.gui` the media button session while MPV was the resumed
    // activity and paused):
    //   1. OUR AUDIO PLAYER NEVER STOPS. `dumpsys audio` with this activity
    //      STOPPED still reports our AAudio stream `state:started` against
    //      MPV's `state:paused` -- the no-click lifecycle the crackle ruling
    //      keeps, the stream being started at a project's open and stopped only
    //      where it is about to be closed. In the framework's eyes this app is
    //      playing audio at every moment it runs.
    //   2. THE FRAMEWORK PROMOTES THE ONLY APP THAT IS STILL PLAYING.
    //      AudioPlayerStateMonitor keeps its uid list most-recently-started
    //      first and then moves the first uid that is ACTIVELY playing to the
    //      head of it. With MPV paused, that uid is ours.
    //   3. THE SESSION AT THAT UID IS THEN CHOSEN AND ITS ACTIVE FLAG IS NEVER
    //      CONSULTED. MediaSessionStack walks that list and takes the first uid
    //      that HAS a session; the match tests the uid and the playback state
    //      and nothing else, the active-state change only drops a cache, and
    //      the dispatch site sends the button to whatever that walk returned.
    //   4. NOR DOES THE APP SIDE GATE IT: MediaSession's dispatch posts the
    //      button straight to the callback without reading its own active bit.
    // SO setActive(false) CANNOT BE THE LEVER -- an inactive session goes on
    // receiving media buttons. release() is: the record leaves the stack, and a
    // uid with no session is SKIPPED, so the walk falls through to the next
    // app, which is the one the user is watching. Verified on the device: with
    // our session gone the earbud press resumed MPV at its own position.
    //
    // THE AAUDIO STREAM IS NOT A LEVER HERE AND WILL NOT BE MADE ONE (architect
    // 2026-09-18, after verifying this fix on his AirPods Pro): stopping it so
    // the tablet stops reading as a player was proposed and REFUSED -- "I can
    // accept closing the GUI. I prefer that to crackling issues in the car or a
    // wild goose chase for a fix that won't happen" -- the crackle ruling
    // standing. Nor is it needed: with this app in the background and its
    // session released, three earbud presses reached MPV and MPV obeyed all
    // three. WHAT REMAINS IS ANOTHER APP'S CHURN: the tablet's AVRCP layer moves
    // its "active player" between every app holding an active session, and
    // YouTube Music holds one permanently, so a press can land there. CLOSING
    // THIS APP is the accepted answer when the buds misbehave; our own release
    // is honoured within 43 ms of onStop. The diagnosis road costs no build:
    // `dumpsys media_session` (the media-button session and its key-event log),
    // `dumpsys bluetooth_manager`'s AvrcpTargetService section (the controllers,
    // the active-player events, every key the buds sent and where it went) and
    // `dumpsys audio` (which players are started or paused).
    //
    // THE GATE IS THE DISPLAY, NOT THE WAKEFULNESS, AND IT IS CONSERVATIVE BY
    // DESIGN: it steps aside only when BOTH the activity's own display reads
    // STATE_ON AND PowerManager.isInteractive() agrees the screen is up, and
    // EITHER of them saying otherwise KEEPS the session. A wrong KEEP leaves a
    // corner of the earbud bug standing; a wrong RELEASE takes the head unit
    // away for a whole drive -- and his car fact is what makes that the worse
    // failure (2026-09-18): "in the car, I use only the GUI. I shut the cover
    // but I never change apps." WHY isInteractive() CANNOT STAND ALONE: the
    // cover close is ordered, and the DISPLAY goes off long before this
    // override runs while the WAKEFULNESS follows it. Measured from his own
    // cover close with this activity resumed -- `Going to sleep due to
    // cover_close` at .578, `screenTurnedOff()` and the Display-off sleep token
    // (which is what pauses the activity) at .955, `wm_on_paused_called` at
    // .960, `wm_on_stop_called` at 54.003, `Dozing...` at 54.267 -- so at our
    // onStop the display is already off and isInteractive() still reads true. A
    // gate on the wakefulness alone would release the session at every cover
    // close.
    @Override
    protected void onStop() {
        super.onStop();
        synchronized (this) {
            if (released || session == null) return;

            // A NULL DISPLAY IS "WE CANNOT SEE THE SCREEN" and keeps the
            // session, the same answer the conservative gate gives to every
            // other uncertainty.
            final Display display = getDisplay();
            final int displayState = display == null
                    ? Display.STATE_UNKNOWN
                    : display.getState();
            final PowerManager power =
                    (PowerManager) getSystemService(POWER_SERVICE);
            final boolean interactive = power != null && power.isInteractive();
            if (displayState != Display.STATE_ON || !interactive) {
                Log.i(TAG, "onStop keeps the media session (display state "
                        + displayState + ", interactive " + interactive + ")");
                return;
            }

            // THE ONE RESIDUAL CORNER, and it is Android's own answer rather
            // than ours: if this app is in front and the user moves to an app
            // that is playing NOTHING, the released session leaves the
            // framework with no media button session at all, and the next press
            // reaches its last media-button receiver instead. That is what
            // "nobody is playing" means to the framework, and it is what any
            // app that releases its session produces.
            Log.i(TAG, "onStop releases the media session (display state "
                    + displayState + ", interactive " + interactive + ")");
            steppedAside = true;
            session.setActive(false);
            session.release();
            session = null;
            // AUDIO FOCUS IS NOT TOUCHED HERE: the stream is still running and
            // still sounding, and focus follows the sound rather than the
            // session.
        }
    }

    // THE LIFECYCLE OVERRIDE THAT TAKES THE SESSION DOWN FOR GOOD, and it
    // exists for the session: a MediaSession is a system-side object that
    // outlives a released-without-release() activity and keeps its
    // media-button claim, so it is released here. super.onDestroy() FIRST:
    // NativeActivity's own onDestroy posts APP_CMD_DESTROY and JOINS the
    // native loop's thread, so by the time it returns no mediaState call can
    // still be in flight and the session may be taken down under the one lock
    // with nothing racing it. THE SESSION MAY ALREADY BE GONE -- a step-aside
    // released it and the process is ending behind another app's screen -- and
    // the null test that has always been here is what covers that; `released`
    // is what stops onStart from ever building another.
    // FOCUS IS ABANDONED HERE AND, IN PRACTICE, ONLY HERE (2026-09-17):
    // the native side's inactive push was what released it at the render
    // player's close, and that push is gone with the session's new lifetime
    // (the session stands for the app's life, the step-aside apart), so this is
    // the abandon a running app always reaches -- which is also why it was
    // already written to cover a process ending with something still sounding.
    // THE BATTERY RECEIVER IS UNREGISTERED FIRST (2026-10-01): its pushes touch
    // one native atomic and nothing the session's lock guards, so its place
    // ahead of super is free, and unregistering a receiver registered in
    // onCreate is the pairing the framework expects.
    @Override
    protected void onDestroy() {
        unregisterReceiver(batteryReceiver);
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
    // called on the native loop's thread whenever what the head
    // unit shows changes -- never per tick: a PLAYING state advances on the
    // head unit's own clock from `positionMs` at speed 1.0, which is what the
    // (state, position, speed) triple means. THE NATIVE SIDE HAS TWO CALLERS
    // (2026-09-17), one live at a time: the render player, which calls at the
    // edges of its own display, and the project transport, which compares its
    // derived state on every tick and calls only when a field changed.
    //
    // METADATA IS THE CONSOLE'S THREE LINES (architect 2026-09-12, from the
    // car): the Accord lays them out as ALBUM above the title, dim, and
    // ARTIST below it, and fills all three whatever is in them, so each says
    // something different -- ALBUM the project's name, the dim top line
    // taking the LEAST important of the three; ARTIST the folder (the
    // playing item's own while it sounds, otherwise the one the listing is
    // in -- or, with the render player closed, THE A/B TAB AND THE VIEW,
    // "A) T+W"), the console's bottom line; TITLE the bare name of the
    // playing file or of the highlighted row -- or, with the player closed,
    // WHERE THE SESSION STANDS IN THE UNDO HISTORY, spelled as a batch cell's
    // basename is ("5_2"). (The first try had it backwards: ARTIST
    // carried the project's name and ALBUM the folder, on the assumption that
    // ARTIST was the top line -- it is the bottom one, and the ruling above
    // swapped the two strings once the layout was seen on the console
    // itself.) DURATION is the length of what is sounding WHEN THERE IS ONE.
    //
    // THE STATE IS A DUMMY AND SAYS PLAYING WHENEVER THE APP IS RUNNING
    // (architect 2026-09-12, from the car, widened 2026-09-17 with the
    // session's lifetime): `active` is the whole fork --
    // inactive is STOPPED, anything else is PLAYING at speed 1.0 -- because a
    // console reads the still-streaming Bluetooth link as playing and
    // OVERRIDES a session that says PAUSED, so its PAUSE stuck every time. The
    // session tells it what it already believes and its one button becomes a
    // plain toggle. `playing` is the TRUE transport bit and is read HERE FOR
    // THE AUDIO FOCUS ALONE. A DURATION OF 0 OR LESS PUTS NO DURATION KEY AT
    // ALL, which is Android's "unknown", AND A LENGTH ARRIVES ONLY WHILE
    // SOMETHING SOUNDS: with nothing sounding under the render player the
    // native side
    // sends a SILENCE TRACK naming the highlighted row at position 0 with the
    // duration unknown, so the console counts up from zero with no length to
    // run into (the rule is at GuiRenderPlayer::publish_media_state), and the
    // PROJECT TRANSPORT sends THE TRIM WINDOW'S LENGTH while its loop is live
    // -- the trim is the whole lap, so the bar here fills through it and
    // refills at each wrap, its clock being the loop's position inside the
    // trim, re-sent at each lap -- and the duration unknown at rest, where a
    // length under a PLAYING state would run this clock into a track end that
    // is not sounding (GuiCarTransport::derive).
    // setActive FOLLOWS THE PUSH, and the push says active for the app's life
    // (architect 2026-09-17): the render player's open and close are the wire
    // changing owners on the native side, not the session coming and going, so
    // the two setActive(false) calls a running app reaches are onDestroy's and
    // the step-aside's at onStop, neither of them a push.
    // A PUSH THAT FINDS NO SESSION -- the step-aside's window -- IS STILL A
    // PUSH: it records itself for onStart's re-seed and runs the focus arms
    // below, and only the session's own setters are skipped.
    // Every setter here is a binder call and is callable from
    // any attached thread; the lock is against onDestroy's release on the UI
    // thread. Focus: requested when a push says playing and none is held;
    // abandoned when a push says inactive -- AN ARM WITH NO PRODUCER WHILE THE
    // APP RUNS since that day, kept because it is the honest pair to the
    // request and because a future inactive push must still let go. SO FOCUS,
    // ONCE GRANTED, IS HELD UNTIL onDestroy ABANDONS IT, unless the system
    // takes it away first (AUDIOFOCUS_LOSS clears focusHeld in the listener
    // below and the next playing push requests again). That is the accepted
    // shape for a kiosk tablet on a stand with this app in the foreground:
    // holding focus across the gaps between plays costs nothing here and
    // keeps the head unit pointed at this session.
    public synchronized void mediaState(boolean active, boolean playing,
                                        String title, String artist,
                                        String album,
                                        long durationMs, long positionMs) {
        if (released) return;

        // EVERY PUSH RECORDS ITSELF, the ones that land while the session has
        // stepped aside included: what onStart re-seeds must be the last
        // picture the native side MEANT, not the last one that reached a
        // session.
        havePushed     = true;
        lastActive     = active;
        lastTitle      = title;
        lastArtist     = artist;
        lastAlbum      = album;
        lastDurationMs = durationMs;
        lastPositionMs = positionMs;

        if (session != null) {
            publishToSessionLocked(active, title, artist, album,
                                   durationMs, positionMs);
        }

        // THE FOCUS ARMS RUN WHETHER OR NOT THERE IS A SESSION, because focus
        // follows THE SOUND and not the session: the AAudio stream goes on
        // sounding through a step-aside, so a push that says playing still asks
        // for focus and an inactive push still lets it go. THE MACHINE IS ASKED
        // FOR RATHER THAN ASSUMED, the same test onDestroy's abandon makes:
        // onCreate builds it BELOW the session and the native loop's thread is
        // already running by then, so a first push can reach here before it
        // exists.
        if (audioManager == null || focusRequest == null) return;

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

    // THE PICTURE'S ONE WRITER ONTO THE SESSION, with TWO callers -- a push
    // above and onStart's re-seed after a step-aside -- so a rebuilt session
    // carries exactly what a push carries and the two can never come to
    // disagree. It touches the session and nothing else: the audio focus is the
    // push's own business (the rule is above) and is no part of re-seeding a
    // display. The caller holds the lock.
    //
    // THE SPEED IS THE RATE OF PLAYBACK, not a constant: a controller
    // EXTRAPOLATES the position from `positionMs` at this speed and the moment
    // of this call. The clock is meant to run whenever the app is running --
    // that is the dummy display's other half -- so the speed is the state's
    // own: 1.0 while active and 0.0 at an inactive push, which no running app
    // makes any more.
    private void publishToSessionLocked(boolean active,
                                       String title, String artist,
                                       String album,
                                       long durationMs, long positionMs) {
        final MediaMetadata.Builder meta = new MediaMetadata.Builder()
                .putString(MediaMetadata.METADATA_KEY_TITLE, title)
                .putString(MediaMetadata.METADATA_KEY_ARTIST, artist)
                .putString(MediaMetadata.METADATA_KEY_ALBUM, album);
        if (durationMs > 0) {
            meta.putLong(MediaMetadata.METADATA_KEY_DURATION, durationMs);
        }
        session.setMetadata(meta.build());

        final int state = active ? PlaybackState.STATE_PLAYING
                                 : PlaybackState.STATE_STOPPED;
        final float speed = active ? 1.0f : 0.0f;
        session.setPlaybackState(new PlaybackState.Builder()
                .setActions(SESSION_ACTIONS)
                .setState(state, positionMs, speed)
                .build());
        session.setActive(active);
    }

    // THE SYSTEM CLIPBOARD, THE SECOND JNI ROAD UP (architect 2026-09-03,
    // "if it's cheap, let's go ahead and build it"): ClipboardManager is a
    // Java object with no NDK surface, so these two methods are what the
    // native clipboard_set_text / clipboard_get_text call
    // (src/gui/platform_android.cpp), on the native loop's thread, exactly as
    // mediaState is called. setPrimaryClip and getPrimaryClip are binder
    // calls and are legal from any attached thread; neither needs the UI
    // thread and neither is posted to a Looper.
    //
    // THE PAYLOAD IS BYTES, NOT A String, ON BOTH ROADS. JNI's NewStringUTF /
    // GetStringUTFChars speak MODIFIED UTF-8 -- a supplementary character as
    // two three-byte surrogate halves, U+0000 as C0 80 -- and the product's
    // text is UTF-8 verbatim and round-trips byte-identically (the text
    // ruling in CLAUDE.md; text_editor::replace_selection is the one incoming
    // filter). So the array crosses raw and the decode and encode happen
    // here, in real UTF-8. A byte[] the decoder cannot read yields U+FFFD
    // rather than an exception, which is the same "nothing here refuses"
    // posture the head unit's title takes.
    //
    // NO synchronized, AND THAT IS DELIBERATE: mediaState takes `this`
    // because it touches the session, the focus request and `released`, all
    // shared with the UI thread's onDestroy. These two touch NO field of this
    // object -- the manager is fetched per call (getSystemService is a cached
    // lookup) -- so there is nothing for a lock to protect, and taking `this`
    // would put a clipboard binder call in the way of onDestroy's release.
    private static final byte[] NO_BYTES = new byte[0];

    // THE PASTE'S ONE PAYLOAD BOUND, MIRRORED FROM THE NATIVE SIDE:
    // kClipboardMaxBytes (src/gui/gui_input.h), the same number the Wayland
    // read has always enforced. THE TWO ARE ONE NUMBER AND ARE EDITED IN ONE
    // ACT -- the media command table's rule (gui_media.h) for the same reason
    // it applies there: the APK build compiles this file against no C++
    // header, so no assert can hold them together. The native reader checks
    // the array it is handed as well; this copy is what keeps an oversized
    // clip from being encoded and crossing JNI at all.
    private static final int MAX_CLIPBOARD_BYTES = 1024 * 1024;

    // FALSE ON ANY THROWABLE is the verdict the native side hands back to the
    // GUI, which cards "The clipboard did not take the copy" on it: some OEM
    // builds refuse setPrimaryClip with a SecurityException, and a copy that
    // did not happen must not be announced as one.
    public boolean clipboardSet(byte[] utf8) {
        if (utf8 == null) return false;
        try {
            final ClipboardManager clip =
                    (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
            if (clip == null) return false;
            clip.setPrimaryClip(ClipData.newPlainText(
                    "warptempo", new String(utf8, StandardCharsets.UTF_8)));
            return true;
        } catch (Throwable t) {
            Log.w(TAG, "clipboard copy refused", t);
            return false;
        }
    }

    // AN EMPTY ARRAY IS THE EMPTY ANSWER, never null and never an exception:
    // no clip, an empty clip, a first item carrying no text, or a system that
    // withholds the clip all mean "there is nothing to paste", which is the
    // consumed no-op the GUI already handles (the clipboard section of
    // platform_wayland.cpp states the rule). On Android 10 and later
    // getPrimaryClip answers null unless the
    // app has window focus; this one is a foreground kiosk, so the withheld
    // case is the same empty answer rather than a state to detect.
    //
    // getText(), NOT coerceToText(): the Wayland twin accepts text/plain
    // mimes and nothing else, so a clip whose first item is a URI or an
    // Intent reads as nothing to paste here for the same reason -- and
    // coerceToText would open a ContentResolver on the GUI loop's thread to
    // find that out.
    public byte[] clipboardGet() {
        try {
            final ClipboardManager clip =
                    (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
            if (clip == null) return NO_BYTES;
            final ClipData data = clip.getPrimaryClip();
            if (data == null || data.getItemCount() == 0) return NO_BYTES;
            final CharSequence text = data.getItemAt(0).getText();
            if (text == null || text.length() == 0) return NO_BYTES;
            // TWO CHECKS, THE CHEAP ONE FIRST: UTF-8 is at least one byte per
            // char, so a char count past the bound cannot encode under it and
            // the encoding is skipped altogether; the encoded array is then
            // measured for the char counts that could go either way. An
            // oversized clip is the same empty answer every other refusal is
            // (the native reader logs the abandonment; this side keeps the
            // bytes out of the process).
            if (text.length() > MAX_CLIPBOARD_BYTES) return NO_BYTES;
            final byte[] utf8 = text.toString().getBytes(StandardCharsets.UTF_8);
            if (utf8.length > MAX_CLIPBOARD_BYTES) return NO_BYTES;
            return utf8;
        } catch (Throwable t) {
            Log.w(TAG, "clipboard read refused", t);
            return NO_BYTES;
        }
    }

    // THE HEAD UNIT'S BUTTONS, one integer each. The transport methods below
    // remain because a controller can call them directly (a system UI, an
    // AVRCP command that arrives as an action rather than as a key), but the
    // MEDIA KEYS DO NOT REACH THEM: onMediaButtonEvent takes them first. Every
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

        // THE KEYS ARE MAPPED HERE, AT ONCE, AND THE FRAMEWORK'S DEFAULT IS
        // BYPASSED. That default is not this product's behaviour: given a
        // declared ACTION_SKIP_TO_NEXT it HOLDS a KEYCODE_MEDIA_PLAY_PAUSE
        // press for ViewConfiguration.getDoubleTapTimeout() to see whether a
        // second press follows, and turns two quick presses into
        // onSkipToNext. That is a delay between the wheel button and the
        // sound with nothing on screen to explain it, and a double-tap-to-Next
        // gesture nobody ruled -- Next is its own button on the wheel. So the
        // key is read straight off the intent and sent down undivided
        // (MEDIA_PLAY_PAUSE), and the native side answers it with the
        // player's own Space toggle.
        //
        // ACTION_DOWN ONLY. A repeat (a held button) and the matching
        // ACTION_UP are consumed and dropped: every car command is an act at
        // the press, exactly as the product's own hotkeys are, and none of
        // them repeats. A keycode this does not map -- anything that is not a
        // transport key -- goes to super, which is where a controller's own
        // handling still belongs.
        @Override
        public boolean onMediaButtonEvent(Intent intent) {
            // getParcelableExtra(String) is deprecated in the API 35 jar this
            // compiles against; its typed replacement landed in API 33 and
            // the manifest's minSdk is 30, so the deprecated call is the one
            // that runs on every device this ships to. One javac warning is
            // expected.
            @SuppressWarnings("deprecation")
            final KeyEvent key = intent == null
                    ? null
                    : (KeyEvent) intent.getParcelableExtra(Intent.EXTRA_KEY_EVENT);
            if (key == null) return super.onMediaButtonEvent(intent);

            final int kind;
            switch (key.getKeyCode()) {
                case KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE:
                case KeyEvent.KEYCODE_HEADSETHOOK:
                    kind = MEDIA_PLAY_PAUSE;      break;
                case KeyEvent.KEYCODE_MEDIA_PLAY:
                    kind = MEDIA_PLAY;            break;
                case KeyEvent.KEYCODE_MEDIA_PAUSE:
                    kind = MEDIA_PAUSE;           break;
                case KeyEvent.KEYCODE_MEDIA_STOP:
                    kind = MEDIA_STOP;            break;
                case KeyEvent.KEYCODE_MEDIA_NEXT:
                    kind = MEDIA_NEXT;            break;
                case KeyEvent.KEYCODE_MEDIA_PREVIOUS:
                    kind = MEDIA_PREVIOUS;        break;
                case KeyEvent.KEYCODE_MEDIA_FAST_FORWARD:
                    kind = MEDIA_FAST_FORWARD;    break;
                case KeyEvent.KEYCODE_MEDIA_REWIND:
                    kind = MEDIA_REWIND;          break;
                default:
                    return super.onMediaButtonEvent(intent);
            }
            if (key.getAction() == KeyEvent.ACTION_DOWN
                    && key.getRepeatCount() == 0) {
                nativeMediaCommand(kind, 0L);
            }
            return true;
        }
    }

    // THE FOCUS MACHINE'S OTHER HALF: a permanent LOSS releases the hold (the
    // system took it; the next playing push requests again -- and since
    // 2026-09-17 this listener is the only thing that ever clears focusHeld
    // while the app runs, the inactive push that used to abandon it having
    // gone with the session's new lifetime), a transient loss
    // keeps it (GAIN returns it), and each is forwarded so the native side
    // STOPS WHATEVER IS SOUNDING -- the render player's item, or the project's
    // own transport with the player closed, the fork being the native side's;
    // GAIN is forwarded and the native side does nothing with it --
    // NOTHING RECOVERS BY ITSELF, the user presses play.
    //
    // THERE IS NO CAN_DUCK ARM, and its absence is the policy (2026-09-02,
    // the four-tier review's R-18(e)). Ducking is the framework's, as the
    // request above says: with setWillPauseWhenDucked left false the system
    // lowers this app's volume itself and never calls this listener for
    // AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK. The arm that stood here forwarded it
    // as a transient loss, which PAUSES -- the opposite of the stated policy,
    // in a case with no producer. It falls to `default` now, so a framework
    // that ever did deliver it would duck the music rather than stop it, which
    // is what a navigation prompt over this player should do.
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
