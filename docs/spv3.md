# Trying SPV3

SPV3 is a campaign mod for Halo Custom Edition, made by the SPV3 team. You
can try it in Chupathingy 0.9.2. It's early. Some things are missing, and we
want your bug reports and logs.

## What you need

- Chupathingy 0.9.2 for Windows, Mac or Linux, the 64-bit build. SPV3 mode
  isn't in the console or 32-bit builds.
- Your Xbox maps, set up as usual ([Set it up](../README.md#set-it-up)).
- Your own copy of SPV3, installed with SPV3's own installer. We test with
  version 3.3.0.

We don't include any SPV3 files, and there's no installer for it in
Chupathingy yet. You copy the files from your own SPV3 install.

## Where the files go

Find the folder that holds your `maps` folder
([where that is](install.md#where-your-files-go)). Copy these from your SPV3
install into it:

1. Make a folder called `maps_spv3` next to `maps`. Copy everything in SPV3's
   `maps` folder into it. That's the `.yelo` files, `ui.yelo`, `bitmaps.map`,
   `sounds.map` and `loc.map`.
2. Make the folders `lua/spv3/scripts/global`. Copy SPV3's
   `chimera/lua/global/init.lua` into it.
3. Make the folder `lua/spv3/modules`. Copy everything in SPV3's `lua`
   folder into it: `CMT`, `class`, `halo_classes` and `JSON.lua`.
4. For SPV3's movies, make the folder `maps_spv3/bink`. Copy `attract.bik`,
   `credits.bik` and `credits_xbox.bik` from your SPV3 install into it.
   `attract.bik` plays when the game starts, and the credits play at the end
   of the campaign. Without them, the game's own intro plays instead. To
   skip the movie at start, set `skip_intro = true` under `[display]` in
   `config.toml`.

When you're done, it looks like this:

```
maps/
maps_spv3/
  ui.yelo
  spv3a10.yelo
  ...
  bitmaps.map
  sounds.map
  loc.map
  bink/
    attract.bik
    credits.bik
    credits_xbox.bik
lua/
  spv3/
    scripts/
      global/
        init.lua
    modules/
      CMT/
      class/
      halo_classes/
      JSON.lua
```

## Lua scripts

SPV3's HUD, skulls and other extras are Lua scripts. In SPV3 mode they run
on their own from `lua/spv3`; you don't need to turn anything on. They get
the same limits as any script: they read and write only their own `data`
folder and load only their own modules.

The `lua.scripts` setting in `config.toml` is for your own scripts in
`lua/scripts`. It stays off unless you turn it on.

## Play it

1. Start Chupathingy. With `maps_spv3` in place, it opens the setup screen
   and asks how you want to play. Pick **ASK EACH TIME**.
2. Pick **SPV3 (EARLY)**.

You get SPV3's own menus. To play Halo again, hold **Shift** (or **Back** on a
controller) while Chupathingy starts, and pick **VANILLA HALO**.

To skip the question, set **START WITH** to **SPV3 (EARLY)** on the same
screen.

## What works and what doesn't yet

All 22 levels and the menu load. SPV3's Lua scripts run.

Not working yet:

- A flat blue box shows at the crosshair
  ([#1781](https://github.com/ChupathingyCE/chupathingyce/issues/1781)).
- Enemies don't transform
  ([#1749](https://github.com/ChupathingyCE/chupathingyce/issues/1749)).
- SPV3's four new grenade types are missing
  ([#1751](https://github.com/ChupathingyCE/chupathingyce/issues/1751)).
- SPV3's post-processing works on the default renderer, but some levels
  don't look right yet and it costs frame rate. Turn it off with
  `display.opensauce_post_processing = false`
  ([#1786](https://github.com/ChupathingyCE/chupathingyce/issues/1786)).
- In online co-op Firefight, players who join don't get the loadout.

The full list is in
[#1808](https://github.com/ChupathingyCE/chupathingyce/issues/1808).

## Report a bug

1. Post in #bug-reports on the [Discord](https://discord.gg/chupathingy), or
   in #reportes in Spanish.
2. Put "SPV3" in the title.
3. Send your logs. In the Delta Guide's **SETTINGS**, pick **SEND MY LOGS**.
   It sends `debug.txt` to dump.chupathingy.org and gives you a code. Paste
   the code in your post ([Send your logs](help.md#send-your-logs)).

Say which level you were on and what you did just before it went wrong.
