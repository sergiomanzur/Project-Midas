package org.midas.android;

import android.content.Intent;
import android.os.Bundle;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import org.libsdl.app.SDLActivity;

import java.io.File;

/**
 * SDL entry point for the game. The ROM has already been validated and
 * installed in app-private storage by {@link RomActivity}.
 */
public class MidasActivity extends SDLActivity {
    private TouchControlsView controls;
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
