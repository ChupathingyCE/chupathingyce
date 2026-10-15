# Offline multiplayer bots

Bots are experimental and excluded by default. Configure with `--bots` to
compile them and register their settings:

```sh
python3 configure.py --bots
ninja macos                 # or ninja linux on Linux
```

Set the bot count in `config.toml` (or `HALO_BOTS` for one run):

```toml
[bots]
count = 7
skill = "marine"            # recruit, marine, odst, or spartan
```

`bots.count` ranges from 0 to 31 and defaults to 0. Bots use ordinary Halo
player datums and the host's player-action pipeline. They are local/offline
players, not network replicas: only a local game or a host with no remote
machines may run them. If a remote machine joins, bots leave. No multiplayer
wire format or network version is changed by this feature.

The standard two-flag CTF behavior is separately opt-in with
`bots.ctf = true` (`HALO_BOT_CTF=true`). It remains experimental; Assault,
single-flag and manual-reset CTF, King and Oddball are unsupported. See
[objective-mode scope](bot-objective-modes.md).

Navigation uses the map's walkable collision geometry and is still map
-dependent. Vehicle driving, teleporter routing and general floor transitions
are not implemented. Bots do not change scores or objective ownership directly;
the game engine remains authoritative.

## Checks

Asset-free C-backed tests can be run without game maps:

```sh
python3 tools/test_engine_ai_behavior.py
python3 tools/test_engine_ai_navigation.py
python3 tools/test_engine_ai_traversal.py
python3 tools/test_engine_ai_utility.py
python3 tools/test_engine_ai_aim_claims.py
python3 tools/test_surface_navigation.py
python3 tools/test_vehicle_navigation.py
python3 tools/test_bot_perception_steering.py
python3 tools/test_bot_fire_safety.py
```

The feature-framework, dispatcher, settings, lobby and CTF tests are pytest
functions and can be run with:

```sh
python3 -m pytest -q tools/test_features.py tools/test_bot_lobby.py \
  tools/test_bot_lobby_menu.py tools/test_bot_ctf.py
```

The ChupathingyCE port was build-checked with Bots enabled and disabled. No live
match or network session was run while porting this branch.
