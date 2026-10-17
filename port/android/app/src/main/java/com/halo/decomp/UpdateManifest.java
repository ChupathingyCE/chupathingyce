package com.halo.decomp;

import java.security.KeyFactory;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.PublicKey;
import java.security.Signature;
import java.security.spec.X509EncodedKeySpec;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * A release's list of its update files (updates.json), as the desktop
 * games read it (port/linux/src/update_manifest.h, which has its shape): for
 * each platform and configuration, the file, the release (tag) it is on, its
 * size and SHA-256. updates.json.sig is the Ed25519 signature of the list's
 * exact bytes by the release key (update_signature.h), so the one signature
 * covers every file the list names.
 *
 * Android checks the list's signature where it has Ed25519 (Android 13 and
 * later); before that, the list's SHA-256 is checked and, as for every
 * update, Android installs the new app only when it is signed with the same
 * app key as the one installed. Nothing here needs Android, so its tests run
 * on a desktop Java (tools/test_update_manifest.py).
 */
final class UpdateManifest {
    static final String FORMAT = "chupathingyce-updates 1";
    /** the release keys, update_key.h's (the release key, then the recovery key) */
    static final String[] KEYS = {
        "c664b381ef69c556d47315b839803be80039771fa8dd1051dd6f955708c1c662",
        "322c5909e9772f70148cf9ac114883fc2ad1a1501c71aed7103e6e4bf769e088",
    };
    /** (an Ed25519 public key's DER: this prefix, then its 32 bytes) */
    private static final String ED25519_SPKI_PREFIX = "302a300506032b6570032100";
    private static final int DEPTH = 16;

    /** the list is not one, not this release's, or not signed: none of it is used */
    static final class Refused extends Exception {
        Refused(String message) {
            super(message);
        }
    }

    /** one of the list's files */
    static final class Entry {
        final String tag, name, sha256;
        final long size;

        Entry(String tag, String name, long size, String sha256) {
            this.tag = tag;
            this.name = name;
            this.size = size;
            this.sha256 = sha256;
        }
    }

    enum Check { SIGNED, NOT_SIGNED, NOT_CHECKED }

    private UpdateManifest() {
    }

    /**
     * Whether signature (the .sig's text: 128 hex digits) is one of
     * document's by one of keys (hex); NOT_CHECKED where this Java has no
     * Ed25519.
     */
    static Check check(byte[] document, String signature, String[] keys) {
        byte[] bytes = hex(signature.trim());

        if (bytes == null || bytes.length != 64)
            return Check.NOT_SIGNED;
        try {
            Signature verifier = Signature.getInstance("Ed25519");
            KeyFactory factory = KeyFactory.getInstance("Ed25519");

            for (String key : keys) {
                PublicKey publicKey = factory.generatePublic(new X509EncodedKeySpec(hex(ED25519_SPKI_PREFIX + key)));

                verifier.initVerify(publicKey);
                verifier.update(document);
                if (verifier.verify(bytes))
                    return Check.SIGNED;
            }
            return Check.NOT_SIGNED;
        } catch (NoSuchAlgorithmException e) {
            return Check.NOT_CHECKED;
        } catch (Exception e) {
            return Check.NOT_SIGNED;
        }
    }

    /**
     * The list's file of kind for platform and configuration, of the
     * release of version; null if the list has none (the zip by its old
     * name, then).
     */
    static Entry find(String text, String version, String platform, String configuration, String kind)
            throws Refused {
        Object parsed;

        try {
            Reader reader = new Reader(text);

            parsed = reader.value(0);
            reader.space();
            if (reader.at != text.length())
                throw new Refused("the list is not JSON this build can read");
        } catch (RuntimeException e) {
            throw new Refused("the list is not JSON this build can read");
        }
        if (!(parsed instanceof Map))
            throw new Refused("the list is not JSON this build can read");
        Map<?, ?> list = (Map<?, ?>) parsed;
        if (!FORMAT.equals(list.get("format")))
            throw new Refused("the list is not one this build reads");
        // (a list signed for another release is not this one's)
        if (!version.equals(list.get("version")))
            throw new Refused("the list is version " + list.get("version") + "'s, not " + version + "'s");
        if (!(list.get("files") instanceof List))
            throw new Refused("the list has no files");
        Map<?, ?> found = null;
        for (Object item : (List<?>) list.get("files")) {
            if (!(item instanceof Map))
                throw new Refused("the list's files are not ones");
            Map<?, ?> file = (Map<?, ?>) item;

            if (platform.equals(file.get("platform")) && configuration.equals(file.get("configuration"))
                && kind.equals(file.get("kind"))) {
                if (found != null)
                    throw new Refused("the list has more than one " + kind + " file for " + platform + " "
                        + configuration);
                found = file;
            }
        }
        if (found == null)
            return null;
        Object tag = found.get("tag"), name = found.get("name"), size = found.get("size"),
            sha256 = found.get("sha256");
        // (its file in full, named as the release workflow names them; the
        // release it is on is one of this version's)
        if (!word(tag) || !word(name) || !(size instanceof Long) || (Long) size < 0 || !(sha256 instanceof String)
            || !((String) sha256).matches("[0-9a-fA-F]{64}")
            || !(tag.equals("v" + version) || ((String) tag).startsWith("v" + version + "-")))
            throw new Refused("the list's " + kind + " file for " + platform + " " + configuration + " is not one");
        return new Entry((String) tag, (String) name, (Long) size, ((String) sha256).toLowerCase());
    }

    /** (letters, digits and . _ + -, as the release workflow names its files and tags) */
    private static boolean word(Object value) {
        return value instanceof String && ((String) value).matches("[0-9A-Za-z_+-][0-9A-Za-z._+-]{0,126}");
    }

    static String sha256(byte[] data) {
        try {
            return toHex(MessageDigest.getInstance("SHA-256").digest(data));
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }

    static String toHex(byte[] bytes) {
        StringBuilder text = new StringBuilder();

        for (byte b : bytes)
            text.append(String.format("%02x", b & 255));
        return text.toString();
    }

    private static byte[] hex(String text) {
        if (text.length() % 2 != 0)
            return null;
        byte[] bytes = new byte[text.length() / 2];
        for (int index = 0; index < bytes.length; index++) {
            int high = Character.digit(text.charAt(index * 2), 16), low = Character.digit(text.charAt(index * 2 + 1), 16);

            if (high < 0 || low < 0)
                return null;
            bytes[index] = (byte) (high << 4 | low);
        }
        return bytes;
    }

    /**
     * The list's JSON, as maps, lists, strings, Longs (whole numbers),
     * Doubles, Booleans and null; anything else is an exception.
     */
    private static final class Reader {
        final String text;
        int at;

        Reader(String text) {
            this.text = text;
        }

        void space() {
            while (at < text.length() && " \t\r\n".indexOf(text.charAt(at)) >= 0)
                at++;
        }

        boolean take(char c) {
            space();
            if (at < text.length() && text.charAt(at) == c) {
                at++;
                return true;
            }
            return false;
        }

        Object value(int depth) {
            space();
            if (depth > DEPTH)
                throw new IllegalArgumentException("too deep");
            char c = text.charAt(at);
            if (c == '{') {
                Map<String, Object> map = new LinkedHashMap<>();

                at++;
                if (!take('}')) {
                    do {
                        space();
                        String key = string();

                        if (!take(':') || map.containsKey(key))
                            throw new IllegalArgumentException("not a field");
                        map.put(key, value(depth + 1));
                    } while (take(','));
                    if (!take('}'))
                        throw new IllegalArgumentException("not an object");
                }
                return map;
            }
            if (c == '[') {
                List<Object> list = new ArrayList<>();

                at++;
                if (!take(']')) {
                    do {
                        list.add(value(depth + 1));
                    } while (take(','));
                    if (!take(']'))
                        throw new IllegalArgumentException("not a list");
                }
                return list;
            }
            if (c == '"')
                return string();
            for (String literal : new String[] { "true", "false", "null" }) {
                if (text.startsWith(literal, at)) {
                    at += literal.length();
                    return literal.equals("null") ? null : Boolean.valueOf(literal);
                }
            }
            return number();
        }

        String string() {
            StringBuilder out = new StringBuilder();

            if (text.charAt(at) != '"')
                throw new IllegalArgumentException("not a string");
            for (at++;; at++) {
                char c = text.charAt(at);

                if (c == '"') {
                    at++;
                    return out.toString();
                }
                if (c < 0x20)
                    throw new IllegalArgumentException("not a string");
                if (c == '\\') {
                    c = text.charAt(++at);
                    if (c == 'u') {
                        out.append((char) Integer.parseInt(text.substring(at + 1, at + 5), 16));
                        at += 4;
                    } else {
                        int index = "\"\\/bfnrt".indexOf(c);

                        if (index < 0)
                            throw new IllegalArgumentException("not an escape");
                        out.append("\"\\/\b\f\n\r\t".charAt(index));
                    }
                } else {
                    out.append(c);
                }
            }
        }

        Object number() {
            java.util.regex.Matcher matcher = java.util.regex.Pattern
                .compile("-?(0|[1-9][0-9]*)(\\.[0-9]+)?([eE][+-]?[0-9]+)?").matcher(text);

            if (!matcher.find(at) || matcher.start() != at)
                throw new IllegalArgumentException("not a value");
            String number = matcher.group();
            at = matcher.end();
            if (matcher.group(2) == null && matcher.group(3) == null && number.length() <= 15)
                return Long.valueOf(number);
            return Double.valueOf(number);
        }
    }
}
