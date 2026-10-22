# Dedicated server changelog

What changed in the ChupathingyCE Dedicated Server. The server is released
with the game and has its version; the game's own changes are in its
release notes.

## Unreleased

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
