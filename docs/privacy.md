# What your game shares

When an online game ends, your game sends the scoreboard to stats.chupathingy.org. That's how your games end up on your service record.

When you host, your game also sends the match details (kills, medals, accuracy) for match pages and heatmaps.

Your game never sends anyone's address or a hardware ID.

You can turn each of these off on the Privacy screen in Settings (Gearbox menus), or under `[network]` in `config.toml`:

| Setting | Turns off |
| --- | --- |
| `report_joined_games = false` | Reports of games you join |
| `report_events = false` | Match details of games you host |
| `report_hosted_games = false` | All reports of games you host (they stay listed) |
| `list_hosted_games = false` | Listing your hosted games, and their reports |

The full list of what the site keeps is on [stats.chupathingy.org/delta#tracked](https://stats.chupathingy.org/delta#tracked).

## Games you join

Your service record is on [stats.chupathingy.org](https://stats.chupathingy.org), found by your name. Games you join count even when the host doesn't run Chupathingy: when an online game you joined ends, the game sends stats.chupathingy.org the scoreboard as your game saw it (your own line in full; everyone else's name, team, place, score, kills and deaths) with your player ID. Your copy also confirms your line in the host's report, which puts the game on your profile. To turn both off for games you join, set `report_joined_games = false` under `[network]` in `config.toml`.

On a console linked to a Delta profile, the game also claims your own line after each online game, hosted or joined: your name, team, score, kills and deaths, the game's name, map and length, signed by the console. That puts the game on your profile. `report_joined_games = false` turns this off as well.

## Showing that you're playing

If your copy is linked to a Delta profile, your profile shows which game you're playing. While you're in a Delta game, your game tells stats.chupathingy.org which game it is, about once a minute: your player key and the game's invite, nothing about other players. The site shows that game (its name, map and player count) only to the people you allow, and the invite itself only to people allowed to join you. It keeps this in memory for a few minutes and never stores your address. Nothing is sent from the menus, or for a game whose host doesn't run Delta.

To stop it, turn off SHOW WHEN I'M PLAYING in the Guide's SETTINGS (F3), set `share_presence = false` under `[network]` in `config.toml`, or turn off "Show when I'm playing" on your profile's Privacy page on the site.

## Your device key

From 0.9.1, your game also makes a device key of its own. Its private half never leaves your computer. Once, when your game enrolls, it sends the site your player key one more time with the device's public key, platform and build. After that the site gives your game a short-lived certificate and tokens, and your game sends a token in place of your player key.

For each device the site keeps its device ID and public key, the copy it enrolled with (your player ID), your profile's ID, the platform and build, the day it was added and the day it was last used, and whether it was signed out or removed. It never keeps an address. When a profile is deleted, its devices are revoked at once. The site keeps only each device's ID and when it was revoked, so servers keep refusing it, and deletes those too after 15 days.

## Games you host

Set `report_events = false` under `[network]` in `config.toml`. It's on by default: the games you host send their Delta Stats (kills with positions, accuracy, medals, objectives) to stats.chupathingy.org for its match pages and heatmaps. Never anyone's address or hardware ID; OpenCE players are only their name, team and that game's score, kills and deaths. This stops Delta Stats only: a listed game's carnage report is still sent. To keep your hosted games listed but send neither, set `report_hosted_games = false`; `list_hosted_games = false` stops the listing too. All of these are also on the Privacy screen in Settings (Gearbox menus).
