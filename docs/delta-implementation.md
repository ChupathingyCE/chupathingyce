# Delta in Chupathingy

How Chupathingy's own game and dedicated server implement Delta: the
settings, files, menus and calls. The protocol itself, the same for every
implementer, is the Delta spec in [docs/delta/](delta/README.md); this page
links to it rather than repeating it.

## Where the code is

| Part | Files |
| --- | --- |
| Delta's numbers (major, capability and platform registries, platform policy, wire, the legacy versions) | `port/linux/include/delta.h` |
| Delta Peer's wire format | `port/linux/src/delta_wire.c`, `delta_wire.h` |
| Delta Peer's sessions (host and client) | `port/linux/src/delta_peer.c`, `delta_peer.h`; the game's side, `delta_peer_game.c` |
| The advertisement's tag, wire ranges, the join mark | `port/linux/src/delta_compat.c`, `delta_compat.h` |
| The signed table: loading, checking, relaying | `port/linux/src/delta.c`, `delta_key.h`, `tools/delta_table.py` |
| Map identity | `port/linux/src/delta_maps.c`, `port/linux/include/halo_map_families.h` |
| Chat, moderation, voice, profiles | `delta_chat*.c`, `delta_moderation.h`, `voice*.c`, `delta_profile.c` in `port/linux/src` and `port/linux/game` |
| Delta List, Stats and Link (HTTP) | `port/linux/src/browser.c`, `event_log.c`, `event_upload.c`, `identity.c` |
| Delta Control | `server/src`, `server/platform` |

Every parser has unit tests and a seeded random-input test
(`port/linux/tests/delta_test.c`, `delta_compat_test.c`, run by
`tools/test_delta_peer.py` and `tools/test_delta_compat.py` in CI), with
libFuzzer harnesses (`delta_fuzz.c`, `delta_compat_fuzz.c`) where clang can
link them. `tools/test_delta_big_endian.py` runs the same tests on a
big-endian machine under QEMU. `tools/test_delta.py` checks `delta.h`'s
tables and `halo_port_limits.h` against each other, and
`tools/test_delta_registry.py` checks [the registry](delta/registry.json).
The reference client for ports, `tools/delta-ref`, is checked against
`delta_wire.c`'s own bytes by `tools/delta_ref_capture/capture.py`.

## Notes by spec section

Where Chupathingy's side of each part of the spec lives. These notes used to
sit in the spec pages; they are about this implementation only.

- **[peer.md Chat](https://github.com/ChupathingyCE/delta-spec/blob/main/peer.md#chat).** The game's side is `port/linux/game/chat.c` (keys, the log, routing, the bridge), `port/linux/src/delta_chat.c` (the filter and the host's rules) and `port/linux/src/delta_chat_bridge.c` (OpenCE's entries, the text's conversion, the bridge's checks), both tested in `delta_test.c`, and the dedicated server's `sv_say`, `sv_chat`, `sv_mute` and `sv_unmute`.
- **[peer.md Team changes](https://github.com/ChupathingyCE/delta-spec/blob/main/peer.md#team-changes).** The game's side is `port/linux/game/team_change.c`, its pause menu row in all the menu sets, and the dedicated server's settings and `sv_team`.
- **[peer.md Host rules](https://github.com/ChupathingyCE/delta-spec/blob/main/peer.md#host-rules).** The game's side is `port/linux/game/host_rules.c` and `port/linux/game/late_join.c`.
- **[peer.md Reports](https://github.com/ChupathingyCE/delta-spec/blob/main/peer.md#reports).** The game's side is `port/linux/game/report_player.c` (the Delta Guide's PLAYERS HERE and the scoreboard's REPORT), the dedicated server's `server/src/server_reports.c` (`sv_reports`, the control panel's Reports).
- **[peer.md Server badges](https://github.com/ChupathingyCE/delta-spec/blob/main/peer.md#server-badges).** The game's side is `port/linux/src/server_badge.c` and `port/linux/src/delta_peer_game.c`; the dedicated server's certificate is `server_badge.cert` in its data folder.
- **[peer.md Voice](https://github.com/ChupathingyCE/delta-spec/blob/main/peer.md#voice).** The game's side is `port/linux/game/voice_game.c` (the channels, the host's rules and its view of the game), `port/linux/src/voice.c` (the microphone, Opus and the voices' playback, through the game's mixer, `dsound_sdl.c`) and `port/linux/src/voice_jitter.c`; Delta Peer's checks and routing are in `delta_peer.c`, all tested in `delta_test.c` and `voice_test.c`. A dedicated server relays without any audio or Opus of its own. The consoles leave voice out for now: a console is never sent any.
- **[peer.md Party teams](https://github.com/ChupathingyCE/delta-spec/blob/main/peer.md#party-teams).** The host's side is `port/linux/game/party_teams.c` (which change: the plan in `port/linux/src/party_teams_plan.c`, tested by `tools/test_c_units.py`); the members' tag comes from `port/linux/game/guide.c`.
- **[peer.md Object sync](https://github.com/ChupathingyCE/delta-spec/blob/main/peer.md#object-sync).** It is offered by the builds with the game list (`HALO_GAME_BROWSER`: the desktop builds, Android, the Switch and the dedicated server), not by the original Xbox's. The game's side is `port/linux/game/network_objects.c`.
- **[stats.md Medals](https://github.com/ChupathingyCE/delta-spec/blob/main/stats.md#medals).** The names and icons are the site's too, one set for both. `tools/medal_assets.py`, run by hand with a checkout of ChupathingyCE/puma, reads `site.js`'s tables (`MEDALS`, `TIERS`, `SHAPES`, `GLYPHS`), draws each icon as the site's `medalSVG` does, and writes `port/assets/medals` (`medals.json` and an atlas of 64 pixels a medal, which the builds embed, and one of 32 for a console later) and `port/linux/src/medal_icons.h`. puma's `test_medals_sync.py` keeps the digest of the tables the game was made from, so a change on the site says to run it again; `tools/test_medal_rules.py` checks every key of `medal_rules.h` has its entry and icon.
- **[list.md Pings before joining](https://github.com/ChupathingyCE/delta-spec/blob/main/list.md#pings-before-joining).** `port/linux/tests/ping_test.c` tests the formats with hostile input, the host's limits, the prober's measuring, the anchors and the Ping column's order; `HALO_NETWORK_TEST=browse` opens Online Games and logs each game's ping, for two copies on one machine.
- **[social/chat.md](https://github.com/ChupathingyCE/delta-spec/blob/main/social/chat.md).** The host's filter uses the network's blocked names list, built in, plus a player's own word file on their client (`chat.filter_file`, in the save folder).
- **[social/invites.md The doorbell](https://github.com/ChupathingyCE/delta-spec/blob/main/social/invites.md#the-doorbell).** The doorbell rides the MQTT connections internet play's signalling already keeps to Chupathingy's own brokers (`port/linux/src/p2p_signal.c`); no new connection is opened.
- **[social/presence.md](https://github.com/ChupathingyCE/delta-spec/blob/main/social/presence.md).** The game also feeds the same status to Discord's rich presence on the player's own machine (`port/linux/src/discord_activity.c`). That is local and outside the spec.
- **[social/voice.md](https://github.com/ChupathingyCE/delta-spec/blob/main/social/voice.md).** Voice is on Windows, macOS, Linux and Android; consoles are never sent voice for now. A bridge to another project's voice is planned as a clean-room spec, not built.

## The advertisement in the code

The tag and the old layout ([peer.md](delta/peer.md#the-advertisement)) are
written and read by `port/linux/src/delta_compat.c`. Its offsets are
`DELTA_ADVERTISED_TAG_OFFSET` (17) and `DELTA_ADVERTISED_TAG_SIZE` (9); the
old layout's are `DELTA_ADVERTISED_WIRE_OFFSET` (3) and
`DELTA_ADVERTISED_RANGE_OFFSET` (20), with `DELTA_ADVERTISED_RANGE_SIZE`
(6). `delta_compat_advertise` writes the tag; `delta_compat_advertise_old`
writes the old layout's major and slot where they don't clash (a dedicated
server, in a game whose engine isn't `DELTA_ADVERTISED_KIND_ENGINE`,
Slayer); `delta_compat_decide` is a client's join decision, by
`DELTA_WIRE_COMPATIBILITY`'s ranges; `delta_compat_join_mark` and
`delta_compat_join_marked` write and read the join mark. The consoles take
the same path (`HALO_CONSOLE_DELTA` or `HALO_CONSOLE_ONLINE_GAMES`).
`delta_compat_read_project` reads the tag's layout 2, a host of another
project's code (`DELTA_PROJECTS` in `delta.h`): such a host speaks Delta, so
the client says HELLO and marks its join, but `delta_compat_decide` leaves
it to the network number, and the system link list shows the project's
name (`network_game_client_advertised_game_project`). This build never
writes layout 2.

## The protocol setting

`network.protocol` in config.toml (`HALO_NET_PROTOCOL`):

- `"auto"` (the default): the behavior above.
- `"opence"`: Delta Peer off entirely: no socket, no flag, no handshake; the
  machine is an OpenCE machine to everyone.
- `"delta"`: the same as auto. The browsers label each listed game Delta
  or OpenCE by what its host announces.

## Platform numbers in a build

A build sends the platform it was built for: Windows, Mac, Linux (Steam
Deck when Steam sets `SteamDeck=1`), Android, FreeBSD, iOS, Apple TV and the
console builds pick their own, and anything else sends `unknown` (0). A
port built from this repository names its own with
`-DHALO_DELTA_PLATFORM=_delta_platform_<name>` (`_delta_platform_ps3`, say).
New numbers: [registry.md](delta/registry.md#how-to-ask-for-a-number).

The policy rows are `DELTA_PLATFORM_POLICY` in `delta.h`, and every place
that names a platform reads `DELTA_PLATFORM_NAMES` there. Overrides, on
purpose:

- `network.platform_limits = "off"` on the weaker machine sets its key's
  opted-in flag: it accepts whatever the host runs. The default is `"on"`.
- `network.host_platform_limits = false` (`HALO_NET_HOST_PLATFORM_LIMITS` on
  a server) has a host ignore the caveats, co-op's included, for testing.
- The scoreboard shows platform logos only in a game played over Delta Peer,
  and none for an unknown platform (`display.scoreboard_platforms`).
- A copy shares its profile over Delta Peer only with
  `network.share_profile`.

The signed table's `platform_policy` section is reserved; no build reads it
yet (the hook is `delta_peer_platform_policy()` in `delta_peer_game.c`).

## Local override

`network.legacy_table` in config.toml (`HALO_LEGACY_TABLE` for servers)
names an unsigned file in the same format (beside config.toml unless a full
path), for testing and for an admin who knows better. Its row for the build's
wire is used as it is (it may narrow too), with its layouts; its serial is
not checked. It logs a warning at start, and while it is set no signed table
is fetched, taken or relayed. A file that can't be read, or has no row for
the wire, leaves the built-in numbers in use (and says so).

This is how one server announces a newer number than the published table,
for example a [C] server that announces 38 so that OpenCE build-268 players
(an exact match) list and join it, while our players of `chupa-28a` and later
join it as before (a Delta client joins by wire, and gets the record of its
own version). On a `chupa-38a` build (0.8.0g and earlier say `chupa-32a`:
use that row name there), `legacy-38.json` beside the server's config:

```json
{"delta_legacy": 1, "serial": 1,
 "wires": {"chupa-38a": {"announce": 38, "minimum": 11, "maximum": 39}}}
```

and `HALO_LEGACY_TABLE=legacy-38.json` (or `network.legacy_table =
"legacy-38.json"`). The server's log says "a local, unsigned legacy table is
in use ... announcing 38", and `sv_status` "Following OpenCE network version
38 (a local table)". It stops relaying signed tables meanwhile; remove the
setting once a published table announces the number. From auto-announce on (below),
a server follows the signed table's newest hosting number by itself, and
`HALO_NET_ANNOUNCE` pins an older one: a local table is for tests.

## Networks

The game's side of [federation](delta/federation.md): which network a
player's certificate belongs to, and showing it. The formats are
delta-spec's `link.md`, "Networks". Nothing on the wire changes and no
certificate changes: a certificate's network is the one whose root key
signed its issuer certificate, so today's read as `chupathingy.org`.

- `port/linux/src/networks.c`: domains, `handle:domain` names, the signed
  list of networks (read strictly: the size, the signature, then the JSON)
  and the networks a game trusts. No SDL, so its tests
  (`tools/test_networks.py`, with delta-spec's test vectors in
  `port/linux/tests/federation_vectors.json`) build anywhere; it is also a
  fuzz target (`tools/tests/host_fuzz/networks`).
- `port/linux/src/networks_game.c`: the home network (`identity_key.h`'s
  domain and root keys, which nothing changes), the newest signed list
  (cached as `delta_networks.signed` in the save root, fetched from
  `network.browser_url`'s `/v1/delta/networks` and `.sig` every few hours),
  and `network.networks_trust` and `network.networks_block`. The list has
  its own keys (`networks_key.h`), never the legacy table's. While they are
  all zero no list is taken or fetched, and a game trusts its home network
  and its local additions alone.
- `delta_peer_game.c`'s proof check finds the network first (home first),
  then checks the chain as before with that network's root keys alone. The
  home network's revocation list applies to its own players; other
  networks' aren't read in this version (their 7-day certificates are the
  limit).
- A player of another network shows as `Name:domain` wherever verified
  names show (`profile_names.c`: scoreboard, lobby, chat, postgame, the
  Guide); where a column is too narrow, as the name alone and a `:`.
  They get no site card, clan tag, role or ban (moderation hears only of
  the home network's bound proofs), and no ADD FRIEND in the Guide. One identity is one player by device, or by network and account
  together.
- A fork with its own network builds with
  `-DIDENTITY_HOME_DOMAIN='"its.domain"'` and its own `identity_key.h`.
- Consoles leave Delta identity out for now
  (`HALO_CONSOLE_NO_DELTA_IDENTITY`), so this too.

## In the code

The constants became calls (`delta_legacy_announce()`,
`delta_legacy_minimum()`, `delta_legacy_maximum()`, in
`halo_port_limits.h`) at the places the number is used: the listing
(`p2p_lobby.c`), the advertisement (`network_server_message_handler.c`), the
join check (`network_client_manager.c`), the browser (`browser.c`) and what
the server and reports print. `HALO_PORT_NETWORK_VERSION` stays as the
built-in announce and the wire's own number, so OpenCE's tools that read it
keep working.

`port/linux/src/delta.c` does the rest (declared in `delta.h`):

| Call | What it does |
| --- | --- |
| `delta_legacy_start()` | at start-up: the override or the cache, and the fetching thread |
| `delta_legacy_serial()` | the serial of the signed table in use; 0 for none or an override |
| `delta_legacy_signed(buffer, size)` | the signed table in use, for Delta Peer to send |
| `delta_legacy_offer(table, size)` | a signed table from a peer: checked, taken if newer |
| `delta_capability_disabled(bit)` | the kill switch |
| `delta_legacy_override()` | whether a local, unsigned table is in use |
| `delta_legacy_relay()` | whether Delta Peer relays tables: not with an override, nor with no key |
| `delta_legacy_following(text, size)` | "Following OpenCE build-145 (table 2)", for the log's header and `sv_status` |
| `delta_legacy_layouts(facts, count)` | the table in use's layout facts for this wire, oldest first (`network_rules.c` asks, told of it at start) |

`tools/test_delta.py` checks the tool and builds `delta.c` with a test key
(`tools/delta_check.c`) to check what the game takes and drops.

## Running the cross-play test

On Linux (the loopback addresses beyond 127.0.0.1 need nothing set up),
with the NTSC Xbox maps:

```
python3 tools/crossplay_test.py build --repo. --ref v0.7.0b --out /tmp/ours --server
python3 tools/crossplay_test.py build --repo../opence --ref build-145 --out /tmp/theirs
echo '{"delta_legacy": 1, "serial": 1, "wires": {"chupa-20a": {"announce": 22, "minimum": 11, "maximum": 22}}}' > row.json
python3 tools/crossplay_test.py run --ours /tmp/ours/build/linux/chupathingy \
    --server /tmp/ours/build/server-x86/chupathingyce-server \
    --theirs /tmp/theirs/build/linux/halo --maps ~/halo/maps \
    --ours-table row.json --work /tmp/crossplay --out result.json
```

Every copy has its own home, data and save folders under `--work`, and
nothing goes online. `check --work /tmp/crossplay --out result.json` checks
a run's logs again. The tool's header lists the steps and what passes.

What OpenCE build-145's own hooks can't do (seen against itself too): its
host starts no game once a client of the network test has changed team in
its lobby, and drops a lobby quiet for 15 seconds, so the test starts each
game 4 seconds after setting it up, and its host's next game (CTF) is not
counted. Every OpenCE copy also logs a release build's assertion in
`sound_dsound_xbox.c` (#980, a gain out of range) a few times; it is
reported, not counted.

## Delta Profile in the game

The player's own Delta profile, shown in the game
(`port/linux/game/profile_screen.c`). The game never makes, renames or
deletes a profile. The password, email, two-step sign-in, export and
deletion stay on the site, which OPEN ON THE WEB reaches. Linking is per
copy: every Halo profile on a copy counts for the same Delta profile.

- **Ways in:** the Chupathingy menus' Profiles have a DELTA row
  below Create New Profile, with a summary in the panel. In both menu
  sets, Online Games'
  Start opens it (the web page when `display.delta_profile` is false).
  A Halo profile's panel shows one quiet line, "Delta: @handle" or
  "Delta: not linked". With an empty `network.browser_url`, every way in
  is hidden.
- **Sections:** OVERVIEW (name, the player's Spartan, badges, record,
  recent games), DEVICES, ROLES, CLAN, PRIVACY, then LINK
  PROFILE or UNLINK THIS DEVICE, and OPEN ON THE WEB. The Chupathingy menus
  draw them with their own widgets
  (`port/assets/menus/chupathingy/delta_profile.xml`), and Privacy is one
  of their settings pages. The Xbox menus get a
  screen drawn as Online Games is. The dialogs (Link Profile, unlinking,
  and the pages of the site with a QR code) are the same in both.
- **Requests:** the game sends this copy's player key, over HTTPS only,
  to the game list: `POST /v1/game/profile` (the profile, its copies, roles
  and privacy), `/v1/game/unlink` (this copy only) and `/v1/game/privacy`
  (the site's own privacy settings). The site takes 6 requests a minute
  from a key, so the game asks at most every 10 seconds, and again when the
  answer is a minute old. Another copy is unlinked on the site
  (`/profile#copies`), signed in, never with a key alone. A copy with a
  device certificate sends a token in place of the key ("Device
  identity"). The answer is
  kept in memory only. `debug.txt` says whether it loaded, never what it
  holds.
- **Profile names:** in a game on Delta, a verified player (Delta Peer's
  `identity`: "Verified names") shows by their Delta Tag, in the
  capitals they picked, on the scoreboard, the postgame report, the lobby,
  chat, the kill feed and the HUD's other lines
  (`port/linux/game/profile_names.c`). The game asks the site for those
  players' public cards by their proven accounts (`POST /v1/game/players`,
  no key). Split-screen players each prove their own Halo profile's
  account. OpenCE players, players who don't share, an older build's
  unproven PROFILE claim, and what OpenCE clients see show game names.
  `display.profile_names = false` shows everyone by their game name.
- **Consoles:** `HALO_CONSOLE_NO_DELTA_PROFILE` leaves all of it out. A
  console that has the game list keeps Online Games' Link Profile panel.
- **Service record:** the profile's `"record"` has the site's totals of
  its confirmed games (host and verified: only games of linked copies
  count) and, from a site with them, `"best_spree"`, `"favorite_map"` (a
  map key), `"favorite_weapon"` (a weapon family, "sniper rifle": the
  second part of `weapons\...` or `vehicles\...` damage paths) and
  `"medal_counts"` ({key: count}, most first, the medals the games
  reported). An older site sends none of these and the game shows what it
  has. `GET /v1/profiles/ID/record` gives the same record to anyone, except
  for a locked profile or one that hides its service record (404).
- **The Delta Guide's SERVICE RECORD** (`port/linux/game/guide_record.c`) shows the
  first player's Halo profile's own record beside the site's. The game
  keeps it in the profile's folder (`service_record.txt`,
  `port/linux/src/service_record.c`): every multiplayer game the profile
  finished on that device, as `game_stats.c` adds it a few seconds after
  the end (the host's final statistics, the medals and weapons of the
  kills it saw). It is never sent. A win is counted as the site counts
  one (the best team, else first place). Its one row, MEDALS ON HUD, is
  `display.medals`.

## Link Profile on a console

The original Xbox, the Xbox 360 and the Wii U link with a key of their own
(`port/console/link`, the site's `console_link.py`). Their Online Games'
Start opens a DELTA PROFILE panel: linked to whom, or a code to enter at
stats.chupathingy.org/connect (and its QR code), then A to confirm.

- **The key:** made on the console the first time its player asks for a
  code (`delta_device.key` beside config.toml: an Ed25519 seed as 64 hex
  digits). It never leaves the console and is never logged. The console's
  player ID is the first 16 bytes of SHA-256("chupathingy console id v1\n"
  + its public key), so no one can claim another console's ID.
- **The code**, over the console list's plain HTTP (no TLS yet): the
  console sends its public key and a commitment to a secret
  (`/v1/console/link/start`), the site answers a nonce, the console sends
  the secret (`/v1/console/link/code`). Both work out the code
  (XXXX-XXXX, 10 minutes) from the key, the secret and the nonce, so a key
  swapped on the way gives a code that isn't the one on the screen. The
  console polls `/v1/console/link/status`, asks its player, and signs its
  answer over the profile it showed (`/v1/console/link/confirm`). Nothing
  sent can be used again by anyone who sees it.
- **HTTPS later:** once a console has Mbed TLS (the site's key pinned, so no
  clock is needed), its port sets the hook's `https_request` and the same
  calls go to stats.chupathingy.org over TLS.
- **Afterwards:** the console keeps the handle (`delta_link.txt`), and asks
  `/v1/console/profile` (a signed one-use challenge) for its name and clan
  tag when Online Games opens; unlinked on the site, it forgets it. A
  linked console shares its ID in Delta Peer's PROFILE
  (`HALO_CONSOLE_PROFILE_ID`), so its games count and Delta players see its
  Delta name. A console that never asked for a code sends nothing.
- **Its games:** after each online game it hosted or joined, a linked
  console claims its own line (`port/console/reports/console_claims.c`,
  `HALO_CONSOLE_GAME_CLAIMS`). It asks for a one-use challenge
  (`/v1/claim_challenge`), then signs it with its line's name and numbers
  and the game's invite or host name, map, length and how long ago it
  ended (`/v1/console/claim`). The site gives that line to the console's
  ID, so the game counts for the profile, even one the console hosted,
  which sends no report. Plain HTTP, nothing secret; tried again a few
  times while the game's reports come in. `report_joined_games = false`
  turns it off too.
- **Unlinking** is on the site (`/profile#copies`). The original Xbox's
  identity file (`D:\game_list_player.key`) still works, and wins when
  present.

## Delta Control's event log hook

`server_roles_set_recorder()` hands every
audited change, as a `struct control_audit_event` (time, via, actor, role,
action, target, reason, ok, detail; never an address), to a recorder: Delta
Stats' event log records moderation through it.
