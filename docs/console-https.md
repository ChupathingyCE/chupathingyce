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
  `CONSOLE_HTTPS_STACK_SIZE` (112 KB) of stack beyond its own use.
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
| stats.chupathingy.org | its public key (SHA-256 of the certificate's SubjectPublicKeyInfo): one of the built-in pins, or of the newest signed pin document's; failing those, a chain to ISRG Root X1 or X2 (the backstop) |
| raw.githubusercontent.com | its chain: it must lead to one of the roots in `console_https_roots.c` (ISRG Root X1 and X2, USERTrust RSA and ECC, DigiCert Global Root G2) |

The certificate must name the host in every case. Dates are never checked.
The check runs after the handshake and before a byte of the request is
sent. The handshake itself proves the server holds the certificate's key.

For the site, pins win. The order is:

1. The built-in pins, and the pins of the newest pin document the console
   has kept. A match is taken.
2. No match: the console asks for the pin document (below). If it brings
   new pins, the request is tried again with them.
3. Still no match: a chain to ISRG Root X1 or X2 that names the host is
   taken, and logged (`its key is not pinned; taken by its chain to a
   root`). This is the backstop: a console that missed a pin update is
   never stranded, and it asks for the pin document again 10 minutes
   later.
4. Otherwise the request is refused, and the game list falls back to plain
   HTTP.

So the site can renew with a new key at any time. A console that has the
new pin takes it by its pin; one that does not yet takes it by its chain
and fetches the new pin.

## Pin updates

The pin document tells consoles the site's keys without a new build. It is
signed with the console pin key, a pair of Ed25519 keys used for nothing
else (`console_pins_key.h`): not Delta's table key, not the update key.

`console-pins.json`, at most 2048 bytes, with `console-pins.json.sig` (the
signature of its exact bytes, 128 hex digits and a line feed):

```json
{"console_pins": 1, "serial": 3, "epoch": 0, "issued": 1793232000,
 "hosts": {"stats.chupathingy.org": {"current": "<64 hex digits>",
                                     "next": "<64 hex digits>"}}}
```

- `current` is the key the site uses; `next` (optional) is the one it will
  move to. Both are taken. Other hosts in the document are read past.
- A serial's top byte is its epoch, and `epoch` must match it. The primary
  key signs epoch 0 alone; the recovery key signs any epoch, so only it
  can open a new one. A leaked primary key is shut out by one document
  from the recovery key with the next epoch.
- A console takes a document only with a higher serial than the one it
  has. A document that does not verify, is older, or is not well formed
  changes nothing, and the log says so once a run.
- It is kept beside config.toml (the port's `pins_path`), checked again at
  every start.

Where it comes from, in order:

1. Plain HTTP: `http://warthog.milenko.org/v1/console/pins` and
   `/v1/console/pins.sig` (the port's `plain_get`). The signature makes
   plain HTTP safe.
2. HTTPS from GitHub: `raw.githubusercontent.com/ChupathingyCE/chupathingyce/console-pins/console-pins.json`
   and `.sig` (the `console-pins` branch), checked by GitHub's roots.

When: right after the site's key was not one the console pins (at most
every 10 minutes), and otherwise once a day while the console runs.

`tools/console_pins.py` makes, signs and checks the document (`make`,
`sign`, `verify`, `pin`, `next-serial`, `keygen`). The command repository's
Console pins workflow publishes it, and its certbot deploy hook starts that
workflow after each renewal of the site's certificate.

### Making the pin keys

The keys in `console_pins_key.h` are zero until this is done. A build with
no key takes no pin document and keeps to its built-in pins and the
backstop.

1. On a computer that stays offline, with OpenSSL 3:
   `python3 tools/console_pins.py keygen --out console-pins-primary.pem`
   and `python3 tools/console_pins.py keygen --out console-pins-recovery.pem`.
2. Each prints its public key as a C array. Paste the primary's into the
   first entry of `console_pins_keys` in
   `port/console/https/console_pins_key.h`, and the recovery key's into the
   second. Only these public halves go in the repository.
3. In ChupathingyCE/command, add the private keys as Actions secrets, each
   the whole PEM file's text:
   - `CONSOLE_PINS_SIGNING_KEY` (the primary) in the environment `signing`;
   - `CONSOLE_PINS_RECOVERY_KEY` (the recovery key) in the environment
     `signing-recovery`.
4. Keep both PEM files offline (two copies, apart). Delete every other
   copy.
5. Run the Console pins workflow with `read_site` on, for the first
   document, and check it with
   `python3 tools/console_pins.py verify console-pins.json` (from the
   `console-pins` branch).

### Rotating the site's key

Nothing has to happen before a renewal: a new key is taken by the backstop
until the pin document brings it. To keep consoles on pins the whole time:

1. Make the next key on the site host and get its pin:
   `python3 tools/console_pins.py pin --certificate <cert.pem>` once a
   certificate exists for it, or from the key itself with
   `openssl pkey -in next.key -pubout -outform der | openssl dgst -sha256`.
2. Run the Console pins workflow with that pin as `next_pin`. Consoles take
   it at their next daily check.
3. Switch the site to the new key. The deploy hook publishes it as
   `current`.

If the pin key itself is lost or leaked: sign with the recovery key
(`use_recovery`, and `new_epoch` if the primary leaked), then put a new
primary in `console_pins_key.h` in the next release, its last epoch the one
the recovery key opened.

The built-in `site_pins` in `console_https_trust.c` are the starting point
for a console that has never fetched a document. Update them in a release
now and then, so new installs start current.

To change GitHub's roots, run `tools/console_https_roots.py` with the new
set and check each fingerprint it prints against the authority's published
one. The site's backstop is the list's first two (ISRG Root X1 and X2).

To check the live hosts from a computer:
`python3 tools/test_console_https.py --keep /tmp/https`, then
`/tmp/https/build/console_https_test 0 stats.chupathingy.org --live --path /v1/health`.

## Tests

`tools/test_console_https.py` builds the client on the host with the
consoles' configuration and runs it against a local TLS server, over TLS 1.2
and 1.3. It checks that a pinned key is taken, another key refused (even
under a root it does not have), the backstop taking a key under the site's
root, a certificate for another host refused, expired and future
certificates taken, GitHub-style RSA chains taken only to a pinned
root, unknown hosts refused, a failed random source refused, an answer
larger than the buffer refused, and that the arena stays under its size and
is empty after every request.

The pin document, with the test's own pin keys: a key it does not pin, then
the document, then HTTPS with the new pin; the kept document used at the
next start; a valid document with a lower serial refused; a bad signature
refused; a new epoch from the primary key refused and from the recovery key
taken; a broken document refused.

## On the console

Each console's log (`debug.txt`, `port.txt` on the Wii U) shows:

- `https: <host>: the handshake failed (-0x....)`: no TLS session. The
  number is Mbed TLS's error.
- `https: <host>: the server's key is not one this console trusts`: the
  key or the chain was refused. The plain HTTP list is used instead.
- `https: <host>: the certificate is not for this host`: the name did not
  match.
- `https: stats.chupathingy.org: its key is not pinned; taken by its chain
  to a root`: the backstop took the site's key.
- `https: pin document N from <host> taken: K pins for stats.chupathingy.org`,
  or `https: no new pin document: <why>`.
- The game list's own lines say whether the list came over HTTPS or HTTP.
