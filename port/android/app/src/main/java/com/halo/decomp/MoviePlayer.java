package com.halo.decomp;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.view.SurfaceHolder;
import android.view.MotionEvent;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;

import java.io.File;

/**
 * The movies the game asks for.
 *
 * The port has no Bink decoder (the RAD SDK is proprietary) and Android's own
 * player cannot read a .bik, so the open-source decoder in libbinkplayer.so
 * (FFmpeg's Bink demuxer and decoders) plays the movie the disc carries,
 * drawn straight to the SurfaceView over the game. The files come from the
 * disc the player extracted (its bink folder), so nothing of the game is
 * packaged with the app.
 *
 * The native side (port/android/host/host_movie.c) is told which movie the
 * game asked for and how far the player is: the engine's own playback loop
 * (bink_playback.c) ends the movie when it reaches its last frame, and stops
 * it when a button skips it.
 */
final class MoviePlayer implements SurfaceHolder.Callback, Runnable {
    private static native int nativePoll();
    private static native void nativeReady(boolean ready);
    private static native void nativeStarted(int movie);
    private static native void nativeProgress(int movie, int positionMs, int durationMs);
    private static native void nativeFinished(int movie);
    /* the asset the movie the guest asked for plays (the language the engine
       chose: host_movie.c resolves it) */
    private static native String nativeAssetName(int movie);
    /* one of the files the disc the player extracted carries */
    private static native void nativeRegisterAsset(String name);

    private final Context context;
    private final SurfaceView view;
    private final View overlay;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private BinkPlayer player;
    private int movie;
    private boolean surfaceReady;
    private boolean starting;

    MoviePlayer(Context context, ViewGroup layout, View overlay) {
        this.context = context;
        this.overlay = overlay;
        view = new SurfaceView(context);
        /* over the game's own surface (SDL's) */
        view.setZOrderMediaOverlay(true);
        view.getHolder().addCallback(this);
        view.setVisibility(View.GONE);
        /* a tap skips the movie (the overlay is hidden while it plays, so
           there would be no way out of it without a gamepad: the Xbox skips
           it on any button) */
        view.setOnTouchListener((touched, event) -> {
            if (event.getAction() == MotionEvent.ACTION_DOWN && player != null) {
                int done = movie;

                stopMovie();
                nativeFinished(done);
                return true;
            }
            return false;
        });
        layout.addView(view, new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        registerAssets();
        handler.postDelayed(this, 100);
    }

    /* where the disc's movies were extracted (bink/*.bik) */
    private File binkDirectory() {
        File root = context.getExternalFilesDir(null);
        return root != null ? new File(root, "bink") : null;
    }

    /* the movies the disc carries: what the native side may offer (a language
       with no file falls back to the one that is here) */
    private void registerAssets() {
        /* no decoder: nothing is offered, so the engine's own check skips every
           movie instead of asking for one this cannot play */
        if (!BinkPlayer.isAvailable()) return;

        File directory = binkDirectory();
        String[] files = directory != null ? directory.list() : null;
        if (files != null) {
            for (String name : files) nativeRegisterAsset(name);
        }
    }

    /* SurfaceHolder.Callback */
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        surfaceReady = true;
        nativeReady(true);
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        surfaceReady = false;
        nativeReady(false);
        stopMovie();
    }

    /** what the game asks for, and how far the player is, every 100 ms */
    @Override
    public void run() {
        int wanted = nativePoll();

        if (wanted == 0) {
            /* the game dropped the movie (it ended, or a button skipped it) */
            if (player != null) stopMovie();
        } else if (wanted != movie && !starting) {
            startMovie(wanted);
        }

        if (player != null) {
            nativeProgress(movie, player.positionMs(), player.durationMs());
            if (player.finished()) {
                int done = movie;
                stopMovie();
                nativeFinished(done);
            }
        }

        handler.postDelayed(this, 100);
    }

    private void startMovie(int wanted) {
        if (wanted <= 0) return;

        String asset = nativeAssetName(wanted);
        File directory = binkDirectory();
        if (asset == null || asset.isEmpty() || directory == null) return;

        File file = new File(directory, asset);
        if (!file.isFile()) return;

        /* the SurfaceView must be shown for its Surface to exist at all: it is
           made visible first and the player starts once it is (the poll below
           comes back in 100 ms, and the engine's own guard covers a failure) */
        if (view.getVisibility() != View.VISIBLE) {
            view.setVisibility(View.VISIBLE);
            if (overlay != null) overlay.setVisibility(View.INVISIBLE);
        }
        if (!surfaceReady) return;

        starting = true;
        try {
            BinkPlayer started = new BinkPlayer(file.getAbsolutePath(), view.getHolder().getSurface());
            started.start();
            player = started;
            movie = wanted;
            view.setVisibility(View.VISIBLE);
            if (overlay != null) overlay.setVisibility(View.INVISIBLE);
            nativeStarted(wanted);
        } catch (RuntimeException e) {
            stopMovie();
        } finally {
            starting = false;
        }
    }

    private void stopMovie() {
        if (player != null) {
            player.release();
            player = null;
        }
        movie = 0;
        view.setVisibility(View.GONE);
        if (overlay != null) overlay.setVisibility(View.VISIBLE);
    }
}
