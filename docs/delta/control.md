# Delta Control

Everything that runs a dedicated server: its `sv_` command table, its
console, its control API and control panel (HTTP on the loopback address,
HTTPS beyond it), in-game moderation over Delta Peer, and the optional link
to the site. The operator's guide is `server/docs/moderation.md`. Its rule:
only the command table runs, never a shell; every change is checked against
a role and audited, on the server and (for the site's) on the site.
Player-hosted games keep OpenCE's own kick and ban; Delta Control is the
dedicated server's.

## Roles

Roles, lowest first: none (0), moderator (1), admin (2), owner (3). The
permission bits are the same on the server, in Delta Peer's MOD_STATE and in
the site's role lists:

| Bit | Permission | Roles |
| --- | --- | --- |
| 0x001 | view: status, players, bans, log, audit, playlists, settings | moderator, admin, owner |
| 0x002 | warn | moderator, admin, owner |
| 0x004 | kick | moderator, admin, owner |
| 0x008 | ban for up to 7 days | moderator, admin, owner |
| 0x010 | ban for longer, or for ever | admin, owner |
| 0x020 | unban | admin, owner |
| 0x040 | maps, the playlist, the game | admin, owner |
| 0x080 | settings, game types | admin, owner |
| 0x100 | roles: accounts, moderators, the link | owner |
| 0x200 | invite roles below one's own | admin, owner |
| 0x400 | the console's own (tokens, accounts) | the console |

The console, startup commands and control API tokens act as the owner. A
command's permission is decided by `control_command_permission`,
checked by the control thread before
it queues a request and again by the main thread before it runs it.

## Identity

A moderator is known by a **moderator key**: an Ed25519 key pair whose seed
is SHA-256("halo-ce-universal moderator key\n" || player key), the player
key being the game list's (`game_list_player.key`, never sent to a game
server). The public key (64 hex digits) is the identity. A game proves it
with Delta Peer's `moderation` capability (see Delta Peer, Moderation);
names and hardware ids never give a role. The site derives the same key
whenever a game sends it its player key, and keeps the public key on the
profile.

A key's role is the highest of: the server's `moderators.txt`; a control
panel account bound to the key (the server asks the game over Delta Peer,
its player confirms); the site's role list, when the server is linked,
never above `HALO_DEDICATED_LINK_ROLE` (admin unless set).

A verified player (Delta Peer's `identity`, "Verified names") is also known
by their Delta account, when their proof is bound to the server. A
machine's role is the highest of its key's and its bound, verified
accounts' (those whose certificates have `moderate`). An account's role
comes from `moderators.txt`'s account lines (`<role> <32 hex digits>
[name]`) and from the site's role list by each entry's account, under the
same cap. Bans can name accounts and devices too ("Verified names").

## Accounts and remote access

The control panel has an account for each person: a name, an Argon2id
password hash, a role, an optional TOTP second factor (RFC 6238, SHA-1,
30 s, 6 digits; the owner may require it), an optional bound moderator key.
The first owner is made from a one-time setup code the server prints on its
console; others from invitations (single use, 48 hours, hashes kept). Wrong
passwords and codes back off per account (a minute after five, doubling to
an hour) and per address (counted by a keyed hash, never kept). Sessions are
HttpOnly, SameSite=Strict cookies (Secure over HTTPS) with CSRF tokens.

Beyond the loopback address the panel is HTTPS only (Mbed TLS, TLS 1.2,
ECDHE and AEAD): the server's own ECDSA P-256 certificate, whose SHA-256
fingerprint it prints and the login page shows, or the operator's (PEM,
reloaded on renewal). No ACME client: it needs port 80 and a domain, and
certbot's files are taken as they are.

## The link to the site

Optional and outbound: the server dials the site; no port is opened.

1. `sv_link`: the server makes an Ed25519 key pair and a 32-byte secret,
   and sends the public key and the secret over HTTPS (the site's
   certificate checked): `POST /v1/control/link/start` `{"public_key",
   "secret", "name", "version"}` answers `{"code", "expires", "token"}`.
2. The owner signs in on the site and enters the code at `/servers/link`
   (good 10 minutes, once). The server asks `POST /v1/control/link/status`
   `{"token"}` every 3 s until `{"state": "linked", "server_id", "owner"}`,
   and keeps `delta_link.key` (0600).
3. Then every few seconds, `POST /v1/control/poll`, signed:
   `{"server_id", "time", "nonce", "payload", "signature"}`, the signature
   Ed25519 over `"delta control request v1\n" + path + "\n" + server_id +
   "\n" + time + "\n" + nonce + "\n" + payload`; the site refuses a time more
   than 300 s off, a nonce seen before, a revoked server. The payload has the
   server's state (name, map, players' names and numbers; never addresses or
   hardware ids) and the results of the commands it ran.
4. The site answers `{"payload", "mac"}`, the MAC BLAKE2b-256 keyed with the
   secret over `"delta control response v1\n" + nonce + "\n" + payload`; the
   server takes nothing from an answer whose MAC is wrong. The payload has
   the commands the site's accounts sent (`sv_warn`, `sv_kick`, `sv_ban`,
   `sv_unban`, `sv_map`, `sv_mapcycle_next`, `sv_end_game`, `sv_name`,
   `sv_maxplayers`, with a reason) and, when it changed, the role list
   (handles, roles, their profiles' moderator keys).
5. Each command is authorized on both sides: the site by the account's role
   on that server, the server by its copy of the role list, its own
   permission table and its cap. Both audit it.
6. Unlinking: `sv_unlink` deletes the credential at once and tells the site
   (`POST /v1/control/unlink`); unlinked on the site, the next poll's MAC'd
   answer says so and the server deletes it. `HALO_DEDICATED_LINK=false`
   turns the link off for good.

7. Newer servers add to the poll (an older site ignores them, an older
   server sends none, and the site's answer keeps its fields):
   - `status.locked` and `status.late_join_hold` (booleans);
   - `reports`: the reports taken since the last poll the site
     acknowledged, at most 20 a poll, each `{"ref", "time", "reason",
     "target": {"name", "handle", "account"}, "reporter": {"name",
     "handle", "account"}, "map", "game_type", "clip", "evidence"}`. `ref`
     is 32 hex digits, `clip` the clip token (32 hex digits) or null,
     `handle` and `account` null for a player who proved no one,
     `evidence` the refusal counts (`{"movement", "vehicle", "hits",
     "clock"}`). Never a report about an OpenCE player. The answer's
     `reports_taken` lists the refs the site kept and `reports_refused`
     those it never will; the rest go again.
   - `bans`: when the server shares its bans, `{"digest",
     "entries"}` with every ban in bans.txt that names an account or a
     device, each `{"ref", "accounts", "device", "handles", "reason",
     "time", "until"}` (`until` 0 for ever). Never an address or hardware
     ID. It's sent only when its digest isn't the answer's last
     `bans_digest`; `{"digest": "", "entries": []}` withdraws them. The
     answer's `bans_sharing` says whether the owner shares that server's
     bans on the site.
   - `shared_bans_version`: the version of the subscribed lists the server
     holds. When the site's isn't that, its answer has `shared_bans`:
     `{"version", "entries"}`, each entry `{"list", "list_name", "ref",
     "accounts", "device", "handles", "reason", "time", "until",
     "source"}` (`source`: the name of the server that banned).

HTTPS requests in the server go one at a time across its threads (Mbed TLS
is built without locks), so polls are short; long polling waits for a build
of Mbed TLS with its threading on.

## Shared ban lists

A linked server's owner may share its bans and take other servers'. Both
are off until the owner turns them on, and both only go by Delta account
and device, never by address or hardware ID.

- **Sharing.** `ban_share` (`sv_set ban_share 1`, or "Share my bans" for
  each server on the site's My servers) sends the server's bans that name
  an account or device ("Verified names") to the site. They go into the
  owner's own list. The owner picks who may subscribe to it: servers they
  name, or every server, once a site admin marks the list trusted.
- **Subscribing.** On My servers, an owner picks which lists each of their
  servers takes: their own, those shared with that server, and the trusted
  ones. The site sends the entries with the server's poll. The server keeps
  them in `shared_bans.cache` (with the link secret's MAC, as the role
  cache) and checks each machine whose players prove an account or device
  against them, as it checks bans.txt. A match is kicked with "Banned on
  <list name>".
- **The server decides.** `sv_banlist` and the panel's Bans show the
  shared entries beside bans.txt's, with their list and the server that
  banned. `sv_unban` on one makes an exception on this server alone
  (`shared_unbans.txt`); the list keeps it. Taking a ban out of bans.txt on
  the server that made it takes it out of its list with the next poll.
  `sv_ban_lists` shows the lists the server takes and when they came.
- **Bans from the site.** `sv_ban_account <account> [duration] [reason]`
  bans a Delta account that isn't in the game (a bans.txt line with only
  `account=`), so a moderator can act on a report after the player left.
