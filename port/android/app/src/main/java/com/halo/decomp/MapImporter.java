package com.halo.decomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.widget.TextView;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Imports Halo PC maps the player picks with the system's file picker (no
 * storage permission): .map files, or .zip archives of them. On recent
 * Android versions file managers cannot open the app's folder, so this is
 * how maps get in without a computer.
 *
 * Each map goes in its family's folder beside maps/ (map_families.c), by its
 * header: a Custom Edition map (cache version 609) in maps_ce; a Halo PC
 * 1.0 map (version 7) in maps_md if it is named as HaloMD's are
 * (<name>_<number>.map), else in maps_pc. Custom Edition's resource maps
 * (bitmaps.map, sounds.map, loc.map) go in maps_ce. Xbox maps (version 5)
 * are left out: they come from the disc image. A map replaces a file of the
 * same name in its folder; it is written under another name first and
 * renamed once whole, so the game never reads a half-copied map.
 */
final class MapImporter {
    static final int REQUEST_CODE = 403;

    private static final int CACHE_SIGNATURE = 0x68656164; // 'head'
    private static final int XBOX_VERSION = 5;
    private static final int HALO_PC_VERSION = 7;
    private static final int CUSTOM_EDITION_VERSION = 609;
    /** a map file, at most (the largest community maps are a few hundred MB) */
    private static final long MAXIMUM_MAP_SIZE = 2048L << 20;

    private MapImporter() {
    }

    /** the system's file picker, for one or more .map or .zip files */
    static boolean choose(Activity activity) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // (maps have no MIME type of their own: any file, checked when read)
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        try {
            activity.startActivityForResult(intent, REQUEST_CODE);
            return true;
        } catch (android.content.ActivityNotFoundException e) {
            new AlertDialog.Builder(activity).setTitle("Import maps")
                .setMessage("No document picker is available.").setPositiveButton("OK", null).show();
            return false;
        }
    }

    /**
     * The picker's result: copies the files away from the UI thread, showing
     * what it is doing, then what it did; then runs done (on the UI thread,
     * also when nothing was picked).
     */
    static void handleResult(Activity activity, int resultCode, Intent data, Runnable done) {
        List<Uri> picked = new ArrayList<>();
        if (resultCode == Activity.RESULT_OK && data != null) {
            ClipData clip = data.getClipData();
            if (clip != null) {
                for (int i = 0; i < clip.getItemCount(); i++)
                    if (clip.getItemAt(i).getUri() != null)
                        picked.add(clip.getItemAt(i).getUri());
            } else if (data.getData() != null) {
                picked.add(data.getData());
            }
        }
        File dataRoot = activity.getExternalFilesDir(null);
        if (picked.isEmpty() || dataRoot == null) {
            if (done != null)
                done.run();
            return;
        }

        TextView text = new TextView(activity);
        int padding = (int) (24 * activity.getResources().getDisplayMetrics().density);
        text.setPadding(padding, padding / 2, padding, 0);
        text.setText("Starting...");
        AlertDialog progress = new AlertDialog.Builder(activity).setTitle("Importing maps")
            .setView(text).setCancelable(false).show();
        Handler handler = new Handler(Looper.getMainLooper());

        new Thread(() -> {
            Report report = new Report();
            for (Uri uri : picked)
                importDocument(activity, uri, dataRoot, report,
                    status -> handler.post(() -> text.setText(status)));
            handler.post(() -> {
                if (activity.isFinishing() || activity.isDestroyed())
                    return;
                progress.dismiss();
                new AlertDialog.Builder(activity).setTitle("Import maps").setMessage(report.summary())
                    .setPositiveButton("OK", null)
                    .setOnDismissListener(d -> { if (done != null) done.run(); }).show();
            });
        }, "halo-map-import").start();
    }

    interface Status {
        void show(String text);
    }

    /** what an import did, for the player */
    private static final class Report {
        final List<String> imported = new ArrayList<>();
        final List<String> skipped = new ArrayList<>();

        String summary() {
            StringBuilder text = new StringBuilder();
            if (imported.isEmpty())
                text.append("No maps were imported.");
            else
                text.append("Imported ").append(imported.size()).append(imported.size() == 1 ? " map:\n" : " maps:\n")
                    .append(String.join("\n", imported))
                    .append("\n\nThey are listed in the multiplayer map list the next time it opens.");
            if (!skipped.isEmpty())
                text.append("\n\nLeft out:\n").append(String.join("\n", skipped));
            return text.toString();
        }
    }

    private static String displayName(Activity activity, Uri uri) {
        try (Cursor cursor = activity.getContentResolver().query(uri,
                new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0))
                return cursor.getString(0);
        } catch (RuntimeException e) {
            // (named by its address below)
        }
        String path = uri.getLastPathSegment();
        return path != null ? path : "file";
    }

    /** a file's name without any folders before it */
    private static String baseName(String name) {
        int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        return slash >= 0 ? name.substring(slash + 1) : name;
    }

    private static boolean endsWith(String name, String ending) {
        return name.toLowerCase(Locale.ROOT).endsWith(ending);
    }

    private static void importDocument(Activity activity, Uri uri, File dataRoot, Report report, Status status) {
        String name = baseName(displayName(activity, uri));
        try (InputStream input = activity.getContentResolver().openInputStream(uri)) {
            if (input == null)
                throw new IOException("it could not be opened");
            if (endsWith(name, ".zip")) {
                int found = 0;
                try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(input))) {
                    for (ZipEntry entry; (entry = zip.getNextEntry()) != null; ) {
                        String entryName = baseName(entry.getName());
                        if (entry.isDirectory() || !endsWith(entryName, ".map"))
                            continue;
                        found++;
                        try {
                            importMap(zip, entryName, dataRoot, report, status, name + ": ");
                        } catch (IOException e) {
                            report.skipped.add(name + ": " + entryName + ": " + e.getMessage());
                        }
                    }
                }
                if (found == 0)
                    report.skipped.add(name + ": no .map files in it");
            } else if (endsWith(name, ".map")) {
                importMap(input, name, dataRoot, report, status, "");
            } else {
                report.skipped.add(name + ": not a .map or .zip file");
            }
        } catch (IOException | RuntimeException e) {
            report.skipped.add(name + ": " + e.getMessage());
        }
    }

    private static int littleEndian(byte[] bytes, int offset) {
        return (bytes[offset] & 0xFF) | (bytes[offset + 1] & 0xFF) << 8 | (bytes[offset + 2] & 0xFF) << 16
            | (bytes[offset + 3] & 0xFF) << 24;
    }

    private static int readFully(InputStream input, byte[] buffer) throws IOException {
        int total = 0;
        for (int size; total < buffer.length && (size = input.read(buffer, total, buffer.length - total)) > 0; )
            total += size;
        return total;
    }

    /** the folder a map belongs in, by its name and header; null to leave it out */
    private static String folderFor(String name, byte[] header, int headerSize) {
        String lower = name.toLowerCase(Locale.ROOT);
        if (lower.equals("bitmaps.map") || lower.equals("sounds.map") || lower.equals("loc.map"))
            return "maps_ce";
        if (headerSize < 8 || littleEndian(header, 0) != CACHE_SIGNATURE)
            return null;
        int version = littleEndian(header, 4);
        if (version == CUSTOM_EDITION_VERSION)
            return "maps_ce";
        if (version == HALO_PC_VERSION)
            return lower.matches(".*_[0-9]+\\.map") ? "maps_md" : "maps_pc";
        return null;
    }

    private static void importMap(InputStream input, String name, File dataRoot, Report report, Status status,
            String from) throws IOException {
        byte[] header = new byte[8];
        int headerSize = readFully(input, header);
        String folder = folderFor(name, header, headerSize);
        if (folder == null) {
            boolean xbox = headerSize >= 8 && littleEndian(header, 0) == CACHE_SIGNATURE
                && littleEndian(header, 4) == XBOX_VERSION;
            report.skipped.add(from + name + (xbox ? ": an Xbox map (those come from the disc image)"
                : ": not a Halo PC map"));
            return;
        }

        File directory = new File(dataRoot, folder);
        if (!directory.isDirectory() && !directory.mkdirs())
            throw new IOException("could not make " + folder);
        File target = new File(directory, name);
        File partial = new File(directory, name + ".part");
        long written = headerSize;
        status.show("Copying " + name + " to " + folder);
        try (OutputStream output = new FileOutputStream(partial)) {
            output.write(header, 0, headerSize);
            byte[] buffer = new byte[1 << 16];
            long reported = 0;
            for (int size; (size = input.read(buffer)) > 0; ) {
                output.write(buffer, 0, size);
                written += size;
                if (written > MAXIMUM_MAP_SIZE)
                    throw new IOException("larger than any map");
                if (written - reported >= (8 << 20)) {
                    reported = written;
                    status.show("Copying " + name + " to " + folder + " (" + (written >> 20) + " MB)");
                }
            }
        } catch (IOException e) {
            partial.delete();
            throw e;
        }
        if (target.exists() && !target.delete()) {
            partial.delete();
            throw new IOException("could not replace " + folder + "/" + name);
        }
        if (!partial.renameTo(target)) {
            partial.delete();
            throw new IOException("could not write " + folder + "/" + name);
        }
        report.imported.add(folder + "/" + name);
    }
}
