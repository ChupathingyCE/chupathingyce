package com.halo.decomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
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
import java.util.ArrayList;
import java.util.List;
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
 * - Yes: the release's app (chupathingyce-android-release.zip or -debug.zip) is
 *   downloaded and handed to Android's package installer, which replaces the
 *   game (closing it) and offers to open the new version.
 * - No: nothing, until the next start.
 * - Do not ask again: after the player confirms it, update.auto = false is
 *   written to config.toml.
 *
 * Every release and nightly is signed with the same key (the release
 * workflow's), which an app must keep for Android to install a new version
 * over it.
 */
final class Updater {
    private static final String REPOSITORY = "ChupathingyCE/chupathingyce";
    private static final String USER_AGENT = "chupathingyce-updater";
    private static final int TIMEOUT_MILLISECONDS = 20000;
    /** the most a download (a release's zip, about 25 MB) or the app in it may be */
    private static final long MAXIMUM_UPDATE_SIZE = 256L * 1024 * 1024;

    private Updater() {
    }

    /** At the game's start: looks for a new version, in the background. */
    static void start(Activity activity) {
        File config = configFile(activity);

        if (!BuildConfig.HALO_RELEASE_BUILD || config == null || !autoUpdate(config))
            return;
        new Thread(() -> {
            String latest = latestRelease();

            if (latest != null && newer(latest, BuildConfig.HALO_VERSION))
                activity.runOnUiThread(() -> ask(activity, latest));
        }, "update check").start();
    }

    static File configFile(Activity activity) {
        File root = activity.getExternalFilesDir(null);

        return root != null ? new File(root, "config.toml") : null;
    }

    /* ---------- config.toml's update.auto */

    private static boolean autoUpdate(File config) {
        String section = "";

        for (String line : readLines(config)) {
            String trimmed = line.trim();

            if (trimmed.startsWith("[") && trimmed.contains("]")) {
                section = trimmed.substring(1, trimmed.indexOf(']')).trim();
            } else if (section.equals("update") && isKey(trimmed, "auto")) {
                return !trimmed.substring(trimmed.indexOf('=') + 1).trim().startsWith("false");
            }
        }
        return true;
    }

    /** update.auto = false written into config.toml (only its line changed) */
    static boolean writeAutoUpdateOff(File config) {
        List<String> lines = readLines(config);
        List<String> out = new ArrayList<>();
        String section = "";
        boolean inSection = false, written = false;

        for (String line : lines) {
            String trimmed = line.trim();

            if (trimmed.startsWith("[") && trimmed.contains("]")) {
                if (inSection && !written) {
                    out.add("auto = false");
                    written = true;
                }
                section = trimmed.substring(1, trimmed.indexOf(']')).trim();
                inSection = section.equals("update");
            } else if (inSection && !written && isKey(trimmed, "auto")) {
                out.add("auto = false");
                written = true;
                continue;
            }
            out.add(line);
        }
        if (!written) {
            if (!inSection) {
                out.add("");
                out.add("[update]");
            }
            out.add("auto = false");
        }
        StringBuilder text = new StringBuilder();
        for (String line : out)
            text.append(line).append('\n');
        try (OutputStream stream = new FileOutputStream(config)) {
            stream.write(text.toString().getBytes(StandardCharsets.UTF_8));
            return true;
        } catch (IOException e) {
            return false;
        }
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
     * Whether version is newer than current: [v]<major>.<minor>.<patch> and a
     * pre-release suffix (0.5.0b, 1.0.0rc1) or none, by the numbers, then a
     * version without a suffix after the ones with, and suffixes in order (as
     * updater.c's updater_newer).
     */
    static boolean newer(String version, String current) {
        java.util.regex.Pattern pattern = java.util.regex.Pattern.compile("v?(\\d+)\\.(\\d+)\\.(\\d+)(.*)");
        java.util.regex.Matcher latest = pattern.matcher(version), now = pattern.matcher(current);

        if (!latest.matches() || !now.matches())
            return false;
        for (int part = 1; part <= 3; part++) {
            long a = Long.parseLong(latest.group(part)), b = Long.parseLong(now.group(part));

            if (a != b)
                return a > b;
        }
        String latestSuffix = latest.group(4), currentSuffix = now.group(4);

        if (latestSuffix.isEmpty() || currentSuffix.isEmpty())
            return latestSuffix.isEmpty() && !currentSuffix.isEmpty();
        return latestSuffix.compareTo(currentSuffix) > 0;
    }

    /** GitHub's latest release's version (its tag, without the v), null if there is none */
    private static String latestRelease() {
        try {
            HttpURLConnection connection = open("https://api.github.com/repos/" + REPOSITORY + "/releases/latest");

            connection.setRequestProperty("Accept", "application/vnd.github+json");
            try (InputStream stream = connection.getInputStream()) {
                String tag = new JSONObject(new String(readAll(stream), StandardCharsets.UTF_8)).optString("tag_name");

                // (a version goes into the download's address: letters, digits and . _ + - only)
                return tag.matches("v[0-9A-Za-z_+-][0-9A-Za-z._+-]{0,30}") ? tag.substring(1) : null;
            } finally {
                connection.disconnect();
            }
        } catch (Exception e) {
            android.util.Log.i("halo", "update: could not check for a new version: " + e);
            return null;
        }
    }

    /* ---------- the player's answer */

    private static void ask(Activity activity, String latest) {
        if (activity.isFinishing())
            return;
        new AlertDialog.Builder(activity)
            .setTitle("ChupathingyCE: new version")
            .setMessage("A new version of ChupathingyCE is out (" + latest + "; this is "
                + BuildConfig.HALO_VERSION + ").\n\nDo you want to update? The game will close and start "
                + "the new version.")
            .setCancelable(false)
            .setPositiveButton("Yes", (dialog, which) -> update(activity, latest))
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

    private static void update(Activity activity, String latest) {
        String asset = "chupathingyce-android-" + (BuildConfig.DEBUG ? "debug" : "release") + ".zip";
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

                download("https://github.com/" + REPOSITORY + "/releases/download/v" + latest + "/" + asset, zip,
                    (received, total) -> activity.runOnUiThread(() -> {
                        bar.setProgress(total > 0 ? (int) (received * 1000 / total) : 0);
                        status.setText("Downloading version " + latest + "... (" + (received >> 20) + " of "
                            + (total >> 20) + " MB)");
                    }));
                extractApk(zip, apk);
                zip.delete();
                activity.runOnUiThread(() -> {
                    progress.dismiss();
                    install(activity);
                });
            } catch (Exception e) {
                android.util.Log.i("halo", "update: failed: " + e);
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

    private interface Progress {
        void report(long received, long total);
    }

    private static void download(String address, File file, Progress progress) throws IOException {
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
                    if (received > MAXIMUM_UPDATE_SIZE)
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
