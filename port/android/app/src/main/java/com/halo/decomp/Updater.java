package com.halo.decomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import android.util.TypedValue;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * The app's self-updater, as the desktop games' (port/linux/src/updater.c).
 *
 * A release's build (BuildConfig.HALO_RELEASE_BUILD: built from the
 * release's tag, v<version>, by ChupathingyCE's release workflow) knows its
 * version (BuildConfig.HALO_VERSION, 0.5.0b); nightlies and other builds
 * never look. When
 * update.auto in config.toml is true (the default), the game asks GitHub for
 * the latest release when it starts, on a thread of its own, and if it is
 * newer asks the player whether to update:
 *
 * - Yes: the release's list of its update files (updates.json, with its
 *   signature: UpdateManifest) names the app for this build
 *   (chupathingyce-android-release.zip or -debug.zip), which is downloaded,
 *   checked against the list's size and SHA-256, and handed to Android's
 *   package installer, which replaces the game (closing it) and offers to
 *   open the new version. A release from before the list has none: its zip
 *   is downloaded by that name, as before.
 * - No: nothing, until the next start.
 * - Do not ask again: after the player confirms it, update.auto = false is
 *   written to config.toml.
 *
 * The update channel (update.channel in config.toml: release, testing or
 * nightly; a build's default is its own channel, BuildConfig
 * HALO_UPDATE_CHANNEL) works as the desktop's (updater.c): Testing is also
 * offered the testing feed's build, Nightly the testing and nightly feeds',
 * the newest of them all, never one older than this build. A feed is used
 * only when its signature checks (UpdateManifest.checkFeed), so before
 * Android 13, which has no Ed25519 here, only releases are offered.
 *
 * Every release and nightly is signed with the same app key (the release
 * workflow's), which an app must keep for Android to install a new version
 * over it.
 */
final class Updater {
    private static final String REPOSITORY = "ChupathingyCE/chupathingyce";
    private static final String USER_AGENT = "ChupathingyCE/" + BuildConfig.HALO_VERSION + " (Android arm64)";
    /** the logcat tag, the native side's (port/android/host/host.h) */
    private static final String LOG_TAG = "chupathingyce";
    private static final int TIMEOUT_MILLISECONDS = 20000;
    /** the native 64-bit build's release asset (build.gradle) */
    static final String ASSET_64BIT = "chupathingyce-android64";
    /** the most a download (a release's zip, about 25 MB) or the app in it may be */
    private static final long MAXIMUM_UPDATE_SIZE = 256L * 1024 * 1024;
    /** the most the release's list of its update files, or a signature, may be */
    private static final long MAXIMUM_LIST_SIZE = 1024L * 1024;

    private Updater() {
    }

    /** the channels, from the most stable (update.channel) */
    private static final String[] CHANNELS = { "release", "testing", "nightly" };

    /** a feed's newer build: its channel, and its list, its signature checked */
    static final class Feed {
        final String channel, list;

        Feed(String channel, String list) {
            this.channel = channel;
            this.list = list;
        }
    }

    /** At the game's start: looks for a new version, in the background. */
    static void start(Activity activity) {
        File config = configFile(activity);

        if (!(BuildConfig.HALO_RELEASE_BUILD || !BuildConfig.HALO_UPDATE_CHANNEL.isEmpty()) || config == null
            || !autoUpdate(config))
            return;
        int chosen = chosenChannel(config);
        new Thread(() -> {
            String latest = latestRelease();
            Feed best = null;

            // (Testing and Nightly: each feed more stable than the chosen one too)
            for (int channel = 1; channel <= chosen; channel++) {
                Feed feed = feed(CHANNELS[channel]);

                if (feed == null)
                    continue;
                try {
                    String version = UpdateManifest.about(feed.list, feed.channel);

                    if (latest == null || newer(version, latest)) {
                        latest = version;
                        best = feed;
                    }
                } catch (UpdateManifest.Refused e) {
                    android.util.Log.i(LOG_TAG, "update: the " + feed.channel + " channel's list was not used: "
                        + e.getMessage());
                }
            }
            String found = latest;
            Feed from = best;

            if (found != null && newer(found, BuildConfig.HALO_VERSION))
                activity.runOnUiThread(() -> ask(activity, found, from));
        }, "update check").start();
    }

    /** update.channel's index in CHANNELS: config.toml's, else this build's own */
    static int chosenChannel(File config) {
        String chosen = configString(config, "update", "channel");

        if (chosen == null)
            chosen = BuildConfig.HALO_UPDATE_CHANNEL.isEmpty() ? "release" : BuildConfig.HALO_UPDATE_CHANNEL;
        for (int index = 0; index < CHANNELS.length; index++) {
            if (CHANNELS[index].equals(chosen))
                return index;
        }
        return 0;
    }

    /** a string setting of section's, or null when config.toml has none */
    private static String configString(File config, String wanted, String key) {
        String section = "";

        for (String line : readLines(config)) {
            String trimmed = line.trim();

            if (trimmed.startsWith("[") && trimmed.contains("]")) {
                section = trimmed.substring(1, trimmed.indexOf(']')).trim();
            } else if (section.equals(wanted) && isKey(trimmed, key)) {
                java.util.regex.Matcher value = java.util.regex.Pattern.compile("=\\s*\"([a-z]*)\"")
                    .matcher(trimmed);

                return value.find() ? value.group(1) : null;
            }
        }
        return null;
    }

    /**
     * A channel's feed (testing or nightly): its list, once its signature
     * checks; null if it is not there, not signed, or cannot be checked here.
     */
    private static Feed feed(String channel) {
        String base = "https://github.com/" + REPOSITORY + "/releases/download/" + channel + "/";

        try {
            byte[] document = fetch(base + "updates.json"), signature = fetch(base + "updates.json.sig");
            UpdateManifest.Check check = UpdateManifest.checkFeed(document,
                new String(signature, StandardCharsets.US_ASCII));

            if (!UpdateManifest.feedUsable(check)) {
                android.util.Log.i(LOG_TAG, "update: the " + channel + " channel's list was not used: "
                    + (check == UpdateManifest.Check.NOT_CHECKED ? "no Ed25519 before Android 13" : "not signed"));
                return null;
            }
            return new Feed(channel, new String(document, StandardCharsets.UTF_8));
        } catch (IOException e) {
            android.util.Log.i(LOG_TAG, "update: could not read the " + channel + " channel's list: " + e);
            return null;
        }
    }

    /** a small download (a list or a signature) into memory */
    private static byte[] fetch(String address) throws IOException {
        HttpURLConnection connection = open(address);

        try {
            if (connection.getResponseCode() != 200)
                throw new IOException("the server answered " + connection.getResponseCode());
            try (InputStream stream = connection.getInputStream()) {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buffer = new byte[16384];
                int count;

                while ((count = stream.read(buffer)) > 0) {
                    out.write(buffer, 0, count);
                    if (out.size() > MAXIMUM_LIST_SIZE)
                        throw new IOException("the download is larger than expected");
                }
                return out.toByteArray();
            }
        } finally {
            connection.disconnect();
        }
    }

    /* ---------- the open-source Vulkan driver for Adreno GPUs

    On an Adreno, the Turnip build (Mesa's Vulkan driver for Adreno) for its series is downloaded into the data folder when it
    is not there, from the project that releases it (port/android/README.md, "Graphics: OpenGL ES and Vulkan"). The download
    must have the size and SHA-256 of the build that was chosen, or it is dropped. It is only looked for when the player turned
    Turnip on (driverWanted), whatever update.auto says: the driver is not an update of the game. LauncherActivity does it
    before the game starts, with the download's progress on screen, so that the game finds the driver on its first start.

    Which archive is this phone's is decided once, from the GPU's name, and written to vk_driver_auto.txt in the data folder
    (the archive's name, or an empty line for a GPU that has none): display.vk_driver = "auto" is that archive
    (port/android/host/host_vk_driver.c), so that a player turns Turnip on with one setting. The default driver is the
    phone's own (display.vk_driver = ""). */

    static final class Driver {
        final String name, url, sha256, series;
        final long size;
        final int firstModel, lastModel;

        Driver(String name, String url, long size, String sha256, int firstModel, int lastModel, String series) {
            this.name = name;
            this.url = url;
            this.size = size;
            this.sha256 = sha256;
            this.firstModel = firstModel;
            this.lastModel = lastModel;
            this.series = series;
        }
    }

    private static final String RELEASE = "https://github.com/K11MCH1/AdrenoToolsDrivers/releases/download/v26.0.0-rc08/";
    private static final Driver[] DRIVERS = {
        // the build the Vulkan renderer was tested with (Adreno 750)
        new Driver("Turnip_v26.0.0_R8.zip", RELEASE + "Turnip_v26.0.0_R8.zip", 3478359,
            "e634db0f929e2205e95511c769071817d0390180ec72c8e690bc76375e813715", 600, 799, "6xx or 7xx"),
        // the same release's build for the A8xx series ("Turnip A8XX Draft"), not tried here
        new Driver("Turnip_v26.0.0_R8_A8xx.zip", RELEASE + "turnip_a8xx.zip", 2477437,
            "27e400a39cae06ac22695d74b837449868f712c679662c5793776bd575eca3bd", 800, 899, "8xx"),
    };
    private static final String AUTO_FILE = "vk_driver_auto.txt";

    /**
     * Where the archives and vk_driver_auto.txt live: the app's private storage (getFilesDir()/vk_driver), which no other
     * app can write, never the external files folder (on Android 9 and 10 any app with the storage permission could swap an
     * archive there). The game's driver loader (port/android/host/host_vk_driver.c) reads the same folder and checks the
     * archive's SHA-256 against the same table before it unpacks and loads it.
     */
    static File driverStore(android.content.Context context) {
        File store = new File(context.getFilesDir(), "vk_driver");

        store.mkdirs();
        return store;
    }

    private static Driver driverNamed(String name) {
        for (Driver driver : DRIVERS) {
            if (driver.name.equals(name))
                return driver;
        }
        return null;
    }

    /** whether the player asked for Turnip: the Vulkan renderer, over display.vk_driver = "auto" */
    static boolean driverWanted(File root) {
        if (root == null)
            return false;
        File config = new File(root, "config.toml");
        String renderer = readSetting(config, "display", "renderer");
        String driver = readSetting(config, "display", "vk_driver");

        return renderer != null && renderer.startsWith("\"vulkan\"") && driver != null && driver.startsWith("\"auto\"");
    }

    /** whether this phone's archive was decided and is in the store, whole and with its pinned hash (or it has none) */
    static boolean driverReady(File store) {
        if (store == null)
            return true;
        List<String> lines = readLines(new File(store, AUTO_FILE));

        if (lines.isEmpty())
            return false;
        Driver driver = driverNamed(lines.get(0).trim());

        return driver == null || driverPresent(store, driver);
    }

    /** the GPU's name, and this phone's archive (null for a GPU that has none) */
    static final class Choice {
        final String gpu;
        final Driver driver;

        Choice(String gpu, Driver driver) {
            this.gpu = gpu;
            this.driver = driver;
        }
    }

    /** decides this phone's archive from its GPU and records it in vk_driver_auto.txt; null if the GPU cannot be named */
    static Choice driverChoose(File store) {
        String renderer = glRenderer();
        int model = adrenoModel(renderer);
        Driver chosen = null;

        if (renderer.isEmpty()) {
            android.util.Log.i("halo", "driver: the GPU could not be named; nothing is downloaded this time");
            return null;
        }
        for (Driver driver : DRIVERS) {
            if (model >= driver.firstModel && model <= driver.lastModel)
                chosen = driver;
        }
        android.util.Log.i("halo", "driver: the GPU is \"" + renderer + "\": " + (chosen != null ? chosen.name
            : "no Turnip build for it; Vulkan runs on the phone's own driver"));
        File partial = new File(store, AUTO_FILE + ".tmp");
        try (OutputStream out = new FileOutputStream(partial)) {
            out.write(((chosen != null ? chosen.name : "") + "\n").getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            partial.delete();
            return new Choice(renderer, chosen);
        }
        if (!partial.renameTo(new File(store, AUTO_FILE)))
            partial.delete();
        return new Choice(renderer, chosen);
    }

    /** whether an archive is in the store, whole and with its pinned hash (a damaged or swapped one is removed) */
    static boolean driverPresent(File store, Driver driver) {
        File archive = new File(store, driver.name);

        if (!archive.isFile())
            return false;
        try {
            if (archive.length() == driver.size && driver.sha256.equals(sha256(archive)))
                return true;
        } catch (Exception e) {
            android.util.Log.i("halo", "driver: could not read " + archive + ": " + e);
        }
        android.util.Log.i("halo", "driver: " + driver.name + " is not the pinned build; downloading it again");
        archive.delete();
        return false;
    }

    /** the archive downloaded into root (as a .partial file, renamed once it is checked); throws why it could not be */
    static void driverDownload(File store, Driver driver, String gpu, Progress progress) throws Exception {
        File archive = new File(store, driver.name);
        File partial = new File(store, driver.name + ".partial");

        try {
            android.util.Log.i("halo", "driver: downloading " + driver.url + " for the " + gpu);
            download(driver.url, partial, driver.size, progress);
            if (partial.length() != driver.size || !driver.sha256.equals(sha256(partial)))
                throw new IOException("the download is not the build that was chosen (" + partial.length() + " bytes)");
            archive.delete();
            if (!partial.renameTo(archive))
                throw new IOException("it could not be renamed to " + archive);
            android.util.Log.i("halo", "driver: " + driver.name + " is in the app's driver folder");
        } catch (Exception e) {
            partial.delete();
            android.util.Log.i("halo", "driver: could not download " + driver.name + ": " + e);
            throw e;
        }
    }

    /** the number of an Adreno GPU from its OpenGL ES renderer string ("Adreno (TM) 750"), 0 for any other GPU */
    private static int adrenoModel(String renderer) {
        Matcher matcher = Pattern.compile("adreno\\D*(\\d{3})").matcher(renderer.toLowerCase(Locale.ROOT));

        return matcher.find() ? Integer.parseInt(matcher.group(1)) : 0;
    }

    /**
     * The GPU's OpenGL ES renderer string, from a context of its own on a 1x1 pbuffer ("" if none can be made). The display
     * is not terminated: the game's renderer shares it.
     */
    private static String glRenderer() {
        EGLDisplay display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        EGLContext context = EGL14.EGL_NO_CONTEXT;
        EGLSurface surface = EGL14.EGL_NO_SURFACE;
        int[] version = new int[2];

        if (display == EGL14.EGL_NO_DISPLAY || !EGL14.eglInitialize(display, version, 0, version, 1))
            return "";
        try {
            int[] attributes = { EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT, EGL14.EGL_SURFACE_TYPE,
                EGL14.EGL_PBUFFER_BIT, EGL14.EGL_NONE };
            EGLConfig[] configs = new EGLConfig[1];
            int[] count = new int[1];

            if (!EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0) || count[0] < 1)
                return "";
            context = EGL14.eglCreateContext(display, configs[0], EGL14.EGL_NO_CONTEXT,
                new int[] { EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE }, 0);
            surface = EGL14.eglCreatePbufferSurface(display, configs[0],
                new int[] { EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE }, 0);
            if (context == EGL14.EGL_NO_CONTEXT || surface == EGL14.EGL_NO_SURFACE
                || !EGL14.eglMakeCurrent(display, surface, surface, context))
                return "";
            String renderer = GLES20.glGetString(GLES20.GL_RENDERER);

            return renderer != null ? renderer : "";
        } finally {
            EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
            if (surface != EGL14.EGL_NO_SURFACE)
                EGL14.eglDestroySurface(display, surface);
            if (context != EGL14.EGL_NO_CONTEXT)
                EGL14.eglDestroyContext(display, context);
        }
    }

    private static String sha256(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        StringBuilder text = new StringBuilder();

        try (InputStream stream = new FileInputStream(file)) {
            byte[] buffer = new byte[65536];
            int count;

            while ((count = stream.read(buffer)) > 0)
                digest.update(buffer, 0, count);
        }
        for (byte b : digest.digest())
            text.append(String.format(Locale.ROOT, "%02x", b));
        return text.toString();
    }

    static File configFile(Activity activity) {
        File root = activity.getExternalFilesDir(null);

        return root != null ? new File(root, "config.toml") : null;
    }

    /* ---------- config.toml's update.auto */

    private static boolean autoUpdate(File config) {
        String value = readSetting(config, "update", "auto");

        return value == null || !value.startsWith("false");
    }

    /** update.auto = false written into config.toml (only its line changed) */
    static boolean writeAutoUpdateOff(File config) {
        return writeSetting(config, "update", "auto", "false");
    }

    /** a setting's value as config.toml has it (a string with its quotes), null when it is not there */
    static String readSetting(File config, String wantedSection, String key) {
        String section = "";

        for (String line : readLines(config)) {
            String trimmed = line.trim();

            if (trimmed.startsWith("[") && trimmed.contains("]")) {
                section = trimmed.substring(1, trimmed.indexOf(']')).trim();
            } else if (section.equals(wantedSection) && isKey(trimmed, key)) {
                return trimmed.substring(trimmed.indexOf('=') + 1).trim();
            }
        }
        return null;
    }

    /** key = value written into config.toml's section (only its line changed; added when it is not there) */
    static boolean writeSetting(File config, String wantedSection, String key, String value) {
        List<String> lines = readLines(config);
        List<String> out = new ArrayList<>();
        String section = "";
        boolean inSection = false, written = false;

        for (String line : lines) {
            String trimmed = line.trim();

            if (trimmed.startsWith("[") && trimmed.contains("]")) {
                if (inSection && !written) {
                    out.add(key + " = " + value);
                    written = true;
                }
                section = trimmed.substring(1, trimmed.indexOf(']')).trim();
                inSection = section.equals(wantedSection);
            } else if (inSection && !written && isKey(trimmed, key)) {
                out.add(key + " = " + value);
                written = true;
                continue;
            }
            out.add(line);
        }
        if (!written) {
            if (!inSection) {
                out.add("");
                out.add("[" + wantedSection + "]");
            }
            out.add(key + " = " + value);
        }
        StringBuilder text = new StringBuilder();
        for (String line : out)
            text.append(line).append('\n');
        // (as the game writes it, port_config.c's config_write_file: to a
        // new file, synced, then renamed over the old file, so a write cut
        // short leaves the old settings. Its own name, not the game's
        // config.toml.new, so the two never write the same temporary file)
        File temporary = new File(config.getPath() + ".new-launcher");
        try (FileOutputStream stream = new FileOutputStream(temporary)) {
            stream.write(text.toString().getBytes(StandardCharsets.UTF_8));
            stream.flush();
            stream.getFD().sync();
        } catch (IOException e) {
            temporary.delete();
            return false;
        }
        if (!temporary.renameTo(config)) {
            temporary.delete();
            return false;
        }
        return true;
    }

    private static boolean isKey(String trimmed, String key) {
        return trimmed.startsWith(key) && trimmed.substring(key.length()).trim().startsWith("=");
    }

    private static List<String> readLines(File file) {
        List<String> lines = new ArrayList<>();

        try (InputStream stream = new FileInputStream(file)) {
            String text = new String(readAll(stream), StandardCharsets.UTF_8);

            for (String line : text.split("\n", -1))
                lines.add(line.endsWith("\r") ? line.substring(0, line.length() - 1) : line);
            if (!lines.isEmpty() && lines.get(lines.size() - 1).isEmpty())
                lines.remove(lines.size() - 1);
        } catch (IOException e) {
            // no file yet: every setting at its default
        }
        return lines;
    }

    private static byte[] readAll(InputStream stream) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[16384];
        int count;

        while ((count = stream.read(buffer)) > 0)
            out.write(buffer, 0, count);
        return out.toByteArray();
    }

    /* ---------- GitHub */

    private static HttpURLConnection open(String address) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(address).openConnection();

        connection.setConnectTimeout(TIMEOUT_MILLISECONDS);
        connection.setReadTimeout(TIMEOUT_MILLISECONDS);
        connection.setRequestProperty("User-Agent", USER_AGENT);
        connection.setInstanceFollowRedirects(true);
        return connection;
    }

    /**
     * Whether version is newer than current (UpdateManifest.compare, as
     * updater.c's updater_newer): by the numbers, then the hotfix letter, a
     * version without one first, then a release before its test builds and
     * nightlies, which are in the order they were built.
     */
    static boolean newer(String version, String current) {
        return UpdateManifest.newer(version, current);
    }

    /** GitHub's latest release's version (its tag, without the v), null if there is none */
    private static String latestRelease() {
        try {
            HttpURLConnection connection = open("https://api.github.com/repos/" + REPOSITORY + "/releases/latest");

            connection.setRequestProperty("Accept", "application/vnd.github+json");
            try (InputStream stream = connection.getInputStream()) {
                String tag = new JSONObject(new String(readAll(stream), StandardCharsets.UTF_8)).optString("tag_name");

                // (a version goes into the download's address: letters, digits and . _ + - only;
                // and the latest release's is a release's, never a test build's or a nightly's)
                if (!tag.matches("v[0-9A-Za-z_+-][0-9A-Za-z._+-]{0,30}") || !UpdateManifest.release(tag.substring(1))) {
                    android.util.Log.i(LOG_TAG, "update: the latest release's tag " + tag + " is not used");
                    return null;
                }
                return tag.substring(1);
            } finally {
                connection.disconnect();
            }
        } catch (Exception e) {
            android.util.Log.i(LOG_TAG, "update: could not check for a new version: " + e);
            return null;
        }
    }

    /* ---------- the player's answer */

    private static void ask(Activity activity, String latest, Feed feed) {
        if (activity.isFinishing())
            return;
        new AlertDialog.Builder(activity)
            .setTitle("ChupathingyCE: new version")
            .setMessage("A new version of ChupathingyCE is out (" + latest + "; this is "
                + BuildConfig.HALO_VERSION + ").\n\nDo you want to update? The game will close and start "
                + "the new version.")
            .setCancelable(false)
            .setPositiveButton("Yes", (dialog, which) -> update(activity, latest, BuildConfig.HALO_UPDATE_ASSET, feed))
            .setNegativeButton("No", null)
            .setNeutralButton("Do not ask again", (dialog, which) -> confirmNever(activity))
            .show();
    }

    private static void confirmNever(Activity activity) {
        new AlertDialog.Builder(activity)
            .setTitle("ChupathingyCE: new version")
            .setMessage("Stop asking about new versions?\n\nTo ask again, set auto = true in the [update] section "
                + "of config.toml.")
            .setCancelable(false)
            .setPositiveButton("Yes", (dialog, which) -> {
                File config = configFile(activity);

                if (config != null)
                    writeAutoUpdateOff(config);
            })
            .setNegativeButton("No", null)
            .show();
    }

    /* ---------- updating */

    /** whether this is the native 64-bit build (build.gradle's native64) */
    static boolean native64() {
        return BuildConfig.HALO_UPDATE_ASSET.equals(ASSET_64BIT);
    }

    /**
     * This version's native 64-bit build installed over this one (the same
     * app, ID and key: the data and the saves stay), for a device whose
     * Java runtime holds the fixed addresses the usual build needs
     * (LauncherActivity). Release builds only: others have no release.
     */
    static void switchTo64Bit(Activity activity) {
        update(activity, BuildConfig.HALO_VERSION, ASSET_64BIT, null);
    }

    private static void update(Activity activity, String latest, String assetBase, Feed feed) {
        // (the native 64-bit build's is chupathingyce-android64-*: build.gradle)
        String configuration = BuildConfig.DEBUG ? "debug" : "release";
        String asset = assetBase + "-" + configuration + ".zip";
        File directory = new File(activity.getCacheDir(), UpdateProvider.DIRECTORY);
        LinearLayout layout = new LinearLayout(activity);
        TextView status = new TextView(activity);
        ProgressBar bar = new ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal);
        int padding = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 24,
            activity.getResources().getDisplayMetrics());

        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(padding, padding / 2, padding, 0);
        status.setText("Downloading version " + latest + "...");
        bar.setMax(1000);
        layout.addView(status);
        layout.addView(bar);
        AlertDialog progress = new AlertDialog.Builder(activity)
            .setTitle("ChupathingyCE: new version")
            .setView(layout)
            .setCancelable(false)
            .show();

        new Thread(() -> {
            try {
                directory.mkdirs();
                File zip = new File(directory, "update.zip");
                File apk = new File(directory, UpdateProvider.APK);

                // (the release path takes a release's version alone)
                if (feed == null && !UpdateManifest.release(latest))
                    throw new IOException("version " + latest + " is not a release's");
                UpdateManifest.Entry listed = feed != null ? feedFile(feed, latest, configuration, assetBase)
                    : listedFile(directory, latest, configuration, assetBase);
                String address = listed != null
                    ? "https://github.com/" + REPOSITORY + "/releases/download/" + listed.tag + "/" + listed.name
                    : "https://github.com/" + REPOSITORY + "/releases/download/v" + latest + "/" + asset;

                download(address, zip, listed != null ? listed.size : MAXIMUM_UPDATE_SIZE,
                    (received, total) -> activity.runOnUiThread(() -> {
                        bar.setProgress(total > 0 ? (int) (received * 1000 / total) : 0);
                        status.setText("Downloading version " + latest + "... (" + (received >> 20) + " of "
                            + (total >> 20) + " MB)");
                    }));
                if (listed != null)
                    checkListed(zip, listed);
                extractApk(zip, apk);
                zip.delete();
                activity.runOnUiThread(() -> {
                    progress.dismiss();
                    install(activity);
                });
            } catch (Exception e) {
                android.util.Log.i(LOG_TAG, "update: failed: " + e);
                activity.runOnUiThread(() -> {
                    progress.dismiss();
                    new AlertDialog.Builder(activity)
                        .setTitle("ChupathingyCE: new version")
                        .setMessage("The update failed:\n\n" + e.getMessage())
                        .setPositiveButton("OK", null)
                        .show();
                });
            }
        }, "update download").start();
    }

    interface Progress {
        void report(long received, long total);
    }

    /**
     * The release's list's file for this build (UpdateManifest), its
     * signature checked; null when the release has no list or no such file
     * in it (the zip by its old name, then).
     */
    private static UpdateManifest.Entry listedFile(File directory, String latest, String configuration,
            String assetBase) throws IOException {
        String base = "https://github.com/" + REPOSITORY + "/releases/download/v" + latest + "/";
        String platform = assetBase.substring("chupathingyce-".length());
        File list = new File(directory, "updates.json"), signature = new File(directory, "updates.json.sig");
        byte[] document, signatureText;

        try {
            download(base + "updates.json", list, MAXIMUM_LIST_SIZE, (received, total) -> { });
        } catch (IOException e) {
            android.util.Log.i(LOG_TAG, "update: no list of update files (" + e.getMessage() + ")");
            return null;
        }
        try {
            document = readFile(list);
            try {
                download(base + "updates.json.sig", signature, MAXIMUM_LIST_SIZE, (received, total) -> { });
            } catch (IOException e) {
                throw new IOException("the release's list of update files has no signature (" + e.getMessage() + ")");
            }
            signatureText = readFile(signature);
        } finally {
            list.delete();
            signature.delete();
        }
        UpdateManifest.Check check = UpdateManifest.check(document,
            new String(signatureText, StandardCharsets.US_ASCII), UpdateManifest.KEYS);
        if (check == UpdateManifest.Check.NOT_SIGNED)
            throw new IOException("the release's list of update files does not match the release key");
        if (check == UpdateManifest.Check.NOT_CHECKED)
            android.util.Log.i(LOG_TAG, "update: no Ed25519 before Android 13: the list's signature is not checked");
        try {
            UpdateManifest.Entry listed = UpdateManifest.find(new String(document, StandardCharsets.UTF_8), latest,
                platform, configuration, "zip");

            if (listed == null)
                android.util.Log.i(LOG_TAG, "update: the release's list has no " + platform + " " + configuration);
            return listed;
        } catch (UpdateManifest.Refused e) {
            throw new IOException("the release's list of update files was not used: " + e.getMessage());
        }
    }

    /**
     * A feed's file for this build, from the list checked when it was found;
     * a feed's build has no zip by an older name, so none is an error.
     */
    private static UpdateManifest.Entry feedFile(Feed feed, String latest, String configuration, String assetBase)
            throws IOException {
        String platform = assetBase.substring("chupathingyce-".length());

        try {
            UpdateManifest.Entry listed = UpdateManifest.findChannel(feed.list, feed.channel, latest, platform,
                configuration, "zip");

            if (listed == null)
                throw new IOException("the " + feed.channel + " channel's list has no " + platform + " " + configuration);
            return listed;
        } catch (UpdateManifest.Refused e) {
            throw new IOException("the " + feed.channel + " channel's list was not used: " + e.getMessage());
        }
    }

    /** the download, checked against the list's size and SHA-256 */
    private static void checkListed(File zip, UpdateManifest.Entry listed) throws IOException {
        if (zip.length() != listed.size)
            throw new IOException("the download is " + zip.length() + " bytes, not the list's " + listed.size);
        try (InputStream stream = new FileInputStream(zip)) {
            java.security.MessageDigest digest = java.security.MessageDigest.getInstance("SHA-256");
            byte[] buffer = new byte[65536];
            int count;

            while ((count = stream.read(buffer)) > 0)
                digest.update(buffer, 0, count);
            if (!UpdateManifest.toHex(digest.digest()).equals(listed.sha256))
                throw new IOException("the download's SHA-256 is not the list's");
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IOException("no SHA-256 to check the download with");
        }
    }

    private static byte[] readFile(File file) throws IOException {
        try (InputStream stream = new FileInputStream(file)) {
            return readAll(stream);
        }
    }

    private static void download(String address, File file, long maximum, Progress progress) throws IOException {
        HttpURLConnection connection = open(address);

        try {
            int status = connection.getResponseCode();

            if (status != 200)
                throw new IOException("the server answered " + status);
            long total = connection.getContentLengthLong();
            long received = 0, reported = 0;
            byte[] buffer = new byte[65536];

            try (InputStream in = connection.getInputStream(); OutputStream out = new FileOutputStream(file)) {
                int count;

                while ((count = in.read(buffer)) > 0) {
                    received += count;
                    if (received > maximum)
                        throw new IOException("the download is larger than expected");
                    out.write(buffer, 0, count);
                    if (received - reported >= 256 * 1024 || received == total) {
                        progress.report(received, total);
                        reported = received;
                    }
                }
            }
            if (total > 0 && received != total)
                throw new IOException("the download broke off");
        } finally {
            connection.disconnect();
        }
    }

    /** the zip's app (its one .apk) to apk */
    private static void extractApk(File zip, File apk) throws IOException {
        try (ZipInputStream in = new ZipInputStream(new FileInputStream(zip))) {
            ZipEntry entry;

            while ((entry = in.getNextEntry()) != null) {
                if (entry.isDirectory() || !entry.getName().endsWith(".apk"))
                    continue;
                try (OutputStream out = new FileOutputStream(apk)) {
                    byte[] buffer = new byte[65536];
                    long written = 0;
                    int count;

                    while ((count = in.read(buffer)) > 0) {
                        written += count;
                        if (written > MAXIMUM_UPDATE_SIZE)
                            throw new IOException("the app in the download is larger than expected");
                        out.write(buffer, 0, count);
                    }
                }
                return;
            }
        }
        throw new IOException("the download has no app in it");
    }

    /**
     * Android's package installer, with the new app: it asks the player to
     * allow this app to install apps the first time, replaces the game
     * (closing it) and offers to open the new version.
     */
    private static void install(Activity activity) {
        Intent intent = new Intent(Intent.ACTION_VIEW);

        intent.setDataAndType(Uri.parse("content://" + UpdateProvider.AUTHORITY + "/" + UpdateProvider.APK),
            "application/vnd.android.package-archive");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
        activity.startActivity(intent);
    }
}
