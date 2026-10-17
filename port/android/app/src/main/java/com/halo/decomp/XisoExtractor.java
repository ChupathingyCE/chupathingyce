package com.halo.decomp;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Copies the maps folder out of an Xbox disc image (an "xiso", or a whole
 * disc's ".iso"), as the desktop games do: a Java port of
 * port/linux/src/xiso.c, whose reader is based on the XISO reader in
 * Milenko's Theseus project (GPL-3.0-or-later). xiso.c describes the disc's
 * file system (XDVDFS): 2048-byte sectors, a volume descriptor in sector 32
 * that gives the root directory's sector and size, and directories whose
 * entries form a binary tree (each entry: left and right subtree offsets in
 * 4-byte units, 0 for none; the start sector; the size; attributes; the
 * name's length and the name; 4-byte aligned, never across a sector, the
 * rest of a sector 0xff padding). Images made from a whole disc put the game
 * partition further in, at one of the offsets below.
 *
 * Every file in the maps folder, and in any folders inside it, is written to
 * maps.partial first and moved into maps once they all are, so an
 * interrupted extraction never leaves maps/ui.map behind.
 *
 * The .bik files in the disc's bink folder (the movies) that the
 * destination's bink folder lacks are copied after the maps, each as
 * <name>.partial first, and never over a file already there. A disc without
 * a bink folder, or a movie that cannot be copied, does not fail the
 * extraction.
 */
final class XisoExtractor {
    private static final int SECTOR_SIZE = 2048;
    private static final long VOLUME_SECTOR = 32;
    private static final int ENTRY_HEADER_SIZE = 14;
    private static final int ATTRIBUTE_DIRECTORY = 0x10;
    /* directory tables are a few sectors; anything much larger is not one */
    private static final long MAXIMUM_DIRECTORY_SIZE = 4 << 20;
    /* an AVL tree of a full table is under 30 deep; deeper is a loop */
    private static final int MAXIMUM_TREE_DEPTH = 64;
    private static final int MAXIMUM_FOLDER_DEPTH = 16;
    private static final int MAXIMUM_FILES = 4096;
    private static final int COPY_BUFFER_SIZE = 1 << 20;
    private static final byte[] VOLUME_MAGIC = "MICROSOFT*XBOX*MEDIA".getBytes(StandardCharsets.US_ASCII);

    /* where the game partition starts: an xiso, an original Xbox disc (XGD1),
    and the Xbox 360's XGD2 and XGD3 discs */
    private static final long[] PARTITION_OFFSETS = { 0, 0x18300000L, 0x0FD90000L, 0x02080000L };

    interface Progress {
        void report(String file, long done, long total);
    }

    /** a reason the player can act on */
    static final class ExtractException extends IOException {
        ExtractException(String message) {
            super(message);
        }
    }

    private static final class Entry {
        /* the path below the directory listed, '/'-separated */
        final String path;
        final long sector;
        final long size;
        final boolean directory;

        Entry(String path, long sector, long size, boolean directory) {
            this.path = path;
            this.sector = sector;
            this.size = size;
            this.directory = directory;
        }
    }

    /** a table that does not hold together */
    private static final class DamagedException extends Exception {
    }

    private final FileChannel image;
    private long partition;

    private XisoExtractor(FileChannel image) {
        this.image = image;
    }

    static void extractMaps(FileChannel image, File destination, Progress progress) throws IOException {
        new XisoExtractor(image).extract(destination, progress);
    }

    /* reads buffer.remaining() bytes at offset in the game partition */
    private void readAt(long offset, ByteBuffer buffer) throws IOException {
        long position = partition + offset;
        while (buffer.hasRemaining()) {
            int count = image.read(buffer, position);
            if (count <= 0)
                throw new ExtractException("Could not read the disc image (is it complete?).");
            position += count;
        }
        buffer.flip();
    }

    /* finds the game partition; returns the root directory's sector and size */
    private long[] findVolume() throws IOException {
        ByteBuffer descriptor = ByteBuffer.allocate(SECTOR_SIZE).order(ByteOrder.LITTLE_ENDIAN);
        for (long offset : PARTITION_OFFSETS) {
            partition = offset;
            descriptor.clear();
            try {
                readAt(VOLUME_SECTOR * SECTOR_SIZE, descriptor);
            } catch (ExtractException e) {
                continue;
            }
            boolean magic = true;
            for (int index = 0; index < VOLUME_MAGIC.length; index++)
                magic &= descriptor.get(index) == VOLUME_MAGIC[index];
            if (magic)
                return new long[] { descriptor.getInt(20) & 0xFFFFFFFFL, descriptor.getInt(24) & 0xFFFFFFFFL };
        }
        throw new ExtractException("This is not an Xbox disc image.");
    }

    /**
     * whether a name is one to write as a file or folder name: no path, no
     * "." or "..", and only printable ASCII that every file system takes as a
     * plain name (a disc's maps folder holds .map files and loading.tga)
     */
    static boolean isPlainName(String name) {
        if (name.isEmpty() || name.equals(".") || name.equals(".."))
            return false;
        for (int index = 0; index < name.length(); index++) {
            char c = name.charAt(index);
            if (c < 0x20 || c > 0x7E || "/\\:*?\"<>|".indexOf(c) >= 0)
                return false;
        }
        return true;
    }

    /* adds the entries of the subtree at offset (in 4-byte units) to list,
    in order, their paths after prefix; names that cannot be written as
    they are (isPlainName) are left out */
    private static void walkTree(ByteBuffer table, int offset, int depth, String prefix, List<Entry> list)
            throws DamagedException {
        int position = offset * 4;
        int size = table.limit();
        if (depth > MAXIMUM_TREE_DEPTH || position + ENTRY_HEADER_SIZE > size)
            throw new DamagedException();
        int length = table.get(position + 13) & 0xFF;
        if (length == 0 || position + ENTRY_HEADER_SIZE + length > size)
            throw new DamagedException();

        int left = table.getShort(position) & 0xFFFF;
        if (left != 0)
            walkTree(table, left, depth + 1, prefix, list);

        byte[] bytes = new byte[length];
        for (int index = 0; index < length; index++)
            bytes[index] = table.get(position + ENTRY_HEADER_SIZE + index);
        String name = new String(bytes, StandardCharsets.ISO_8859_1);
        if (isPlainName(name)) {
            if (list.size() >= MAXIMUM_FILES)
                throw new DamagedException();
            list.add(new Entry(prefix.isEmpty() ? name : prefix + "/" + name,
                table.getInt(position + 4) & 0xFFFFFFFFL, table.getInt(position + 8) & 0xFFFFFFFFL,
                (table.get(position + 12) & ATTRIBUTE_DIRECTORY) != 0));
        }

        int right = table.getShort(position + 2) & 0xFFFF;
        if (right != 0)
            walkTree(table, right, depth + 1, prefix, list);
    }

    /* adds a directory's entries to list, and with recurse everything in the
    folders in it, after its own entries (so a folder always comes before
    what it holds) */
    private void listDirectory(long sector, long size, String prefix, boolean recurse, int depth, List<Entry> list,
            String damaged) throws IOException {
        if (depth > MAXIMUM_FOLDER_DEPTH || size > MAXIMUM_DIRECTORY_SIZE)
            throw new ExtractException(damaged);
        int first = list.size();
        if (size > 0) {
            ByteBuffer table = ByteBuffer.allocate((int) size).order(ByteOrder.LITTLE_ENDIAN);
            try {
                readAt(sector * SECTOR_SIZE, table);
                walkTree(table, 0, 0, prefix, list);
            } catch (ExtractException | DamagedException e) {
                throw new ExtractException(damaged);
            }
        }
        if (!recurse)
            return;
        int last = list.size();
        for (int index = first; index < last; index++) {
            Entry entry = list.get(index);
            if (entry.directory)
                listDirectory(entry.sector, entry.size, entry.path, true, depth + 1, list, damaged);
        }
    }

    /* the maps folder among the root's entries: "maps", or on a
    multi-language European disc (no "maps", a "maps_xx" per language
    instead) the one for the device's language, else English's, else the
    first */
    private static Entry findMapsFolder(List<Entry> root) {
        for (Entry entry : root) {
            if (entry.directory && entry.path.equalsIgnoreCase("maps"))
                return entry;
        }
        String wanted = "maps_" + Locale.getDefault().getLanguage();
        Entry english = null, first = null;
        for (Entry entry : root) {
            if (!entry.directory || entry.path.length() != 7 || !entry.path.regionMatches(true, 0, "maps_", 0, 5))
                continue;
            if (entry.path.equalsIgnoreCase(wanted))
                return entry;
            if (english == null && entry.path.equalsIgnoreCase("maps_en"))
                english = entry;
            if (first == null)
                first = entry;
        }
        return english != null ? english : first;
    }

    /* copies one file's sectors to path; returns done with its bytes added */
    private long copyFile(Entry entry, File path, ByteBuffer buffer, long done, long total, Progress progress)
            throws IOException {
        long offset = entry.sector * SECTOR_SIZE;
        long remaining = entry.size;

        try (FileOutputStream out = new FileOutputStream(path)) {
            FileChannel output = out.getChannel();

            while (remaining > 0) {
                buffer.clear();
                buffer.limit((int) Math.min(remaining, COPY_BUFFER_SIZE));
                readAt(offset, buffer);
                int count = buffer.remaining();
                while (buffer.hasRemaining())
                    output.write(buffer);
                offset += count;
                remaining -= count;
                done += count;
                progress.report(entry.path, done, total);
            }
        } catch (ExtractException e) {
            throw new ExtractException("Could not read " + entry.path + " from the disc image (is it complete?).");
        } catch (IOException e) {
            throw new ExtractException("Could not write " + path + " (is the storage full?).");
        }
        return done;
    }

    /* the .bik files of the disc's bink folder that bink lacks (whatever the
    case of their names there); none if the disc has no bink folder */
    private List<Entry> missingMovies(List<Entry> root, File bink) throws IOException {
        List<Entry> movies = new ArrayList<>();
        for (Entry folder : root) {
            if (!folder.directory || !folder.path.equalsIgnoreCase("bink"))
                continue;
            List<Entry> all = new ArrayList<>();
            try {
                listDirectory(folder.sector, folder.size, "", false, 0, all, "The disc image's bink folder is damaged.");
            } catch (ExtractException e) {
                return movies;
            }
            String[] present = bink.list();
            for (Entry entry : all) {
                if (entry.directory || !entry.path.toLowerCase(Locale.ROOT).endsWith(".bik"))
                    continue;
                boolean there = false;
                if (present != null) {
                    for (String name : present)
                        there |= name.equalsIgnoreCase(entry.path);
                }
                if (!there)
                    movies.add(entry);
            }
            break;
        }
        return movies;
    }

    private void extract(File destination, Progress progress) throws IOException {
        long[] root = findVolume();

        /* the root's maps folder */
        List<Entry> rootEntries = new ArrayList<>();
        listDirectory(root[0], root[1], "", false, 0, rootEntries, "The disc image's file system is damaged.");
        Entry maps = findMapsFolder(rootEntries);
        if (maps == null)
            throw new ExtractException("The disc image has no maps folder: it is not a Halo disc.");

        /* everything in it */
        List<Entry> entries = new ArrayList<>();
        listDirectory(maps.sector, maps.size, "", true, 0, entries, "The disc image's maps folder is damaged.");
        long total = 0;
        boolean hasUi = false;
        for (Entry entry : entries) {
            if (entry.directory)
                continue;
            total += entry.size;
            hasUi |= entry.path.equalsIgnoreCase("ui.map");
        }
        if (!hasUi)
            throw new ExtractException("The disc image's maps folder has no ui.map: it is not a Halo disc.");

        File bink = new File(destination, "bink");
        List<Entry> movies = missingMovies(rootEntries, bink);
        for (Entry movie : movies)
            total += movie.size;

        File partial = new File(destination, "maps.partial");
        File finished = new File(destination, "maps");
        if (!partial.isDirectory() && !partial.mkdirs())
            throw new ExtractException("Could not create " + partial + ".");
        ByteBuffer buffer = ByteBuffer.allocateDirect(COPY_BUFFER_SIZE);
        long done = 0;
        for (Entry entry : entries) {
            File path = new File(partial, entry.path);
            if (entry.directory) {
                if (!path.isDirectory() && !path.mkdirs())
                    throw new ExtractException("Could not create " + path + ".");
                continue;
            }
            done = copyFile(entry, path, buffer, done, total, progress);
        }

        /* (the maps folder may be there, empty: the app makes it for adb) */
        if (!finished.isDirectory() && !finished.mkdirs())
            throw new ExtractException("Could not create " + finished + ".");
        for (Entry entry : entries) {
            File from = new File(partial, entry.path);
            File to = new File(finished, entry.path);

            if (entry.directory) {
                if (!to.isDirectory() && !to.mkdirs())
                    throw new ExtractException("Could not create " + to + ".");
                continue;
            }
            to.delete();
            if (!from.renameTo(to))
                throw new ExtractException("Could not move " + entry.path + " into " + finished + ".");
        }
        /* (the folders left behind, now empty: the deepest first) */
        for (int index = entries.size() - 1; index >= 0; index--) {
            if (entries.get(index).directory)
                new File(partial, entries.get(index).path).delete();
        }
        partial.delete();

        /* the movies; the maps are all there, so one that fails is left out
        (the game plays without it) */
        if (movies.isEmpty() || (!bink.isDirectory() && !bink.mkdirs()))
            return;
        for (Entry entry : movies) {
            File from = new File(bink, entry.path + ".partial");
            try {
                done = copyFile(entry, from, buffer, done, total, progress);
                File to = new File(bink, entry.path);
                if (to.exists() || !from.renameTo(to))
                    from.delete();
            } catch (IOException e) {
                from.delete();
                return;
            }
        }
    }
}
