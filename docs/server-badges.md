# Server badges

A server badge marks a dedicated server in Online Games and on the site:

- **Official server**: one of ours, the [C] servers.
- **Developer server**: a server run by a Chupathingy developer.
- **Community server**: a server run by a recognized community member or group.

A badge can't be copied. It is a certificate signed with our badge key for
one server's own key, and the server proves it holds that key every time it
is listed and every time someone joins. Badges never come from an address
or a server name.

## For hosts

1. Link your server to your profile (`sv_link`).
2. On your server's page on the site, ask for a badge.
3. Once it is approved, download `server_badge.cert` from the same page.
4. Put it in the server's data folder. The server picks it up within a few
   seconds.

Linking the server again makes a new key, so the old certificate stops
working. Ask for a new one after relinking. A certificate is good for a
year.

## The certificate

Format 1, little-endian:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `CBDG` |
| 4 | 1 | format, 1 |
| 5 | 1 | tier: 1 official, 2 developer, 3 community. Another number is a tier a reader doesn't know: the certificate is read and shows no badge |
| 6 | 1 | label length, 0 to 31 |
| 7 | 1 | reserved, 0 |
| 8 | 4 | issued, Unix seconds |
| 12 | 4 | expires, Unix seconds (after issued) |
| 16 | 32 | the server's Delta Control link key (Ed25519 public key) |
| 48 | L | the label, printable ASCII: a developer's handle or a group's name |
| 48+L | 64 | a badge key's Ed25519 signature of `chupathingy server badge v1\n` and the bytes before it |

A certificate is 112 to 143 bytes. Its ID, which the revoked list names, is
the SHA-256 of all of it. In `server_badge.cert` it is one line of hex;
blank lines and lines starting with `#` are skipped.

Readers accept a certificate issued up to a day ahead of their clock, and
none at or after its expiry.

## The proofs

A certificate is public: the server hands it to everyone who joins. Holding
one proves nothing, so the server also signs with the key the certificate
names:

- **Listing.** A dedicated server with a good certificate adds
  `badge_cert` (hex), `badge_time` (Unix seconds) and `badge_signature`
  (hex) to each announcement. The signature is of
  `chupathingy server badge announce v1\n`, the invite in lowercase, `\n`,
  the time in decimal and `\n`. The site takes it only within 5 minutes of
  its clock, from a dedicated server, with a certificate that is a badge
  key's, in date and not revoked. A developer or community certificate
  also needs the server still linked, and a developer certificate needs
  its owner to still hold the Developer profile badge. The list then says
  `badge` and `badge_label` (`/v1/games`, and `/v1/games.txt` fields 24
  and 25 with `?fields=25`).
- **Joining.** Over Delta Peer (`server_badge`, BADGE_ASK and BADGE in
  [delta.md](delta.md), "Server badges"): the client's fresh nonce, signed
  with the same key as `chupathingy server badge proof v1\n` and the nonce.
  A host that fails the check shows no badge from then on.

A build shows a badge only from the list, and only with badge keys of its
own. It drops the badge of a host that does not prove it on joining.

## The keys

The badge key set is a primary and a recovery Ed25519 pair, made by the
command repository's Keys workflow (set `badge`). The private halves live
only in its `signing` and `signing-recovery` environments. The public halves
are in:

- command's `keys/badge.pub.json`;
- this repository's `port/linux/src/server_badge_key.h` (all zero until the
  set is made: such a build takes no certificate and shows no badge);
- the site, as puma's `--badge-keys`.

Certificates signed with either key are taken. To rotate, add the new key
beside the old one, release, then sign with the new key once the builds in
use have it.

## Issuing and revoking

The command repository's **Issue server badge** workflow signs one
certificate for a link key, tier, label and lifetime (a year by default,
two at most). The site starts it when an admin approves a request, then
takes the certificate back at `POST /v1/badges/issued` (only for an
approved request of the same key, tier and label). It can also be run by
hand and the certificate uploaded in the admin panel. Official badges are
issued by hand only.

The site's revoked list (`/v1/badges/revoked.txt`) names certificate IDs,
or `key <hex>` for every certificate of a server key. A server unlinked,
a Developer profile badge taken away, or an admin's revoke adds to it.

## Where it is shown

- Online Games: a tag after the server's name, the badge on the Host line,
  and the label on a "Run by" line.
- The lobby's host text: OFFICIAL SERVER, DEVELOPER SERVER or COMMUNITY
  SERVER.
- The site's game list.

Console builds (Warthog) don't show badges yet. Their list and Delta Peer
client need the same check first.

## The test certificate

Command's `tools/test_server_badge.py` makes it byte for byte, and this
repository's `tools/test_server_badge.py` and puma's
`test_server_badges.py` read it. It is a developer certificate labeled
`Milenko`, issued 1790000000 and good until 1821536000, for the server key
from the seed SHA-256(`chupathingy test server key`), signed with the test
badge key from the seed SHA-256(`chupathingy test badge key`) (public key
`a32edf54a17391184c72e23278f2908175f62951817ffdd7db8fc61bd0d074fd`).
Neither key is ever a real one.
