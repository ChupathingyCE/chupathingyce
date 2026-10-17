package com.halo.decomp;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.Signature;

/**
 * The Android updater's reading of a release's list of update files
 * (UpdateManifest), on a desktop Java; run by tools/test_update_manifest.py.
 * With arguments, "find LIST VERSION PLATFORM CONFIGURATION KIND" answers as
 * tools/update_check.c does, so the two readers are checked on the same lists.
 */
public final class UpdateManifestTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static String list(String version, String files) {
        return "{\"format\": \"chupathingyce-updates 1\", \"version\": \"" + version + "\", \"commit\": \"\", "
            + "\"files\": [" + files + "]}";
    }

    private static String entry(String platform, String configuration, String kind, String tag, String name,
            String size, String sha256) {
        return "{\"platform\": \"" + platform + "\", \"configuration\": \"" + configuration + "\", \"kind\": \""
            + kind + "\", \"tag\": \"" + tag + "\", \"name\": \"" + name + "\", \"size\": " + size
            + ", \"sha256\": \"" + sha256 + "\"}";
    }

    private static String answer(String text, String version, String platform, String configuration, String kind) {
        try {
            UpdateManifest.Entry found = UpdateManifest.find(text, version, platform, configuration, kind);

            return found == null ? "missing" : "found " + found.tag + " " + found.name + " " + found.size + " "
                + found.sha256;
        } catch (UpdateManifest.Refused e) {
            return "refused";
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 6 && args[0].equals("find")) {
            String text = new String(Files.readAllBytes(Paths.get(args[1])), StandardCharsets.UTF_8);

            System.out.println(answer(text, args[2], args[3], args[4], args[5]));
            return;
        }
        String hash = "ab".repeat(32);
        String good = entry("android", "release", "zip", "v0.8.0", "chupathingyce-android-release.zip", "1000", hash);
        check(answer(list("0.8.0", good), "0.8.0", "android", "release", "zip")
            .equals("found v0.8.0 chupathingyce-android-release.zip 1000 " + hash), "a listed file is found");
        check(answer(list("0.8.0", good), "0.8.0", "android64", "release", "zip").equals("missing"),
            "another platform's file is not this one's");
        check(answer(list("0.8.0", entry("android", "debug", "zip", "v0.8.0-debug", "a.zip", "5", hash)), "0.8.0",
            "android", "debug", "zip").startsWith("found v0.8.0-debug "), "the debug builds' release");
        check(answer(list("0.7.9", good), "0.8.0", "android", "release", "zip").equals("refused"),
            "another release's list is refused");
        check(answer(list("0.8.0", good).replace("updates 1", "updates 2"), "0.8.0", "android", "release", "zip")
            .equals("refused"), "a later format is refused");
        for (String[] change : new String[][] {
                { "v0.8.0\"", "v0.7.0\"" }, { "v0.8.0\"", "v0.8.0x\"" },
                { "chupathingyce-android-release.zip", "../halo.zip" }, { "chupathingyce-android-release.zip", "a/b" },
                { "1000", "-1" }, { "1000", "1.5" }, { "1000", "\"1000\"" }, { hash, "zz" + hash.substring(2) },
                { hash, hash.substring(2) } }) {
            check(answer(list("0.8.0", good.replace(change[0], change[1])), "0.8.0", "android", "release", "zip")
                .equals("refused"), "a file that is not one is refused: " + change[1]);
        }
        check(answer(list("0.8.0", good + ", " + good), "0.8.0", "android", "release", "zip").equals("refused"),
            "two files for one build are refused");
        check(answer(list("0.8.0", good).replace("\"commit\": \"\"",
            "\"later\": {\"x\": [1, 2.5e3, null, true, \"\\u00e9\"]}"), "0.8.0", "android", "release", "zip")
            .startsWith("found"), "a later list's fields are passed over");
        check(answer(list("0.8.0", good) + "x", "0.8.0", "android", "release", "zip").equals("refused"),
            "text after the list is refused");
        check(answer("[".repeat(100) + "]".repeat(100), "0.8.0", "android", "release", "zip").equals("refused"),
            "a list nested too deep is refused");
        check(answer("", "0.8.0", "android", "release", "zip").equals("refused"), "an empty list is refused");

        // (the list's signature: a key made here, never a real one)
        KeyPair pair = KeyPairGenerator.getInstance("Ed25519").generateKeyPair();
        byte[] encoded = pair.getPublic().getEncoded();
        String key = UpdateManifest.toHex(java.util.Arrays.copyOfRange(encoded, encoded.length - 32, encoded.length));
        byte[] document = list("0.8.0", good).getBytes(StandardCharsets.UTF_8);
        Signature signer = Signature.getInstance("Ed25519");
        signer.initSign(pair.getPrivate());
        signer.update(document);
        String signature = UpdateManifest.toHex(signer.sign()) + "\n";
        check(UpdateManifest.check(document, signature, new String[] { key }) == UpdateManifest.Check.SIGNED,
            "the list's signature checks");
        check(UpdateManifest.check(document, signature, UpdateManifest.KEYS) == UpdateManifest.Check.NOT_SIGNED,
            "a key that is not the release's is turned away");
        document[10] ^= 1;
        check(UpdateManifest.check(document, signature, new String[] { key }) == UpdateManifest.Check.NOT_SIGNED,
            "a changed list is turned away");
        check(UpdateManifest.check(document, "00", new String[] { key }) == UpdateManifest.Check.NOT_SIGNED,
            "a signature that is not one is turned away");
        check(UpdateManifest.sha256(new byte[0])
            .equals("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "SHA-256");
        System.out.println("UpdateManifestTest: passed");
    }
}
