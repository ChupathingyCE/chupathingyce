# Dedicated server changelog

What changed in the ChupathingyCE Dedicated Server. The server is released
with the game and has its version; the game's own changes are in its
release notes.

## Unreleased

- A playlist entry whose map isn't installed on the server is skipped,
  and a playlist with no installed map at all isn't hosted (the console
  and the log say so) instead of the game ending at the switch.
- Parties can stay together (`keep_parties_together`, off by default): in
  team games, Chupathingy players in one party go to one team when the
  teams stay within one player of each other. `sv_set keep_parties_together
  true` turns it on while the server runs.
- Players' reports: Chupathingy players report others from DELTA, and they
  land in `sv_reports` and the panel's new Reports tab with the server's
  refusal counts for that player. Linked servers send them to the site too,
  where a clip the reporter attached is kept for 14 days. OpenCE players can
  be reported by name; those reports stay on the server.
- `sv_lock` and `sv_unlock` stop and start new joins (the panel's Status
  tab, the game's Moderation screen and the site have it too). The game
  list shows a locked server. It ends once the server has been empty for
  10 seconds, and a restart unlocks.
- `late_join_hold` (off by default): players who join a game in progress
  watch until the next game.
- Shared ban lists for linked servers (both off by default): `ban_share`
  sends your account and device bans to your list on the site, and the
  lists you pick on My servers come back as `sv_banlist`'s S entries
  (`sv_ban_lists`, `sv_unban S<ref>` for an exception here).
  `sv_ban_account` bans a Delta account that isn't in the game.
- The control panel is on by default, at `http://127.0.0.1:8080` (this
  machine only). New hosts get the one-time setup link on the console and in
  `control_setup_code.txt`; on Windows the server also opens it in the
  browser. `HALO_DEDICATED_CONTROL=off` turns it off. If port 8080 is taken,
  the server logs one line and keeps hosting. Public addresses stay opt-in,
  and our Docker image keeps the panel off until you set it.
- Halo Custom Edition 1.0.10 players can play (`ce_joins`, still off by
  default): a Custom Edition player joins a game on a Custom Edition map as
  one of the server's own players (up to 4), and stays from one game and
  map to the next, as on Custom Edition's own servers. The development
  switch `HALO_DEDICATED_CE_ACCEPT` is gone. See "Custom Edition join
  attempts" in server/docs/settings.md for what works and what doesn't yet.
- Host rules, off by default as on the Xbox: `allow_player_names`
  (`HALO_DEDICATED_ALLOW_PLAYER_NAMES`) lets Chupathingy players show names
  above heads, and `allow_follow_camera`
  (`HALO_DEDICATED_ALLOW_FOLLOW_CAMERA`) lets them watch a follow camera
  while they wait to respawn. Team changes during a game (`team_change`)
  are now `off` unless set; `balanced` and `allow` work as before, and
  players see CHANGE TEAMS only where the server allows it. The rules reach
  Chupathingy players over Delta Peer; OpenCE players play as before.
- Auto-announce: with `HALO_NET_ANNOUNCE=latest` (the default), the server
  announces the newest OpenCE network version the signed table proves for
  hosting, checks for a new table every 30 minutes, and moves to a newer
  number between games without a restart. A table row may name host
  protections a number needs; a server without them keeps announcing its
  own number and logs why. A number in `HALO_NET_ANNOUNCE` pins it.
- Halo 2 anniversary game types and playlist in `server/events/h2-anniversary`
  (not in the download): seven game types and a 12-entry playlist on the
  stock maps, from options the game already has.
- A betrayed player respawns on the normal timer; the betrayer takes the game type's suicide penalty (plus any friendly fire penalty) on their next respawn. Suicides and deaths nobody caused keep the suicide penalty.
- Hits through walls are refused: a client's reported hit counts only when
  the shot had a clear line to its target from where the server had the
  shooter (their view, head, or the gun of their vehicle seat), or, for an
  explosion, from where it went off. Glass, what the projectile goes
  through, and projectiles that home in or arc are allowed for; melee and
  vehicle hits are not checked. The log counts these refusals with the
  other refused hits. On by default; `network.hit_line_of_sight = false`
  (`HALO_NET_HIT_LINE_OF_SIGHT=false`) turns it off if good hits are
  refused. The players' builds need nothing new.
- The console keeps what you are typing on its own prompt line in a
  terminal: log lines print above it instead of breaking it up. Up and
  Down bring back earlier commands. Pipes and services see the same output
  as before.
- First-run setup in the control panel. A server started with the panel on
  (`HALO_DEDICATED_CONTROL`) and no playlist no longer exits: it waits
  until its owner makes their account and picks a name, most players, a
  playlist and Server Browser listing in the browser, then hosts and offers
  to link the server to a Chupathingy account. The one-time setup code is
  also written to `control_setup_code.txt` in the data folder (owner-only,
  deleted once the account is made), for servers whose console nobody sees.
  Servers started with `HALO_DEDICATED`, or with an owner already, start as
  before.
- A new `config.toml` has only the settings the server uses. The display,
  sound, controls and other game settings are left out. An existing file
  is not changed and still works.
- The control panel's Status tab answers "Can people join?": internet play's
  UDP port, whether the router forwarded it (UPnP), whether the network lets
  most players in, whether the game is in the Server Browser and why not,
  the OpenCE network version it announces, and players. When something is in
  the way it says what to do, with a link to the site's ports guide.
  `sv_status` has the same lines, and a `reach` object in JSON. Ports and
  states only, never an address.
- New [Host a server](docs/hosting.md) guide: Docker or Linux, ports, the
  control panel (SSH tunnel or HTTPS), linking to your profile, and
  moderators. It matches the site's stats.chupathingy.org/host page. The
  compose file's control panel note now says where the setup code is and
  how to reach the panel from your own computer.
- Evidence for moderators: `sv_evidence [player]`, `GET /v1/evidence` and a
  Refused column in the panel's Players count what the server refused of
  each player's machine this game (movement and vehicle predictions out of
  bounds, hit reports that failed validation, hits through walls, windows
  its clock ran fast).
  Moderators and up; reset each game; never sent anywhere, never an
  automatic ban.
- Betrayal and AFK limits, both off by default: `betrayal_kick`
  (`HALO_DEDICATED_BETRAYAL_KICK`) warns a player one betrayal short of the
  limit and kicks them at it, and a second such kick within 24 hours is a
  30-minute ban; `afk_kick_minutes` (`HALO_DEDICATED_AFK_KICK_MINUTES`)
  kicks a player with no input for that long in a game in progress, warned
  30 seconds before, moderators never. OpenCE players are warned through
  the host's notice in their chat. Every warning, kick and ban is in the
  audit file.
- A Windows server: `chupathingyce-server-windows-x64.zip`, for 64-bit
  Windows 10, Windows 11 and Windows Server. The same server, settings,
  playlists, game types, commands and control panel as on Linux, as a
  console program with `start-server.bat` and a README (`server/windows`).
  Ctrl+C or closing its window stops it and takes its game off the lists.
  `start-server.bat install` (or `--install-service`) makes it a Windows
  service that starts with Windows, restarts after a crash and runs as its
  own account, not SYSTEM; `--uninstall-service` takes it out. Both keep
  the server's saves in `saves` in the data folder, apart from the game's.
- Map names as Halo PC takes them: commas, brackets, spaces and the rest a
  Windows file name can have (`bigassv2,104@ce`, `[h3]_sandtrap@ce`), in
  `sv_map`, `sv_maps` and playlists. A name with a space goes in double
  quotes (`sv_map "skedar v2@ce" slayer`, and the same in a playlist file).
  A playlist entry that cannot be played (a missing or damaged map, a
  campaign level, an unknown game type, or a lobby that closed before its
  game started) is skipped with the reason in the log, `sv_status` and the
  panel, instead of being hosted again every 5 seconds or stopping the
  server on the damaged disc error. When every entry is skipped, the server
  waits 5 seconds, then twice as long each round, up to 5 minutes.
- GameSpy status queries, off by default (`gamespy_listing`,
  `HALO_DEDICATED_GAMESPY_*`): the server answers `\basic\` and QR2 status
  queries on UDP 2302 and sends QR2 heartbeats to the masters Halo Custom
  Edition 1.0.10 reports to, with Xbox maps named `xbox_<map>` (naming from
  the Custom Edition community, via ugoboom). It reports its own version, so
  Custom Edition 1.0.10's browser does not show it.
- Halo Custom Edition 1.0.10 join attempts, off by default (`ce_joins`,
  `HALO_DEDICATED_CE_JOINS`, with `gamespy_listing` on): the GameSpy port
  answers a Custom Edition client's connection (its handshake, key
  agreement and enciphered messages), names the map, reads the join request
  and refuses it cleanly with Custom Edition's "Unable to join the game
  server!" instead of a timeout. Playing comes in later steps. For
  development, `HALO_DEDICATED_CE_ACCEPT=1` takes the join instead: game
  settings, begin game and keep-alives, so the client loads the map and
  stays connected (it doesn't see the game yet).
- Lobby message limit: a player's machine may send the lobby's settings,
  player settings and add or remove player requests in a burst of 32 and
  16 a second after it, more than four players on one machine holding a
  menu key send. Past that, the extra requests are dropped and the log says
  so once per machine per lobby (its machine number only). OpenCE and Xbox
  players are held to the same limit; nothing on the wire changes.
- Machines per address: `max_machines_per_address`
  (`HALO_DEDICATED_MAX_MACHINES_PER_ADDRESS`, default 8, 0 for no limit).
  A machine joining from an internet address that already has that many in
  the game is refused with OpenCE's "too many players from your address"
  code (13). Internet play players are counted by their real address, and
  local network addresses are never limited. The log names the refused
  machine with its address hidden. Games hosted by players have the same
  limit as `network.max_machines_per_address`, off by default.
- Custom Edition players (still behind the development switch, off by
  default): their own position corrected to the server's, shield and
  health, deaths, the scoreboard, weapons and equipment on the ground,
  vehicles and seats, a welcome line in their chat (`ce_welcome`), and
  `sv_say`, `sv_kick` and `sv_ban` reaching them. Bans, the limit on
  players from one address and the betrayal and AFK limits apply to them.
- Control panel hardening: `sv_welcome` needs the settings permission (an
  admin's) and is in the audit file; a command missing from the permission
  table is the console's alone. Wrong logins lock an account only for the
  address they came from, are forgotten after 15 quiet minutes, and a
  locked login reads as a wrong password; `HALO_DEDICATED_CONTROL_TRUSTED_PROXY`
  lets the limits count each client behind a reverse proxy. The pending
  link code is shown only to the owner and the console, and no longer
  written to the log. A second factor is replaced only by turning it off
  first (password and code). Idle connections are dropped after 3 seconds,
  8 at most from one address. The log hides IPv4-mapped IPv6 addresses;
  `sv_link_status`'s JSON is escaped; `HALO_DEDICATED_LINK_URL`'s plain-HTTP
  exception is this machine's exact address only.
- Bomb and Grifball game types for playlists (`bomb`, `grifball`,
  `grifball_open_pitch`, `bomb.*` fields), played only with Chupathingy
  clients; standard game types are unchanged.
- Xbox map packs: each pack's maps in its own `maps_<pack>` folder of the
  data folder (Halo CE+'s in `maps_cex`), named `<name>@<pack>` in
  playlists, `sv_map` and `sv_maps` (`wizard@cex`); `/v1/maps` adds
  `map_packs` (each pack map's name, pack and tag). The listing names a
  pack's map ("Gothra", Halo CE+) for players without it. Deploy: a CE+
  server needs the pack's `maps` folder copied to `data/maps_cex` and a
  playlist naming `@cex` maps; nothing changes for the other servers.
- Network versions 30, 31 and 32. Announces 32 and joins as a client hosts
  of 11 through 33. Redeploy: a legacy table can't follow this one (wire
  `chupa-32a`).
- `HALO_NET_BLOCK_MOUSE_AIM_ASSIST` (default `false`): turns the view's
  magnetism off for players aiming with the mouse, in every game of the
  server; clients are told when they join and when it changes.
- Riders of an elevator are carried by its movement of the same tick, and a
  second game on one computer can play (a client takes any free port).

## 0.7.5b

- No server changes of its own; built from the same code as the game's 0.7.5b.

## 0.7.5a

- OpenCE's network version 29 (build-206). No message changed, but a game
  on a Custom Edition map names it by the map's own name, and its map
  version is the first 32 bits of the map's hash (the header checksum
  before). The server hosts its Custom Edition maps that way.
- Announces network version 29; joins as a client hosts of 11 through 30.
  Redeploy: a legacy table can't follow this one (wire `chupa-29a`).
- A reason for `sv_warn`, `sv_kick` and `sv_ban` is the rest of the line,
  quoted or not, up to 127 characters (longer ones were refused as "a word
  is too long" or "too many words"). The web page and the site's commands
  take reasons that long too.
- A warning reads "Server warning: <reason>" in the player's game, not
  "Server warning: Warning: <reason>".

## 0.7.5

- `sv_name_filter`: players whose name is on the blocked names list
  (`blocked_names.txt` in the data folder, shipped with the server) are
  warned, then removed 3 seconds later unless they change it. Off by
  default; `sv_name_filter 1`, `name_filter = true` in admin/settings.toml or
  `HALO_DEDICATED_NAME_FILTER=true` turns it on.
- Busy games' carnage reports are sent whole (a list of players who left
  past about 16 was cut off, and the site turned the report away).
- Network version 28, as 0.7.4e: no redeploy needed for cross-play.

## 0.7.4e

- OpenCE's network version 28 (build-201). No message changed; the game
  plays three things as OpenCE's 28 does: a multiplayer script's
  `switch_bsp` keeps the game's first BSP (maps with a copy of their place
  for each sky stop reloading), the flashlight and dynamic lights stay on
  past the Xbox's player and vehicle limits, and a second weapon picked up
  while firing is readied.
- Announces network version 28; joins as a client hosts of 11 through 30.
  Redeploy: a legacy table can't follow this one (wire `chupa-28a`).

## 0.7.2e

- OpenCE's network version 26 (build-184): its internet play's signalling
  is version 4, which OpenCE reads alone. The server reads and answers both
  3 and 4, so OpenCE's players of build-184 join it over the internet again,
  as do older builds'. OpenCE's browser players can't (no WebRTC).
- Announces network version 26; joins as a client hosts of 11 through 30.
  Redeploy: a legacy table can't follow this one (wire `chupa-26a`).

## 0.7.1b

- Delta Control: moderators for every server, no site needed
  ([docs/moderation.md](docs/moderation.md)). Roles (owner, admin,
  moderator), each with its permissions, checked for every command wherever
  it comes from, limited per person, and written to `control_audit.log`.
- Moderators by moderator key (a key each ChupathingyCE game makes from its
  player key and proves): `moderators.txt`, `sv_mod_list`, `sv_mod_add`,
  `sv_mod_remove`; `sv_players` shows a proved key and its role.
- In-game moderation over Delta Peer's `moderation` capability: a
  moderator's game warns, kicks, bans, ends the game or skips the map, each
  action signed.
- `sv_warn`; reasons for `sv_kick` and `sv_ban`.
- The control panel: an account for each person (name, Argon2id password,
  TOTP second factor the owner may require), a one-time setup link for the
  first owner, invitations and password resets, `sv_account_*` on the
  console, backoff per account and per address. Pages for people, the audit
  file, playlists and game types, settings, one's own account.
- HTTPS for the control panel beyond the loopback address: the server's own
  certificate (its fingerprint printed) or yours
  (`HALO_DEDICATED_CONTROL_CERT`, `_KEY`), `HALO_DEDICATED_CONTROL_TLS`.
  **Changed:** a non-loopback `HALO_DEDICATED_CONTROL` is HTTPS now; set
  `HALO_DEDICATED_CONTROL_TLS=off` on a private network to keep plain HTTP.
- The optional link to halo.milenko.org (`sv_link`, `sv_unlink`,
  `sv_link_status`, `HALO_DEDICATED_LINK`, `_LINK_URL`, `_LINK_ROLE`): roles
  given on the site, commands from the site's Servers page, outbound only.
- Playlists made and edited on the server (`sv_playlist_*`,
  `sv_mapcycle_add`, `sv_mapcycle_del`), game type files
  (`sv_gametype_*`), and a settings file (`sv_settings`, `sv_set`).
- Control API: `/v1/query`, `/v1/file`, `/v1/audit`, `/v1/playlists`,
  `/v1/gametypes`, `/v1/settings`, `/v1/moderators`, `/v1/link`, and the
  accounts' endpoints. A request body may be 16 KB.

## 0.6.3b

- The server is a program of its own, `chupathingyce-server`, for Linux
  only, in three downloads: `chupathingyce-server-linux-x64`,
  `chupathingyce-server-linux-arm64` (new: Oracle Cloud's free tier,
  Raspberry Pi 4 and 5, Ampere) and `chupathingyce-server-linux-x86`.
- One static file: no SDL, OpenGL or sound libraries, and no C library
  version to match. It runs on any Linux of its architecture.
- `--version` and `--help`. Without a playlist it says how to start one;
  without maps, or with a playlist it cannot read, it stops at once with a
  clear error.
- One container image for every architecture, on Alpine Linux, not root by
  default.
- Custom Edition and HaloMD maps on the x64 and arm64 servers.
- Commands for a running server, named after Halo PC's: `sv_status`,
  `sv_players`, `sv_kick`, `sv_ban` (for ever or for a while), `sv_unban`,
  `sv_banlist`, `sv_map`, `sv_mapcycle`, `sv_mapcycle_next`,
  `sv_end_game`, `sv_maxplayers`, `sv_name` and `help`. Kicks and bans use
  the game's own protocol; bans are `bans.txt`'s, by hardware id and
  address, read on every join.
- A console: commands typed where the server runs (`HALO_DEDICATED_CONSOLE`).
- Startup commands from a file (`HALO_DEDICATED_COMMANDS`).
- A control API, off unless `HALO_DEDICATED_CONTROL` turns it on: HTTP and
  JSON on 127.0.0.1 by default (`/v1/status`, `/v1/players`, `/v1/command`,
  `/v1/log`), with a token made on first use and kept only as an Argon2id
  hash, wrong tokens limited, and every command logged. See
  [docs/admin.md](docs/admin.md).
- A web admin page on the control API's port (on when the API is): status,
  players (kick, ban for a while or for ever), maps (play a map and game
  type now, skip, end the game, the playlist), bans (unban), the live log,
  and the server's name and most players. It works on a phone, in light
  and dark. A login with a token starts a session (an HttpOnly,
  SameSite=Strict cookie, ending after 30 minutes idle or 12 hours); every
  change needs the session's CSRF token, logins count against the same
  limits as tokens, and every action is logged with the admin who did it.
  Strict Content-Security-Policy, no framing, no caching. Reach it through
  an SSH tunnel, Tailscale or a TLS reverse proxy, never a public port.
- New API endpoints for the page and for scripts: `/v1/bans`,
  `/v1/mapcycle` and `/v1/maps` (`sv_banlist`, `sv_mapcycle` and the new
  `sv_maps`, as JSON).
- `sv_maps`: the maps the server can play (Xbox, `@ce`, `@md`) and the
  game types `sv_map` takes.
- A token for each admin: `sv_admin_add`, `sv_admin_rotate`,
  `sv_admin_remove` and `sv_admin_list` on the server's console (never the
  API). A new token is printed once there; a rotated or removed one stops
  working at once, with its web sessions. An existing
  `control_credentials.txt` keeps working as it is.
