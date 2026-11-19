# Delta Link

- **Players** (exists): Link Profile ties a game install to a site profile
  with a code (`/v1/link`, `/v1/connect`, `/v1/claim`). A link counts the
  install's games from the moment it's made. Games it played before count
  for no one until the profile's owner claims the ones that were theirs
  (the site's Past games from this device), since a shared computer or a
  second-hand console may have played someone else's. Claimed games and
  games played while linked stay with the profile after an unlink. Links
  made before this rule keep the games they had. Each device's line in a
  game is its own entry, so two of your devices in one game count once.
  A game that reaches the site late goes by the link that applied when it
  was played. A second-hand console: if someone else linked it first, the
  games from before their link stay theirs to claim, and the games it
  played while linked to them stay theirs; only the games after their
  unlink wait for you. An admin can move waiting games to another profile
  when they turn out to be someone else's.
- **Servers** (exists): see Delta Control, The link to the site.

## Device identity

Today a Delta profile rests on one secret, the player key
(`game_list_player.key`): whoever holds a copy of it is the player, and the
game sends it to the site with most requests. Device identity adds a key per
device whose private half never leaves it, and short-lived certificates
the site signs for those keys. Anyone can build a client, server or site
that speaks it from this section alone.

It comes in stages. **Stage A** (this section) runs beside the player key:
a new build proves its player key once, gets a device certificate, and from
then on sends the site a short-lived token instead of the key. Nothing
players see changes, the key keeps working everywhere it works today, and
OpenCE players see nothing at all. **Stage B** adds sign-in by code
(Quick Connect) and the device list with revoke, and **stage C** winds the
player key down per account ("Devices", below). Proof of identity over
Delta Peer, and showing only proven names in games, are "Verified names"
under Delta Peer.

**Keys.**

| Key | Who holds it | What it signs |
| --- | --- | --- |
| Root (the `identity` key set: primary and recovery) | GitHub environment secrets in command, made inside Actions like the update and delta sets. Nobody sees them | Issuer certificates (CISS) |
| Issuer | The site host, in a signer process of its own that the web process reaches over a Unix socket | Device certificates (CDEV) and the revocation list (CREV) |
| Device | Each install, made on first use. Never sent anywhere | Its requests to the site, and later its proofs in games |
| Player key (legacy) | Existing copies, as today | Nothing new. Sent once more, to enroll |

A build trusts both root keys (`identity_key.h`: the primary
`9c1c4454...468e` and the recovery `a3124502...d29f` of command's
`keys/identity.pub.json`, made 2026-11-09). The issuer key changes every few months: the site makes a new
pair and command's `identity-issuer.yml` signs a CISS for it with the root
key. A device certificate verifies while the issuer certificate that
vouches for it is in date.

**Encoding.** All three are binary with little-endian numbers, read byte by
byte (so big-endian hosts need no special case), and travel as lowercase
hex. Signatures are Ed25519 (RFC 8032), each over a label and then the
bytes before the signature, checked as Monocypher 4's `crypto_ed25519_check`
checks them: S must be below the group order, the check is cofactored
([8]R = [8]([S]B - [h]A)), and points that aren't canonically encoded are
allowed. A strictly cofactorless verifier can disagree on rare
deliberately-crafted signatures, so implementations should check the same
way. A verifier also refuses a public key of small order.

- **Key ID** of an issuer: the first 8 bytes of SHA-256(its public key).
- **Device ID**: the first 16 bytes of SHA-256(`"chupathingy device id\n"`
  + the device's public key).
- **Copy ID**: the player ID a player key has today, the first 16 bytes of
  SHA-256(`"halo-ce-universal player id\n"` + the key).

**CISS, the issuer certificate** (format 1, 112 bytes; signed by a root key
over `"chupathingy identity issuer v1\n"` + bytes 0 to 47):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `CISS` |
| 4 | 1 | format, 1 |
| 5 | 3 | reserved, 0 |
| 8 | 4 | valid from (Unix seconds) |
| 12 | 4 | valid until |
| 16 | 32 | the issuer's public key |
| 48 | 64 | signature |

**CDEV, the device certificate** (format 1, 148 + L bytes, 168 at most;
signed by the issuer over `"chupathingy device cert v1\n"` + bytes 0 to
83 + L):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `CDEV` |
| 4 | 1 | format, 1 |
| 5 | 1 | platform, the Delta platform number ("Platform numbers") |
| 6 | 1 | handle length L: 0 (no profile yet) or 3 to 20 |
| 7 | 1 | flags: 0x01 enrolled with a player key, 0x02 console |
| 8 | 4 | issued |
| 12 | 4 | expires |
| 16 | 8 | serial (random) |
| 24 | 8 | the issuer's key ID |
| 32 | 16 | account: the profile's player ID; the copy ID while it has no profile |
| 48 | 32 | the device's public key |
| 80 | 4 | capabilities |
| 84 | L | the handle, lowercase `a-z 0-9 _ -` |
| 84 + L | 64 | signature |

Capabilities say what the device may do as the account: 0x01 `play` (prove
identity in games), 0x02 `stats` (confirm stats lines, send reports),
0x04 `profile` (privacy and clan answers from the game), 0x08 `moderate`
(sign in to moderation; the server still checks the role), 0x10 `social`
(friends, presence, invites), 0x20 `devices` (approve another device's
code, revoke another device of the account; stage B). A device gets the
first five; only one signed in by code and approved from a password
session on the site also gets `devices`, so approving and revoking never
pass down a chain: not a device enrolled with a player key, not one
another device approved. A device of another app (a Delta client,
[clients.md](clients.md)) gets only the scopes it asked for
that its app has, and never `devices`. Unknown bits and flags are ignored. A certificate lasts 7 days on desktops and Android and 14 on
consoles, and the device refreshes it at half its life. The display name,
emblem and clan aren't in it; they change, and peers fetch them as today.

A certificate checks when its issuer's key ID is a trusted CISS's, the
issuer signed it, it was issued inside the CISS's validity, and now is
between issued and expires (10 minutes of grace either way). An expired
certificate never stops play; its player just isn't proven until it
refreshes.

**CREV, the revocation list** (format 1, 96 + 16 N bytes; signed by the
issuer over `"chupathingy device revocations v1\n"` + the bytes before the
signature):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `CREV` |
| 4 | 1 | format, 1 |
| 5 | 3 | reserved, 0 |
| 8 | 4 | serial, higher is newer |
| 12 | 4 | issued |
| 16 | 4 | next update |
| 20 | 8 | the issuer's key ID |
| 28 | 2 | count N, at most 4096 |
| 30 | 2 | reserved, 0 |
| 32 | 16 N | revoked device IDs, sorted, no repeats |
| 32 + 16 N | 64 | signature |

A device stays on the list for 15 days after it is revoked, longer than any
certificate it could still hold, so the list stays small.

**The site's endpoints** (Delta Link, HTTPS, JSON bodies; IDs, keys, nonces
and signatures in lowercase hex):

| Request | Answer |
| --- | --- |
| `GET /v1/identity/nonce` | `{"nonce": 32 hex, "expires_in": 300}`. Single use |
| `POST /v1/identity/legacy/enroll {"key", "device_key", "nonce", "signature", "platform", "build", "name"?}` | 200 with a certificate (below) |
| `POST /v1/identity/refresh {"device_id", "nonce", "signature"}` | 200 with a certificate |
| `POST /v1/identity/token {"device_id", "nonce", "signature"}` | `{"token", "expires_in": 3600, "scopes": [...]}` |
| `GET /v1/identity/revoked` | `{"revoked": CREV, "issuers": [CISS,...], "serial"}` |

- **The audience.** Every device signature names the site it is for: one
  byte of length, then the site's host as the device addresses it
  (lowercase, no scheme or port; `stats.chupathingy.org`). A signature made
  for one site is no use at another.
- **Enroll** proves the player key one last time (`key`, as today) and
  registers `device_key`. The signature is the device key's, over
  `"chupathingy legacy enroll v1\n"` + audience + nonce (16) + copy ID (16)
  + device public key (32). The site enrolls only a copy it has seen before
  (a profile, a link or a confirmed game). Enrolling the same device key
  again for the same copy gives a new certificate. A device key that was
  revoked or signed out can't enroll again: the device makes a new key.
- **Refresh** and **token** are signed by the device key over
  `"chupathingy device refresh v1\n"` or `"chupathingy device token v1\n"`
  + audience + nonce (16) + device ID (16). No secret travels. Both are
  refused for a revoked, signed-out or locked device.
- **A certificate answer:** `{"device_id", "certificate": CDEV,
  "issuer": CISS, "account", "handle", "expires", "refresh_after"}`.
  `expires` is Unix seconds; `refresh_after` is a duration, the seconds
  from now until the device should refresh (half the certificate's life).
  The account and handle follow the copy's link: a copy linked to a
  profile after it enrolled gets the profile's at its next refresh.
- **Refusals:** 400 a malformed request; 401 `{"error": "nonce"}` (get a
  new nonce) or `{"error": "signature"}`; 403 with `"revoked": true`,
  `"signed_out": true`, `"locked": true` or `"unknown_copy": true`; 404
  `{"enroll": true}` for a device the site doesn't know (enroll again);
  409 with `"other_copy": true` for a key that belongs to another copy (the
  device makes a new key, once) or `"too_many": true`; 429 slow down; 503
  busy, or the site doesn't issue certificates yet.
- **The issuer's signer** signs only certificates issued now (within 5
  minutes of its clock) and revocation lists whose serial rises and isn't
  ahead of its clock. The site checks every signature it gets back.
- **Stage A's limit:** whoever holds a copy's player key can still enroll a
  new device for it. Revoking a device stops that device key, not the
  player key; later stages let an account turn its old key off.

**Tokens.** A token is opaque ASCII, at most 200 characters of
`A-Z a-z 0-9. _ -`. It goes in a header, `Authorization: Delta <token>`,
in place of the `"key"` member, on every request that takes the player key
from the game today except Link Profile (`/v1/connect/start`), which sign-in
by code replaces on a site that speaks identity:

| Requests | Capability |
| --- | --- |
| `/v1/claim`, `/v1/client_report` | `stats` |
| `/v1/game/profile`, `/unlink`, `/privacy`, `/clan`, `/friends`, `/v1/link` (stage B) | `profile` |
| `/v1/presence`, `/v1/social/*` | `social` |
| `/v1/identity/devices` (stage B) | `profile` |
| `/v1/identity/device/revoke`, `/approve`, `/approved` (stage B) | `devices` |

The request acts for the copy the device enrolled with, exactly as its key
would. A token the site can't use gets 401 `{"token": "unknown" |
"expired" | "revoked" | "scope"}`. A site restart ends every token.

**How a client falls back.** Nobody is locked out at any step:

1. With no device certificate yet, the client sends its key as today. In
   the background it gets a nonce and enrolls. A 404 for
   `/v1/identity/nonce` means an older site: it tries again in an hour. A
   503 means busy or not issuing yet: it tries again in ten minutes, backing
   off to an hour. A 403 `unknown_copy` (a new install the site hasn't
   seen yet) tries again soon after the player's next confirmed game or
   report, or within an hour, whichever comes first. A 403 `locked` or a
   409 `too_many` waits a day, and those retries are capped.
2. With a certificate, it gets a token (nonce, then token) when it has none
   or the one it has is about to run out, and sends it instead of the key.
3. A request answered 401 with `"token"` drops the token, gets a new one,
   and sends the request again with it once. A request sent with a token
   goes with the key only when the site doesn't speak identity (its
   `/v1/identity/nonce` answers 404) or refuses the token as `"unknown"`
   and no new token can be had. Otherwise the answer is returned as it is,
   and a request the site answered is never sent again. From stage C a
   client with a certificate (or signed out by its player) never sends the
   key unless the site doesn't speak identity: without a token to send, the
   request waits and is tried again later.
4. At `refresh_after`, or on start when the saved certificate is past it or
   expired, it refreshes with the device key; it never enrolls again just
   because a certificate expired. A 404 with `"enroll"` enrolls again. A
   403 `revoked` or `signed_out` drops the certificate and keeps the key for
   this run.
5. A different player key (a restored backup, an Install in Game link)
   means a different copy. The device then drops its certificate and token
   and makes a new device key, which enrolls with the new player key. The
   copy ID is saved beside the certificate so this is caught at start too.
6. The key file stays. Old servers' `moderators.txt` list the moderator key
   derived from it, and older sites only know it.

**On the device.** `delta_device.key` in the save root holds the device
key's 32-byte seed, written and read like the player key (32 raw bytes,
readable by its owner alone, opened without following links).
`delta_device.cert` beside it holds the answer's certificate, issuer
certificate and the copy ID it was enrolled with (public data). Neither is ever logged. Consoles leave stage A
out until their own endpoints come.

**Test vectors.** Each seed or key below is SHA-256 of its text:
the root key's `chupathingy test identity root`, the issuer's
`chupathingy test identity issuer`, the device's `chupathingy test device
key`, and the player key `chupathingy test player key`. The certificates
use the times 1790000000 (valid from), 1797776000 (valid until), 1790100000
(issued) and 1790704800 (expires); the device certificate is for platform 1
(Windows), handle `milenko`, serial 01 02... 08, all five capabilities,
legacy enrolled; the list names the test device and sixteen 0x11 bytes.
The nonce is the bytes 00 to 0f and the audience `stats.chupathingy.org`.
The same values are in
`port/linux/tests/identity_vectors.json`, and the game's, the site's and
command's tests all check them.

```
root public        12762975a2781f981d53c9bfd7625f10b56137eee3235fb5f3eb92c966f023fa
issuer public      fc284aa53690a1b91984e67b1820c0a28452dafbc58ed107d40066ad47814d90
issuer key ID      48810d1682965a46
device public      f6bcb81b396cf7f3cb5519a437d3699adc4ed40f2e4d22f30c3bfecb6ffcca8e
device ID          a0f42dfd380bf6aee133508172a12700
copy ID            31ea6b25b20006a252c816cd2077ff5b

CISS  4349535301000000803bb16a80e2276bfc284aa53690a1b91984e67b1820c0a2
      8452dafbc58ed107d40066ad47814d9038cc84f430b86054d1ede8b53244a90b
      da00b6012d4df101a0e00f5d388af8193c95238b1498d9fbd9314d4226ca2e6d
      40332529ea8163ad127e579cc26dcc0c

CDEV  434445560101070120c2b26aa0fcbb6a010203040506070848810d1682965a46
      31ea6b25b20006a252c816cd2077ff5bf6bcb81b396cf7f3cb5519a437d3699a
      dc4ed40f2e4d22f30c3bfecb6ffcca8e1f0000006d696c656e6b6f359728fbc6
      34431278f9352410620d865f517ebad416d36e0c845cdc85613fb2e8867b8ff2
      b8fe26c1d7b5b9abb3d99c462edea4d82ad78c975c6b970f239b02

CREV  435245560100000020c2b26a20c2b26aa013b46a48810d1682965a4602000000
      11111111111111111111111111111111a0f42dfd380bf6aee133508172a12700
      1169fc31fd48dcb2b10d79abd24a09fc115f4323eae5feebbd1f7f9b97d24693
      2b730bd2e50b6be603b751854cceefdf32c3311a4a20e8f48c9410e1c40ae50f

enroll signature   cec504f992252432a5d499113cbb7469a791b22e743cd44f6f15b278df2d4882
                   e89a5724ddcaffe73fb2a14fb8a74fd21f00160cf177e9e2f64561cdbd3b4305
refresh signature  f09ba64e6aa8ca29d8414d334dca2759d86d29f3757493a2b00be775a770f1ce
                   0f7dc0388671683b6868a493416b50a27f11a1458bb1e29fc89541849b00170d
token signature    0c02b24be2e9caa6385ef49de7bf80cb0740c7a73085d8a267c1f7f87978f1e8
                   331b870581b254a5a258c85465b3130d5153e68646b300265f8ea304f0fd0901
```

## Devices

Stages B and C of device identity, on top of "Device identity". Stage B
lets a copy sign in to a Delta profile with a short code instead of a key,
like Plex's or Jellyfin's Quick Connect, and gives every account a device
list with revoke. Stage C stops the player key doing new things once an
account no longer needs it. Old clients and old sites keep working: every
endpoint below is new, and the player key's endpoints stay.

**Sign-in by code (Quick Connect).** A copy with a device key and no
account (or one that signs in to another) asks for a code. Its player
approves the code where they're already signed in: on the site at
`/connect`, or from another signed-in device's LINK A DEVICE (in the Delta Guide). The device
asks its own player to confirm, then gets its certificate. The player key is
never sent or shown.

1. `GET /v1/identity/nonce`, then `POST /v1/identity/device/start
   {"device_key", "nonce", "signature", "platform", "build", "name"?,
   "client_id"?, "scopes"?}`,
   signed by the device key over `"chupathingy device start v1\n"` +
   audience + nonce (16) + device public key (32). `name` is what the
   device calls itself (printable ASCII, at most 32), shown when it is
   approved; the site defaults to the platform's name. `client_id` is the
   app the device belongs to, and `scopes` what it asks for (see
   [clients.md](clients.md)); without them, or with
   `"chupathingy"`, it is Chupathingy's own, as before. Our builds send
   `"chupathingy"`, on the enroll too.
   The answer: `{"code_nonce": 32 hex, "poll_token": 64 hex, "interval": 5,
   "expires_in": 600}`.
2. The device works its code out itself: 8 characters, each
   `ALPHABET[d[i] % 31]` for the first 8 bytes d of SHA-256(`"chupathingy
   device code v1\n"` + device public key + code nonce), with ALPHABET
   `23456789ABCDEFGHJKMNPQRSTUVWXYZ`. It shows them as ABCD-EFGH, with a QR
   code of `<site>/connect?code=ABCD-EFGH`. The site picks the nonce after
   it has seen the key, so someone in the middle who swaps in a key of
   their own gets a code that isn't the one on the screen. The alphabet,
   length and life are a console's Link Profile code's, so both go in the
   same box at `/connect`, under the same counts.
3. The player types the code in, signed in: `POST /v1/connect/claim
   {"code"}` on the site (a password session), or `POST
   /v1/identity/device/approve {"code"}` from another device with a token
   (capability `devices`). Either takes any code `/connect` takes. A
   device's code is shown first: 202 `{"shown": true, "handle",
   "confirm_seconds", "device": {"name", "platform", "build"}, "warning":
   "Only enter a code shown on your own screen."}`, so the page can say
   "Windows (Living room), build 0.9.1, wants to sign in as @handle". Then
   `POST /v1/connect/approve {"approve": true | false}` (the page) or
   `/v1/identity/device/approve {"approve":...}` (the device) approves it
   (202 `{"waiting": true,...}`) or denies it (`{"state": "declined"}`: the
   code is dead). The approver never sees which profile the device counted
   for before. A code is spent when it's typed, right or not.
   `/v1/connect/claimed` (the page) and `/v1/identity/device/approved` (a
   device) say how it went: `{"state": "shown" | "waiting" | "connected" |
   "declined" | "expired",...}`.
4. The device polls `POST /v1/identity/device/poll {"poll_token"}` every
   `interval` seconds: `{"state": "pending"}` (shown too), `{"state": "confirm",
   "handle", "previous"}`, `{"state": "connected", "handle"}`, or
   `{"state": "declined" | "expired"}` (an unknown token too).
5. On "confirm" it asks its player (A to sign in, B not), then `POST
   /v1/identity/device/confirm {"poll_token", "device_id", "nonce",
   "accept", "signature"}`, signed over `"chupathingy device confirm v1\n"`
   + audience + nonce (16) + device ID (16) + one byte (1 yes, 0 no) + one
   byte of the handle's length + the handle it showed. A yes is answered
   with a certificate (the certificate answer, plus `"state":
   "connected"`), a no with `{"state": "declined"}`. The player has 120
   seconds after the approval to answer. Asked again after a lost answer,
   a yes gets a new certificate and nothing is done twice, unless the
   device was signed out, revoked or locked since.

What signing in does on the site:
- A device enrolled with its player key (stage A) brings its copy: the
  copy's games from then on count for the profile, as Link Profile links
  them (its older games wait to be claimed), and the certificate follows
  the copy as before.
- A device with no copy (a new install the site hasn't seen, or a second
  account on a copy) is signed in to the account itself. Its certificate's
  account is the profile's player ID, its flags have no 0x01, and its
  token acts for the profile: the lines it confirms and the reports it
  sends count for the profile directly.
- A device signed out before signs in again with the same key. A revoked
  one can't: `/device/start` answers 403 `{"revoked": true}`, and the
  device makes a new key, once.

Limits: 10 codes an hour an address, and a device key (counted once its
signature checks); 5 answers a code and 30 an address in 10 minutes; 400
polls a code and 2000 an address in 10 minutes (a code is counted only
when there is one); a code lives 10 minutes and is good once;
codes, poll tokens and approvals are kept in memory only, as hashes. 400,
401 (`nonce`, `signature`), 429 and 503 as for the other identity paths.
`/v1/connect/claim`'s wrong-code counts cover these codes too.

**Signing out on the device.** `POST /v1/identity/device/signout
{"device_id", "nonce", "signature"}`, signed over `"chupathingy device
signout v1\n"` + audience + nonce (16) + device ID (16). The site marks the
device signed out (no more refreshes or tokens) and answers `{"ok": true}`.
The device drops its certificate and token and keeps its key, and until it
signs in again it sends the site nothing as that account, the player key
neither. Signing in again is a new code.

**The device list.** Every device of an account that isn't revoked: those
signed in to it by code, and those of the copies whose games count for it
(its own player ID and the IDs linked to it). Each is `{"id"` (the device
ID, 32 hex), `"name"`, `"platform"`, `"build"`, `"created"`,
`"last_seen"` (day precision, or null), `"legacy"` (enrolled with a
player key), `"console"`, `"signed_out"`, `"this"}` (the device asking),
this device first, then the latest seen; at most 32. No address, ever,
and no key.

| Request | Who | Answer |
| --- | --- | --- |
| `POST /v1/session/devices {}` | The profile page, signed in (a page the game opened may read it) | `{"devices", "legacy_key"}` |
| `POST /v1/session/devices/revoke {"device_id"}` | The page, a password session | `{"ok", "devices"}`; 404 for another account's |
| `POST /v1/session/devices/rename {"device_id", "name"}` | The page, a password session | `{"ok", "devices"}` |
| `POST /v1/session/devices/signout_all {"auth", "code"?}` | The page, with the password (and the two-step code) | `{"ok", "revoked", "devices"}`: every device revoked and every other session of the account ended; consoles are unlinked, computers' copies aren't |
| `POST /v1/identity/devices {}` | A device's token (`profile`) | `{"devices"}` |
| `POST /v1/identity/device/revoke {"device_id"}` | A device's token (`devices`) | `{"ok", "devices"}`; 409 `{"this": true}` for itself (it signs out instead); 404 for a console (the site revokes those) |
| `POST /v1/game/profile` | As before | `"devices"` beside `"copies"`, without `"build"`, at most 16, cut to fit like the rest |

- **Revoke** stops the device's tokens at once, refuses its refreshes, and
  puts it on the revocation list (CREV) until its last certificate has run
  out. The device alone stops: a player key it was enrolled with keeps
  working for the copy's other installs (an older build, an Xbox identity
  file); a key that leaked is turned off with the account's switch, below,
  and the page and the mail say so when such a device is revoked. A
  computer's copy is unlinked once no other live device of that copy is
  left (the profile's own player ID has no link to take). A console's link
  is its key, so a console revoked is always unlinked: from the site, down
  an approval chain, and by signing out everywhere. A console still linked
  is never left out of the list. The site sessions it opened
  (`/v1/link` with its token) end. Every device it approved by
  code goes with it, and theirs: the site keeps who approved each device,
consoles approved from a device included, so a stolen
  device leaves none behind. Revoked from another device rather than the
  site, a console down the chain is revoked and unlinked all the same.
  Revoking doesn't turn the player key back on (stage C, below).
- **Consoles.** A console linked by its own Link Profile
  (`/v1/console/link/*`, "Link Profile on a console") is in the list as a
  device with `"console": true`, its device ID made from its console key as
  any device's is. Its key is its link, so revoking it unlinks it as well.
  Linked again by code later, it comes back. Consoles don't hold
  certificates yet; that's the next step, and verified names on consoles
  wait on it.
- **Tokens** are checked against the device's capabilities as they are
  now, not only the token's own.
- **Mail and the log.** A device added by code, a device revoked, signing
  out everywhere and the player key turned back on are each told to the
  account's confirmed address, and kept in the admin log (by its profile
  name, never an address).
- **The game** shows the list under Delta Profile's DEVICES, also opened
  from the Delta Guide's SETTINGS: this device offers SIGN OUT, another device REVOKE
  (asked first). The Delta Guide's SETTINGS (and X in the list) also have LINK A
  DEVICE, where the player enters another device's code with the d-pad: 8
  characters, or an older copy's 6-character Link Profile code. A device's
  code shows the device and the warning first, then APPROVE or DENY.
  The Delta Guide's SIGN IN shows a Quick Connect code right in its row, and A there
  answers the question; Link Profile's dialog shows the same code with its
  QR code.
- **The server panel** shows a player's verified profile name (from proof
  of identity over Delta Peer) and nothing of their devices: no platform,
  device name or ID.

**Stage C: the player key winds down.** Once every copy of an account has
its own device, the account's player key stops doing new things. It never
stops a device, and nobody is locked out:
- Counted per install, not per key. A copy counts as moved once a device
  was ever enrolled with its key or brought it by code (kept for good, past
  any purge; a later revoke doesn't count it back), or it is a console in
  the list. It still counts as on the key while a game used its key after
  it moved (a request the site took, a minute or more after), within the
  last 30 days: another install of the same key. Only game requests the
  site took count (`/v1/game/*`, `/v1/link`, `/v1/connect/start`,
  presence, social); claims and reports, which take the key whatever, and
  refused requests don't, so off stays off. A copy that proved itself a
  console with its own key (`/v1/console/claim` signed by its console key)
  within 60 days isn't on the key; a player key's signed claim never says
  so, nor a self-reported platform. An account for which an Xbox identity
  file was made keeps its key on. A profile's own player ID whose key no
  game request ever used (a profile made on the site) isn't waited for;
  copies that reported games before this came in count as used. Automatic
  turns the key off only once at least one copy has moved. An account with
  one copy not moved keeps its key.
- With the key off, `/v1/game/*`, `/v1/link`, `/v1/connect/start`,
  `/v1/presence`, `/v1/social/*` and enrolling a new device with that key
  are refused: 403 `{"error": "Update Chupathingy to keep using your
  profile.", "legacy_key_off": true}` (plain text for the text paths).
  `/v1/claim` and `/v1/client_report` keep taking it, so stats never stop.
  A device enrolled already enrolls again as before.
- Each account has a switch, `POST /v1/session/legacy_key {"setting":
  "auto" | "on" | "off", "auth", "code"?}` (the password, and the code with
  two-step sign-in): automatic (above, the default), always on, or off now.
  The answer and `/v1/session/devices` carry `"legacy_key": {"setting",
  "on", "waiting", "used"}` (copies not moved yet; `used` false for an
  account none of whose copies ever used the key from a game, shown as
  "Not used"). The profile page has it under
  Account and security, and the admin page shows it. Off is also how a
  leaked key is turned off; On takes the password (and the two-step code)
  like any change. Any change that turns the key back on is mailed.
- The profile page no longer offers Install in game, Copy key link or
  Download for Xbox. The `halo://key/` handler, `/v1/session/console_key`
  and the Xbox's identity file keep working for whoever has one.
- A client with a certificate never sends the key to a site that speaks
  identity ("How a client falls back", step 3). One the site answers 403
  `revoked` or `signed_out` counts itself signed out, on disk, and sends
  neither key nor token until it signs in again by code. Sign-in by code
  falls back to the old Link Profile with the key only when the site's
  `/v1/identity/nonce` is a 404.

**More than one account on a copy.** Each Halo profile on a copy may sign
in to its own Delta profile. A copy has one device key per account it signs
in to: the copy's own (`delta_device.key`, the one stage A enrolls) and,
for each Halo profile that signs in elsewhere, its own
(`delta_device_<n>.key` and `.cert`, n from 1), signed in by code with no
copy. The site needs nothing more, since each key is a device of its own
account, and two accounts can't be tied together by a shared key.
`delta_accounts.txt` in the save root maps a Halo profile (by its save
folder's name) to n; a Halo profile not in it uses the copy's own. Builds of
0.9.1 keep the file and its parsing, and show one account per copy.

**Test vectors** (the device of "Device identity", its nonce as the code
nonce and the nonce, handle `milenko`):

```
device code         DVG69E5J
start signature     14a41005d7d1e060eeb46b1af3de5cae53771df0ccf627ceb6d5c54b9a1ff6b1
                    822ee2de4135bcaaa4a4ebf7b7cf743aa6cbb5ba00a107ef660a1f798e8abc08
confirm signature   0ad07709c097a5b192b6c0763ce5f0859ff801d3eccad5c1249b0ac07ebdfdbd
                    9e0500b05b6c9ca512eb860968a70b1be1166a8720af25fae633431ee7fd1a02
decline signature   0f0f2b8e63f0ef1136a0aefaf3f51437b5fd37fb2f82e4806271bb37b7f38402
                    d5ba02d1f96430bac33a6e201d034f0fe657f236fb84c77e6a737bf2991ab80f
signout signature   f579b26e3db2c34573a7a4e9842d85ef6646513dbf73706aeae41c825a447805
                    1dddea806dbe0ecfd7a39b803e3e520190750980b2c4ff3689f2c19c1a66e80b
```

## The doorbell

The Delta Guide asks the site for its friends every 20 seconds in the menus and
every minute in a game (`POST /v1/social/state`; the site takes 6 a minute
from a copy). The doorbell makes invites, friend requests, party changes
and friends coming online show up within about a second instead: the site
rings, and the game asks at once.

- **No new connection:** the doorbell rides internet play's signalling,
on the connections it keeps to our own brokers, the
  `*.chupathingy.org` lines of `brokers.txt`: `mqtt.chupathingy.org:1883`
  (halo-mqtt) and `doorbell.chupathingy.org:1883` (the site's host). The
  game uses up to 5 brokers at once, so the public ones stay. When the game
  isn't hosting, joining or browsing, the doorbell starts signalling with
  our brokers alone; anything else that starts it brings in the rest.
- **A doorbell per copy:** the site hands each linked copy its doorbell in
  the friends state (`"doorbell"`: 32 hex digits, also in an unchanged
  answer). It's the first 128 bits of HMAC-SHA256 under a key only the
  site has, over `doorbell:<the copy's player ID>:<week>`: nobody can work
  out another copy's doorbell, or whose a doorbell is. It changes every
  week, so one that got out goes stale. The site rings the doorbell it last
  handed each copy, so a ring at the turn of the week still reaches it, and
  the copy's next poll hands it the new one. The topic is
  `doorbell/<doorbell>`.
- **Listening:** on our brokers only, signalling's CONNECT carries the
  doorbell as its MQTT user name (no password; the client ID stays
  signalling's own), and subscribes to `doorbell/<doorbell>` at QoS 0. A
  new doorbell makes those two connections anew; the others never see it.
- **The brokers' ACL** on the signalling listener (1883, anonymous):
  `topic readwrite hceu/#` and `pattern readwrite hceu/#` (signalling, for
  clients with and without a user name: plain `topic` lines cover only
  clients without one), and `pattern read doorbell/%u`. A client reads
  only the doorbell that is its user name (mosquitto checks each message as
  it delivers it, so a wildcard or someone else's doorbell gets nothing,
  and names with `+` or `#` never match), and nobody on that listener can
  publish to `doorbell/`. The site rings through a second listener (8883,
  TLS, `per_listener_settings`) as `doorbell-site`, which may only write
  `doorbell/+`. Signalling on 1883 is unchanged for every client, OpenCE's
  included.
- **Ringing:** a ring is QoS 0, not retained, one byte: what changed (1
  friends, 2 invite, 3 party, 4 presence, 5 clan; anything else, or none,
  is a ring all the same). No handle, no name, no game. The site rings both brokers.
- **Who hears what:** the site rings only copies that asked for their
  friends in the last 3 minutes, at most 12 times a minute each. A friend
  request rings the one it went to (never when they block the sender: a
  block stays silent and rings only the blocker's own copies), becoming
  friends or a removal rings both, a decline rings only the one who
  declined, an invite rings the friend invited, and a profile coming
  online, going into a game or turning "appear offline" off rings its
  friends (never while it appears offline or hides what it plays; turning
  it on rings nobody, and friends see it on their next poll).
  A party invite rings the friend invited; someone joining, leaving or
  kicked out, a new leader, rule or code, and the leader's go ring the
  other members (and the one kicked out), with kind 3. A clan's invite
  rings the player invited, with kind 5.
- **On a ring:** the game polls at once, at least 2 seconds after the last
  poll, and only while fewer than 2 polls went out early for a ring and
  fewer than 5 went out at all in the last minute. The schedule's own
  polls never use up a ring's room, so a ring speeds things up in the
  menus too, and the menus' 3 a minute plus rings stay at 5 or fewer, so a
  join or an invite still fits. A ring never cuts short a wait the site asked for
  (429, an unlinked copy).
- **Failing:** our brokers fail and come back as signalling's always have
  (its retries). Without a doorbell the polls go on as before.
- **What a watcher sees:** signalling on 1883 is plain MQTT, as it always
  was: someone on the network path could see a copy's doorbell and when it
  rings, never what changed or for whom.
- **The setting:** `network.doorbell` (on by default; it changes nothing in
  a game). Off, or a copy not linked, no user name and no subscription.
  Consoles keep polling (they have no Delta friends yet).
- **The code:** `p2p_signal.c` (`p2p_signal_doorbell`), `p2p.c`
  (`p2p_doorbell`, `p2p_doorbell_rings`), `browser.c` and `social.c` (the
  ring's poll), and the site's `doorbell.py`. Tests: `tools/test_doorbell.py`
  (signalling against a local mosquitto with the brokers' ACL, when
  installed), `tools/test_social.py`, and puma's `test_doorbell.py`.

## Clans

Clans are groups of Delta profiles, kept by the game list. A clan has a name, a tag of 2 to 8 letters and digits, a
motto, an emblem and ranked members (leader, officers, members). Making a
clan, ranks, handing the clan to another member and the emblem are on the
site (`/profile#clan`, `/clans/TAG`). Inviting and kicking are on the site
and in the Delta Guide.

- **In the Delta Guide:** the CLAN section shows the clan's members by rank, each
  with where they are (in a game, online or offline, worked out as a
  friend's: hidden while they appear offline or hide their game). The
  leader and officers also see the invites they have out, and get INVITE
  FRIENDS (it opens FRIENDS, where a friend has INVITE TO CLAN) and, on a
  member of a lower rank, KICK (pressed twice). Everyone has LEAVE CLAN
  (pressed twice). With no clan, CLAN lists the clans' invites: JOIN CLAN
  and DECLINE. The clan's card (emblem, tag, name, motto) shows beside the
  player's own line.

- **In the game:** Delta Profile's CLAN section shows the profile's clan:
  its emblem, name, tag, member count and motto, then its members by rank
  (the first nine, then how many more). With no clan it lists the clans
  that invited the profile. Its A opens a dialog: SHOW MY TAG and LEAVE
  CLAN, or the invites (A joins, X declines). A leader leaves once another
  member leads, or when alone (the clan goes with them).
- **The emblem:** a background shape in one color and a foreground shape in
  two, from Halo's 18 armor colors. The pieces are the site's
  (`GET /v1/clans/emblem`); `port/linux/src/clan_emblem_table.h` is made
  from them by `tools/clan_emblem_table.py`, and `clan_emblem.c` draws
  them the way the site does.
- **Requests:** `/v1/game/profile` carries `clan` and `clan_invites`, and
  its privacy `show_clan_tag`; `POST /v1/game/clan` (the player key, an
  action: `accept`, `decline` or `leave` with the clan, `invite` or `kick`
  with a handle, or `roster`) answers with the same two, the members'
  handles and presence, and a leader's or officer's `clan_invited`. The
  friends poll (`/v1/social/state` with `"clan": true`) carries the same
  roster, so the Delta Guide spends no extra requests on it. A site from before
  these leaves them out, and the game shows what `/v1/game/profile` has.
- **Tags in games:** a verified player ("Verified names") who shows their
  clan tag is "[TAG] Name" on the scoreboard, the postgame report and the
  lobby, the name cut to fit with "...". The public card (`/v1/game/players`) carries
  the tag only while its owner shows it. OpenCE players and clients see
  nothing new.
- **Privacy:** SHOW CLAN TAG (Delta Profile's Privacy, and the clan dialog)
  is the site's own setting. Members who hide their service record are left
  out of the member lists others see, counted as hidden.
