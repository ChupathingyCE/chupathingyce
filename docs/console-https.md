# HTTPS on the consoles

The original Xbox, the Xbox 360 and the Wii U talk HTTPS to
stats.chupathingy.org and to GitHub's raw files through one shared client,
`port/console/https`. It is Mbed TLS 3.6 (the copy in
`port/third_party/mbedtls`, the same one the PC, Android and the server use)
over each console's own sockets. The Switch is a desktop build: it keeps
`port/linux/src/posix_browser.c`, with its own certificate bundle.

What goes over it:

- The console game list (`/v1/console/games`) from stats.chupathingy.org.
  When HTTPS fails, the console falls back to plain HTTP from
  warthog.milenko.org, as before.
- The signed legacy table (`/v1/delta/legacy` and its `.sig`, else GitHub's
  copy on the `delta-table` branch). The table is signed, so it would be
  safe over plain HTTP too; TLS just keeps it private.
- The console link calls (`port/console/link`), through the link's
  `https_request` hook (`console_https_site_request`).

## How it works

A port fills in `struct console_https_port` (`console_https.h`): connect,
send, receive and close on its sockets, its hardware random numbers, a
millisecond counter, a lock, and a log line. It calls
`console_https_initialize` once its network is up. Requests then block on
the caller's thread, so they always run on a thread of the port's, never
the game's.

- **Config.** `console_mbedtls_config.h`: a TLS 1.2 and 1.3 client only.
  ECDHE (X25519, P-256, P-384) with AES-GCM or ChaCha20-Poly1305. ECDSA and
  RSA signatures are verified, never made. No server, DTLS, renegotiation,
  tickets, files or threads.
- **Memory.** Mbed TLS allocates from an 80 KB arena on the requesting
  thread's stack, so the game's heap is never touched. The arena is wiped
  and gone when the request returns. A handshake uses about 30 KB with the
  site and about 50 KB with GitHub's longer RSA chain (measured on a 64-bit
  host; less on the consoles). A thread that calls in needs
  `CONSOLE_HTTPS_STACK_SIZE` (104 KB) of stack beyond its own use.
- **Random numbers.** `MBEDTLS_ENTROPY_HARDWARE_ALT`: the port's `random`
  hook is the only entropy source. It seeds PSA's CTR-DRBG at the start of
  every request. If the hook fails, the request fails.
- **No clock.** `MBEDTLS_HAVE_TIME_DATE` is off, so certificate dates are
  never checked. Many consoles never set their clock, and a wrong one would
  otherwise lock them out.

## What it trusts

`console_https_trust.c` lists the only hosts the client will talk to.

| Host | Checked by |
| --- | --- |
| stats.chupathingy.org | its public key: SHA-256 of the certificate's SubjectPublicKeyInfo must be one of the pinned keys |
| raw.githubusercontent.com | its chain: it must lead to one of the roots in `console_https_roots.c` (ISRG Root X1 and X2, USERTrust RSA and ECC, DigiCert Global Root G2) |

The certificate must name the host in both cases. The check runs after the
handshake and before a byte of the request is sent. The handshake itself
proves the server holds the certificate's key.

Pinning the site's key, and not its certificate authority, means nobody can
get a console to trust a different stats.chupathingy.org, even with a valid
certificate from a real authority. Without a clock, it also means an old
certificate is no use to anyone who does not have the site's private key.

## Rotating keys

The site's certificate has to renew with the same key. With certbot that
is `reuse_key = True` in `/etc/letsencrypt/renewal/stats.chupathingy.org.conf`
(or `certbot renew --reuse-key`). A renewal then changes nothing for the
consoles.

Two keys are pinned: the one in use, and a backup that has never been on
the server. The backup's private key is kept offline by the site's owner,
not in any repository.

To move the site to a new key:

1. Make the next backup key and get its pin:
   `openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out next.key`,
   then `openssl pkey -in next.key -pubout -outform der | openssl dgst -sha256`.
2. Add the new pin to `site_pins` in `console_https_trust.c`, keeping the
   current and backup pins. Ship it in a console release.
3. Wait until most consoles have that release.
4. Switch the site to the old backup key (`certbot --key-path` or a new
   certificate request for that key). The backup is now the key in use, and
   the key from step 1 is the new backup.
5. In a later release, drop the pin of the key that is no longer used.

If the key in use is ever lost or leaked, go straight to step 4 with the
backup key. Consoles on an older release keep working as long as the key
the site uses is one they pin.

A console that does not pin the site's key (an old build, or a mistake)
still gets the game list over plain HTTP and the legacy table from GitHub,
so it is never cut off. The log says why HTTPS was refused.

To change GitHub's roots, run `tools/console_https_roots.py` with the new
set and check each fingerprint it prints against the authority's published
one. GitHub moving to a root not on the list only costs the GitHub copy of
the table; the site's copy still works.

To check the live hosts against the pins and roots from a computer:
`python3 tools/test_console_https.py --keep /tmp/https`, then
`/tmp/https/build/console_https_test 0 stats.chupathingy.org --live --path /v1/health`.

## Tests

`tools/test_console_https.py` builds the client on the host with the
consoles' configuration and runs it against a local TLS server, over TLS 1.2
and 1.3. It checks that a pinned key is taken, another key refused (even
under a known root), a certificate for another host refused, expired and
future certificates taken, GitHub-style RSA chains taken only to a pinned
root, unknown hosts refused, a failed random source refused, an answer
larger than the buffer refused, and that the arena stays under its size and
is empty after every request.

## On the console

Each console's log (`debug.txt`, `port.txt` on the Wii U) shows:

- `https: <host>: the handshake failed (-0x....)`: no TLS session. The
  number is Mbed TLS's error.
- `https: <host>: the server's key is not one this console trusts`: the
  key or the chain was refused. The plain HTTP list is used instead.
- `https: <host>: the certificate is not for this host`: the name did not
  match.
- The game list's own lines say whether the list came over HTTPS or HTTP.
