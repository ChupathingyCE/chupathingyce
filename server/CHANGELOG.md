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
