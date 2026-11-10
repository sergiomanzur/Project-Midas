package org.midas.android;

import android.content.Intent;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;

/**
 * SDL entry point for the game. The ROM has already been validated and
 * installed in app-private storage by {@link RomActivity}.
 */
public class MidasActivity extends SDLActivity {
    private TouchControlsView controls;

    /* Keyboard shortcut: Ctrl+Shift+T. Touch fallback: three-finger tap.
     * The old on-screen HIDE button sat beside the shoot button, so it was
     * removed; these two gestures are the only way to toggle the overlay. */
    private boolean overlayGestureCandidate;
    private int overlayGestureMaxPointers;
    private long overlayGestureDownTime;
    private float overlayGestureDownX;
    private float overlayGestureDownY;
    private boolean overlayGestureMoved;

    @Override
    protected String[] getLibraries() {
        return new String[]{"SDL2", "main"};
    }

    @Override
    protected String[] getArguments() {
        File rom = RomStore.romFile(this);
        return new String[]{"--rom-file", rom.getAbsolutePath()};
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (controls != null
                && event.getAction() == KeyEvent.ACTION_DOWN
                && event.getRepeatCount() == 0
                && event.getKeyCode() == KeyEvent.KEYCODE_T
                && event.isCtrlPressed()
                && event.isShiftPressed()) {
            toggleTouchOverlay();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        trackOverlayToggleGesture(event);
        return super.dispatchTouchEvent(event);
    }

    private void toggleTouchOverlay() {
        if (controls == null) {
            return;
        }
        controls.toggleTouchControls();
        Toast.makeText(this,
                controls.isTouchControlsVisible()
                        ? "Touch controls shown"
                        : "Touch controls hidden - Ctrl+Shift+T or three-finger tap to show",
                Toast.LENGTH_SHORT).show();
    }

    /*
     * Observe every touch without consuming it: the SDL surface keeps receiving
     * the same gesture. A quick, still three-finger tap toggles the overlay, so
     * touch-only devices can bring it back after it has been hidden.
     */
    private void trackOverlayToggleGesture(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                overlayGestureCandidate = true;
                overlayGestureMaxPointers = 1;
                overlayGestureDownTime = SystemClock.uptimeMillis();
                overlayGestureDownX = event.getX();
                overlayGestureDownY = event.getY();
                overlayGestureMoved = false;
                break;

            case MotionEvent.ACTION_POINTER_DOWN:
                if (event.getPointerCount() > overlayGestureMaxPointers) {
                    overlayGestureMaxPointers = event.getPointerCount();
                }
                break;

            case MotionEvent.ACTION_MOVE: {
                if (!overlayGestureMoved) {
                    final float slop =
                            ViewConfiguration.get(this).getScaledTouchSlop() * 2.0f;
                    if (Math.abs(event.getX() - overlayGestureDownX) > slop
                            || Math.abs(event.getY() - overlayGestureDownY) > slop) {
                        overlayGestureMoved = true;
                    }
                }
                break;
            }

            case MotionEvent.ACTION_UP:
                if (overlayGestureCandidate
                        && overlayGestureMaxPointers >= 3
                        && !overlayGestureMoved
                        && SystemClock.uptimeMillis() - overlayGestureDownTime < 600L) {
                    toggleTouchOverlay();
                }
                overlayGestureCandidate = false;
                break;

            case MotionEvent.ACTION_CANCEL:
                overlayGestureCandidate = false;
                break;

            default:
                break;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        GameData.ensureInstalled(this);
        RomStore.recoverInterruptedImport(this);
        RomStore.ensureAndroidConfig(this);
        boolean hasRom = RomStore.hasUsableRom(this);
        super.onCreate(savedInstanceState);

        /* Test hook (adb am start -n org.midas.android/.RomActivity --es ge_stage dam):
         * start straight into a mission. The engine reads GE_STAGE lazily on its
         * first front-end tick, so setting it here is early enough. */
        String stage = getIntent() != null ? getIntent().getStringExtra("ge_stage") : null;
        if (stage != null && !stage.isEmpty()) {
            try {
                SDLActivity.nativeSetenv("GE_STAGE", stage);
            } catch (Throwable ignored) {
                // Not fatal: the game just starts at the normal menu.
            }
        }

        /* Same hook for every other engine test variable: an extra named ge_xxx becomes the
         * environment variable GE_XXX (--es ge_mp temple --es ge_mpplayers 1, --es ge_drive 1,
         * --es ge_spawnxyz "x,y,z", ...). */
        android.os.Bundle extras = getIntent() != null ? getIntent().getExtras() : null;
        if (extras != null) {
            for (String extra : extras.keySet()) {
                Object value = extras.get(extra);
                if (extra.startsWith("ge_") && !extra.equals("ge_stage") && value instanceof String) {
                    try {
                        SDLActivity.nativeSetenv(extra.toUpperCase(java.util.Locale.ROOT), (String) value);
                    } catch (Throwable ignored) {
                        // Not fatal: the game just starts at the normal menu.
                    }
                }
            }
        }

        if (!hasRom) {
            // The launcher normally prevents this. If the game activity is
            // restored after its data was cleared, go back to the importer
            // instead of starting native code without a cart. finish() here
            // prevents SDL's native thread from being resumed.
            startActivity(new Intent(this, RomActivity.class));
            finish();
            return;
        }

        if (!SDLActivity.mBrokenLibraries) {
            controls = new TouchControlsView(this);
            addContentView(controls, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }
    }

    @Override
    protected void onPause() {
        if (controls != null) {
            controls.resetTouchInput();
        }
        AndroidInput.reset();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        AndroidInput.reset();
        super.onDestroy();
    }
}
