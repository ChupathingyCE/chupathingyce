package com.halo.decomp;

import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.ViewGroup;
import android.widget.Toast;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import android.graphics.Insets;
import android.os.Build;
import android.view.Display;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLControllerManager;

/**
 * The game: SDL3's activity, running libmain.so (port/android/host), which
 * loads the game image from the APK's assets.
 */
public class HaloActivity extends SDLActivity {
    /** lets system link's broadcasts in over Wi-Fi while the game runs */
    private WifiManager.MulticastLock multicastLock;
    /**
     * the latest system gesture insets {left, top, right, bottom}, in pixels;
     * written on the UI thread, read by the game's thread: replaced as a
     * whole, never changed in place
     */
    private volatile int[] gestureInsets = new int[] { 0, 0, 0, 0 };
    /** the on-screen touch controls, over SDL's surface */
    private TouchControls touchControls;
    private static final int EXPORT_LAYOUT = 401, IMPORT_LAYOUT = 402;
    private String pendingLayoutExport;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (savedInstanceState != null)
            pendingLayoutExport = savedInstanceState.getString("pending-layout-export");
        if (mLayout != null) {
            touchControls = new TouchControls(this);
            mLayout.addView(touchControls, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        hideSystemBars();
        preferHighestRefreshRate();
        trackGestureInsets();
        acquireMulticastLock();
        // a new version looked for while the game starts
        Updater.start(this);
    }

    /** the system's file picker, for the touch layout; no storage permission */
    public void chooseLayoutFile(boolean export, String configuration) {
        new AlertDialog.Builder(this).setTitle(export ? "Export layout" : "Import layout")
            .setMessage(export ? "Choose the folder and file name for your touch layout."
                : "Choose an exported touch layout. It replaces your buttons, sensitivity and General settings.")
            .setNegativeButton("Cancel", null)
            .setPositiveButton(export ? "Choose location" : "Choose file", (dialog, which) -> {
                Intent intent = new Intent(export ? Intent.ACTION_CREATE_DOCUMENT : Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType(export ? "text/plain" : "*/*");
                if (export) {
                    pendingLayoutExport = configuration;
                    intent.putExtra(Intent.EXTRA_TITLE, "halo-touch-layout.halolayout");
                }
                try { startActivityForResult(intent, export ? EXPORT_LAYOUT : IMPORT_LAYOUT); }
                catch (android.content.ActivityNotFoundException e) {
                    pendingLayoutExport = null;
                    Toast.makeText(this, "No document picker is available.", Toast.LENGTH_LONG).show();
                }
            }).show();
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putString("pending-layout-export", pendingLayoutExport);
    }

    /** Halo PC maps from the system's file picker (MapImporter); no storage permission */
    public void chooseMapFiles() {
        MapImporter.choose(this);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        if (request == MapImporter.REQUEST_CODE) {
            MapImporter.handleResult(this, result, data, null); return;
        }
        if (request != EXPORT_LAYOUT && request != IMPORT_LAYOUT) {
            super.onActivityResult(request, result, data); return;
        }
        String exported = pendingLayoutExport; pendingLayoutExport = null;
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri document = data.getData();
        new Thread(() -> {
            try {
                if (request == EXPORT_LAYOUT) {
                    if (exported == null) throw new java.io.IOException("Layout snapshot is unavailable");
                    try (OutputStream output = getContentResolver().openOutputStream(document, "wt")) {
                        if (output == null) throw new java.io.IOException("Cannot open destination");
                        output.write(exported.getBytes(StandardCharsets.UTF_8));
                    }
                    runOnUiThread(() -> Toast.makeText(this, "Layout exported.", Toast.LENGTH_SHORT).show());
                } else {
                    ByteArrayOutputStream contents = new ByteArrayOutputStream();
                    try (InputStream input = getContentResolver().openInputStream(document)) {
                        if (input == null) throw new java.io.IOException("Cannot open layout file");
                        byte[] buffer = new byte[4096]; int size;
                        while ((size = input.read(buffer)) != -1) {
                            if (contents.size()+size > 65536) throw new java.io.IOException("Layout file is too large");
                            contents.write(buffer, 0, size);
                        }
                    }
                    String configuration = new String(contents.toByteArray(), StandardCharsets.UTF_8);
                    // checked away from the UI thread, then applied to the overlay at once
                    TouchLayout.importConfiguration(configuration);
                    runOnUiThread(() -> {
                        if (isFinishing() || isDestroyed() || touchControls == null) return;
                        try {
                            touchControls.importLayout(configuration);
                            Toast.makeText(this, "Layout imported and saved.", Toast.LENGTH_SHORT).show();
                        } catch (IllegalArgumentException e) { layoutFileError(e); }
                    });
                }
            } catch (Exception e) { runOnUiThread(() -> layoutFileError(e)); }
        }, "halo-touch-layout-file").start();
    }

    private void layoutFileError(Exception error) {
        if (isFinishing() || isDestroyed()) return;
        new AlertDialog.Builder(this).setTitle("Layout file")
            .setMessage("Could not complete the operation: "+error.getMessage())
            .setPositiveButton("OK", null).show();
    }

    /**
     * the system back key (the button, or the back gesture) to the game, as
     * the key SDL names AC_BACK, which the menus take as B (xinput_sdl.c).
     * SDL hears keys only through its surface's key listener, so only while
     * the surface has the focus; when it did not (after the touch controls'
     * panels or a dialog took it), the key went to the activity, whose back
     * SDL traps (SDL_ANDROID_TRAP_BACK_BUTTON), and nothing happened. A
     * controller's back button and a mouse's back button go on as before
     * (SDL's joystick and its mouse handling).
     */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK &&
            !SDLControllerManager.isDeviceSDLJoystick(event.getDeviceId()) &&
            (event.getSource() & InputDevice.SOURCE_MOUSE) != InputDevice.SOURCE_MOUSE) {
            if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0)
                SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
            else if (event.getAction() == KeyEvent.ACTION_UP)
                SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override protected void onResume() {
        super.onResume();
        hideSystemBars();
        if (touchControls != null && getWindow().getDecorView().hasWindowFocus())
            touchControls.startDeviceInput();
    }

    @Override
    protected void onPause() {
        if (touchControls != null) touchControls.stopDeviceInput();
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        if (touchControls != null) {
            if (hasFocus) touchControls.startDeviceInput();
            else touchControls.stopDeviceInput();
        }
        super.onWindowFocusChanged(hasFocus);
        // (a dialog, the Options screen or the notification shade brings
        // the bars back with them)
        if (hasFocus) hideSystemBars();
    }

    /**
     * full screen, the status and navigation bars hidden until a swipe from
     * an edge shows them for a moment. SDL hides them only with the flags
     * that Android 11 replaced, and only when the game asks, which left the
     * status bar over the game on some phones, or back after a dialog.
     */
    @SuppressWarnings("deprecation") // the flags are the only way before Android 11
    private void hideSystemBars() {
        android.view.Window window = getWindow();
        WindowManager.LayoutParams attributes = window.getAttributes();
        int cutout = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R ?
            WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS :
            WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;

        if (attributes.layoutInDisplayCutoutMode != cutout) {
            attributes.layoutInDisplayCutoutMode = cutout;
            window.setAttributes(attributes);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
            }
        } else {
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
            window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    @Override
    protected void onDestroy() {
        if (touchControls != null) touchControls.stopDeviceInput();
        if (multicastLock != null && multicastLock.isHeld())
            multicastLock.release();
        multicastLock = null;
        super.onDestroy();
    }

    /**
     * keeps gestureInsets current; Android sends the insets again when the
     * activity rotates (after the game's native code has started on a phone
     * launched from portrait), so a single read at startup would keep the
     * portrait values; the listener hands the insets on so SDL's own
     * handling still sees them
     */
    private void trackGestureInsets() {
        getWindow().getDecorView().setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View view, WindowInsets insets) {
                gestureInsets = readGestureInsets(insets);
                return view.onApplyWindowInsets(insets);
            }
        });
    }

    @SuppressWarnings("deprecation") // getSystemGestureInsets is the only call on Android 10
    private static int[] readGestureInsets(WindowInsets insets) {
        Insets gesture;

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
            gesture = insets.getInsets(WindowInsets.Type.systemGestures());
        else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
            gesture = insets.getSystemGestureInsets();
        else
            return new int[] { 0, 0, 0, 0 };
        return new int[] { gesture.left, gesture.top, gesture.right, gesture.bottom };
    }

    /**
     * the edges of the screen where Android keeps its gestures; in sticky
     * full screen the first swipe from an edge only shows the system bars,
     * and Android hands that swipe to the game as an ordinary finger, so
     * the game (port/linux/src/touch_input.c) must ignore touches that
     * begin there; returns a copy of {left, top, right, bottom} in pixels:
     * all 0 before Android 10 (which has no insets) and until the first
     * insets arrive. The game's native code calls this through JNI by name
     * and signature (host_main.c GetMethodID(...,
     * "getSystemGestureInsetsPixels", "()[I")), at every finger down, so
     * it must not be renamed, retyped or removed as unused
     */
    public int[] getSystemGestureInsetsPixels() {
        return gestureInsets.clone();
    }

    /**
     * Many phones drop the Wi-Fi's broadcast and multicast datagrams to
     * save power unless an app holds this: without it they would not see
     * system link games on the local network, nor be seen hosting one.
     */
    private void acquireMulticastLock() {
        try {
            WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wifi == null)
                return;
            multicastLock = wifi.createMulticastLock("halo-system-link");
            multicastLock.setReferenceCounted(false);
            multicastLock.acquire();
        } catch (RuntimeException e) {
            // (no Wi-Fi, or not allowed: the local network may miss games)
            multicastLock = null;
        }
    }

    /**
     * The game draws a frame at every display refresh, between its 30 Hz
     * ticks (port/linux/game/render_interpolation.c); Android otherwise
     * often keeps an app at 60 Hz on a faster display.
     */
    private void preferHighestRefreshRate() {
        Display display = getWindowManager().getDefaultDisplay();
        Display.Mode current = display.getMode();
        Display.Mode best = current;

        for (Display.Mode mode : display.getSupportedModes()) {
            if (mode.getPhysicalWidth() == current.getPhysicalWidth() &&
                mode.getPhysicalHeight() == current.getPhysicalHeight() &&
                mode.getRefreshRate() > best.getRefreshRate()) {
                best = mode;
            }
        }
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.preferredDisplayModeId = best.getModeId();
        getWindow().setAttributes(attributes);
    }
}
