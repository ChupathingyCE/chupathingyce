# Moderators and the control panel

Delta Control is how a ChupathingyCE Dedicated Server is run: its console,
its control API, its web control panel, in-game moderation over Delta Peer,
and, if you want it, a link to halo.milenko.org. All of it works without
the site. Nothing is on until you turn it on, and nobody is a moderator
until you make them one.

Player hosts (a game hosted from the menus) already have OpenCE's own kick
and ban, from the host's console; Delta Control is the dedicated server's,
and adds roles, accounts, an audit file, the control panel, moderators in
the game and the site link. Kicks and bans go through the same code as the
host's (`bans.txt`, the game's own protocol), so players need nothing new,
and OpenCE players are kicked and banned like anyone else.

## Roles

| | moderator | admin | owner |
| --- | --- | --- | --- |
| Status, players, bans, the log, the audit file, playlists, settings | yes | yes | yes |
| Warn a player (`sv_warn`) | yes | yes | yes |
| Kick (`sv_kick`) | yes | yes | yes |
| Ban for up to 7 days (`sv_ban <player> 2h`) | yes | yes | yes |
| Ban for longer, or for ever | | yes | yes |
| Unban (`sv_unban`) | | yes | yes |
| Maps and playlists (`sv_map`, `sv_mapcycle_next`, `sv_end_game`, `sv_playlist_*`) | | yes | yes |
| Settings and game types (`sv_name`, `sv_maxplayers`, `sv_set`, `sv_gametype_*`) | | yes | yes |
| Invite moderators to the control panel | | yes | yes |
| Roles: accounts, `sv_mod_*`, invitations of any role, `sv_link` | | | yes |

The server's console, its startup commands and the control API's tokens act
as the owner. The console alone manages tokens (`sv_admin_*`).

Every check is made where the command runs (the main thread), whichever
way it came: the console, the API, the control panel, a moderator's game,
or the site. Every change is written to `control_audit.log` in the data
folder, done or refused: when, who, how (console, api, web, game, site),
their role, what, to whom, why. Never an address. Each person may make 20
changes a minute (10 at once).

## Who a moderator is

The game's player key (`game_list_player.key`, in each copy's save folder)
never leaves the player's machine except to halo.milenko.org. From it the
game makes a second key, an Ed25519 key pair, whose public half is the
player's **moderator key**: 64 hex digits. A server knows a moderator by
that key, and a ChupathingyCE game proves it holds the key (a signature of
a challenge the server makes for each session, and of each action) when
its player signs in from the game's Moderation screen. Names can be faked
and hardware ids can be claimed; a moderator key cannot be.

A player's game shows only when its player signs in (it is the same key on
every server), so other servers never learn it.

## Setting up moderators without the site

### The moderators file

`moderators.txt` in the data folder, a line a person:

```
# role       moderator key                                                      name (for display)
moderator    72f7b19d4735618403dab4fe9e4c8c0d13e510709552140a17cd9b9ed2a798ec  Odb718
admin        3f2a9c1b5d0e7a64b2c8f19d0e6a5b4c3d2e1f0a9b8c7d6e5f4a3b2c1d0e9f8a7  Mega
```

Roles: `moderator`, `admin`, `owner`. Lines beginning with `#` are
comments. Edits by hand take effect within a second; a line the server
cannot read is left out, and the commands below refuse to rewrite the file
until it is fixed.

### The console's commands

| Command | What it does |
| --- | --- |
| `sv_mod_list` | The moderators file's moderators. |
| `sv_mod_add <player> <role> [name]` | Gives a player in the game a role, by the key their game proved (they sign in from the game's Moderation screen first: `sv_players` shows their key). |
| `sv_mod_add <moderator key> <role> [name]` | The same, with the key itself (a moderator can read theirs on the Moderation screen). |
| `sv_mod_remove <key or name>` | Takes a moderator out (the key's first 8 or more digits will do). |
| `sv_warn <player> <reason>` | A warning in the player's game (a ChupathingyCE game shows it; any warning is in the audit file). |
| `sv_kick <player> [reason]`, `sv_ban <player> [duration] [reason]` | As before, with a reason. A ChupathingyCE player sees it as their game drops them. |

### In the game

A moderator in a ChupathingyCE game opens the Moderation screen (Y on a
controller, or M, from the pause menu) on a server that offers it, signs in
(A), and picks a player and an action: warn, kick, ban for an hour, a day,
7 days, or for ever, end the game, next map. Only the actions their role
allows are shown, and the server checks each again. OpenCE games cannot
sign in (they cannot prove a key); they can be warned (in the audit file
only), kicked and banned like anyone.

## The control panel

The web page on the control API's port: status, players (warn, kick, ban,
with reasons), maps, playlists and game types, bans, the log, the audit
file, settings, the people who run the server, and one's own account. Each
role sees what it may do.

### Turning it on

```sh
HALO_DEDICATED_CONTROL=8080            # 127.0.0.1:8080: this machine only (an SSH tunnel)
HALO_DEDICATED_CONTROL=0.0.0.0:8443    # every address: HTTPS (below)
```

### The first owner

The first time the panel is on, with no owner yet, the server prints a
setup link, once, on its console (never in `debug.txt`):

```
ChupathingyCE Dedicated Server: the control panel has no owner yet. Open

    https://<this server's address>:8443/#setup=set_4f0c...

(through your SSH tunnel, or at the server's address) and make the owner's account
with this one-time code. It is good for 24 hours, and until the account is made;
sv_account_setup on this console makes a new one.
```

Open it, choose a name and a password (12 characters or more), log in, and
set up a second factor (My account: scan the QR code with an authenticator
app). No file to edit.

### Accounts and invitations

Each person has their own login. An owner (or an admin, for moderators)
makes an invitation in People; it is a link, good once, for 48 hours.
Send it privately; the person opens it, chooses their name and password,
and sets up their second factor. From People an owner changes roles, takes
accounts out (their sessions end at once), makes a reset link for someone
who lost their password or authenticator (it sets a new password and turns
the second factor off), and may require a second factor of everyone (an
account without one can then do nothing but set it up).

On the console: `sv_account_list`, `sv_account_invite <role>`,
`sv_account_role <name> <role>`, `sv_account_remove <name>`,
`sv_account_reset <name>`, `sv_account_setup`.

The accounts are `control_accounts.txt` in the data folder (readable by its
owner alone): passwords as salted Argon2id hashes; second factors' secrets
as they are (an authenticator needs them). Back it up with the data folder,
and keep the backup private.

### One person in the panel and in the game

In My account, an account can be bound to its person's game: join the
server, find your number in Players, and "Ask my game"; your game asks you
to confirm (A), and proves your moderator key over Delta Peer. From then on
your game has your account's role on this server.

### Remote access, safely

On 127.0.0.1 (the default) the panel is plain HTTP for this machine alone:
reach it through an SSH tunnel (`ssh -N -L 8080:127.0.0.1:8080
you@server`), or a TLS reverse proxy ([admin.md](admin.md)).

On any other address the panel is HTTPS only:

- **The server's own certificate** (the default): made once, ECDSA P-256,
  kept as `control_tls.crt` and `control_tls.key` (readable by its owner
  alone) in the data folder. No authority vouches for it, so the browser
  warns once; the server prints the certificate's SHA-256 fingerprint at
  start and in its log, and the login page shows the one the browser got.
  Compare them (the browser's padlock, then the certificate's details)
  before typing a password, then accept it.
- **Your own certificate** (Let's Encrypt's, from certbot, or any):
  `HALO_DEDICATED_CONTROL_CERT=/etc/letsencrypt/live/example.org/fullchain.pem`
  and `HALO_DEDICATED_CONTROL_KEY=.../privkey.pem` (readable by the server's
  user). A renewal is taken within a minute, without a restart. The server
  does not run ACME itself: it would need port 80 open and a domain name,
  and certbot does it well.

`HALO_DEDICATED_CONTROL_TLS`: `auto` (the default: HTTPS beyond the
loopback address), `on` (always), `off` (never: refused on a public
address; on a private one, such as Tailscale's 100.x addresses or Docker's
network behind `-p 127.0.0.1:...`, it is plain HTTP with a warning).

The panel speaks TLS 1.2 with ECDHE and AEAD suites only.

## The link to halo.milenko.org (optional)

Link the server to your account on the site, and you can give other
people on the site a role on it there, and run it from the site's Servers
page. The server dials out to the site: no port to open, and it works
behind any router.

1. On the server's console (or the panel's Settings, as owner): `sv_link`.
   The server prints a code.
2. Sign in on halo.milenko.org, open Servers, Link a server, and enter the
   code (good for 10 minutes); confirm.
3. On the server's page on the site, give other accounts a role: moderator
   or admin. Their games' moderator keys (from Link Profile) come with the
   role, so they are moderators in the game too.

`sv_link_status` tells whether it is linked, to whom, and when the site was
last heard from. `sv_unlink` (or Unlink on the site) unlinks it: the
server's credential (`delta_link.key` in the data folder) is deleted at
once, and the site is told.

What the site may do: the commands of the table below, as its accounts'
roles allow, checked by the site and again by the server, and never more
than an admin's (`HALO_DEDICATED_LINK_ROLE`: `admin`, or `moderator` to
allow less). The site cannot change roles on the server, its files,
accounts or tokens. Every command from the site is in both the site's
audit log and the server's audit file.

| From the site | |
| --- | --- |
| `sv_warn`, `sv_kick`, `sv_ban`, `sv_unban` | with a reason |
| `sv_map`, `sv_mapcycle_next`, `sv_end_game` | |
| `sv_name`, `sv_maxplayers` | |

`HALO_DEDICATED_LINK=false` turns the link off for good: `sv_link` refuses,
and the server never asks the site. `HALO_DEDICATED_LINK_URL` points it at
another site (HTTPS only).

## Security notes

**What is exposed.** With nothing set: nothing (the console only). With
`HALO_DEDICATED_CONTROL`: one TCP port, on 127.0.0.1 unless you say
otherwise, HTTPS beyond the machine. With Delta Peer (on by default, the
game's own UDP 5160 beside 5150): signed moderation messages from games
already in the server's game. With the link: outbound HTTPS to the site.

**What it defends against.**

- Guessing passwords and codes: Argon2id (19 MiB, 2 passes) for every check,
  at most 30 checks a minute for everyone; five wrong from an address and
  it waits five minutes; five wrong for an account and it is locked a
  minute, doubling to an hour. Addresses are counted by a keyed hash, never
  kept. The setup code and invitations are 128-bit, single use, and expire.
- Stolen sessions: HttpOnly, SameSite=Strict cookies (Secure over HTTPS),
  30 minutes idle, 12 hours at most, a CSRF token on every change, the
  page's origin checked, a strict Content-Security-Policy.
- A leaked password: a second factor (TOTP, each code once), which the
  owner can require of everyone.
- A modified game claiming to be a moderator: it must sign with the
  moderator key; names and hardware ids are never trusted for roles. A
  server relaying another server's challenge is refused by the game
  (the challenge carries the server's invite).
- A compromised site: it can do no more than its accounts' roles allow,
  capped at admin, and only the commands above; the server's owner unlinks
  it with one command.
- Spam: each person's changes are limited, and Delta Peer limits each
  game's messages.

**What it does not defend against.** Someone with a shell on the server, or
the data folder's files (they hold the accounts' hashes and second factors'
secrets, the link's credential and the TLS key). A moderator acting badly
within their role (the audit file shows it). A LAN game is not encrypted;
internet play goes through the invite tunnel, which is.

**Defaults.** The panel and API off; when on, the loopback address; no
default password; no moderators; no link.
