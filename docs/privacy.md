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

## Games you host

Set `report_events = false` under `[network]` in `config.toml`. It's on by default: the games you host send their Delta Stats (kills with positions, accuracy, medals, objectives) to stats.chupathingy.org for its match pages and heatmaps. Never anyone's address or hardware ID; OpenCE players are only their name, team and that game's score, kills and deaths. This stops Delta Stats only: a listed game's carnage report is still sent. To keep your hosted games listed but send neither, set `report_hosted_games = false`; `list_hosted_games = false` stops the listing too. All of these are also on the Privacy screen in Settings (Gearbox menus).
