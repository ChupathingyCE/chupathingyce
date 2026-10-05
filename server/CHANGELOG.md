# Dedicated server changelog

What changed in the ChupathingyCE Dedicated Server. The server is released
with the game and has its version; the game's own changes are in its
release notes.

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
