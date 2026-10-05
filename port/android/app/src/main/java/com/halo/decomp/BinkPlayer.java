package com.halo.decomp;

import android.view.Surface;

/**
 * A Bink movie, decoded by the open-source decoder in libbinkplayer.so
 * (FFmpeg's Bink demuxer and decoders).
 *
 * The port has no Bink SDK (it is proprietary) and the device's own player
 * cannot read a .bik, so this draws the decoded frames straight to a Surface
 * and its sound to the device's output. The file comes from the disc the
 * player extracted (its bink folder), so nothing of the game is packaged with
 * the app.
 */
final class BinkPlayer {
    static {
        System.loadLibrary("binkplayer");
    }

    private long handle;

    BinkPlayer(String path, Surface surface) {
        handle = nativeOpen(path, surface);
        if (handle == 0)
            throw new IllegalStateException("the movie could not be opened: " + path);
    }

    void start() {
        if (handle != 0) nativeStart(handle);
    }

    int positionMs() {
        return handle != 0 ? nativePositionMs(handle) : 0;
    }

    int durationMs() {
        return handle != 0 ? nativeDurationMs(handle) : 0;
    }

    boolean finished() {
        return handle != 0 && nativeFinished(handle);
    }

    void release() {
        if (handle != 0) {
            nativeClose(handle);
            handle = 0;
        }
    }

    private static native long nativeOpen(String path, Surface surface);
    private static native void nativeStart(long handle);
    private static native int nativePositionMs(long handle);
    private static native int nativeDurationMs(long handle);
    private static native boolean nativeFinished(long handle);
    private static native void nativeClose(long handle);
}
