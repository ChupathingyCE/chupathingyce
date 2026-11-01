# Playing online

The short version is on the [front page](../README.md#play-online).

## Playing with other builds

Chupathingy players on every platform always play together.

Chupathingy also plays with OpenCE players when the network versions match. The Xbox 360, Wii U and Switch builds join on network version 32 until their next updates, and our console servers announce 32 for them.

Browser builds can't join our games, and we can't join theirs.

- **Our own releases.** Chupathingy has its own code, releases and version
  numbers, and doesn't change under you every few hours.
- **Playing together.** Chupathingy players and OpenCE players
  join each other's games when their network versions match. Chupathingy
  follows OpenCE's versions: a cross-play test
  runs against each new one, and once it passes, a signed table tells
  existing Chupathingy builds which network versions to announce and join,
  without a new download. A change to how multiplayer plays still needs a
  release. The "Compatible with" line in the [README](../README.md#play-online)
  says which versions this release plays with.
- **Delta** is Chupathingy's network family ([docs/delta.md](delta.md)):
  the game list, stats, profile links and dedicated server admin tools on
  [stats.chupathingy.org](https://stats.chupathingy.org), and a channel between
  Chupathingy machines for features of our own (how builds play together:
  [docs/delta-compatibility.md](delta-compatibility.md)). Games with OpenCE players
  use the shared game protocol, so everyone plays together.

## Joining, hosting and invites

| You want to | Do this |
| --- | --- |
| Join a game | **Multiplayer → Online Games**, pick a game, press **A**. Or press **Join** on [stats.chupathingy.org](https://stats.chupathingy.org). |
| Host a game | **Multiplayer → Online Games → Y (Create Game)**, or host from System Link as usual. Your game is listed online by itself, on stats.chupathingy.org and in OpenCE's in-game Server Browser (`public_lobby`/`host_public` under `[network]` in `config.toml` turn that off). |
| Invite a friend | When you host, the game copies an invite link (`halo://join/…`). Send it; opening it joins your game. |
| Make an account | On [stats.chupathingy.org/profile](https://stats.chupathingy.org/profile), or press **Start** in Online Games to make one for the player you already are. |
| Link the game without a browser (Steam Deck, Game Mode) | In Online Games, press **RB** (or **C** on the keyboard) for Link Profile. On your phone or computer, go to [stats.chupathingy.org/connect](https://stats.chupathingy.org/connect), enter the code the game shows (or scan its QR code), then press **A** in the game to confirm. |
| Use the PC menus | Set `menus = "pc"` under `[display]` in `config.toml`. **Multiplayer → Join Game → Server Browser** lists the public games of every compatible build and the games of stats.chupathingy.org, each once. |
| List a game from a build that can't list itself | Sign in on the site, open **Host a Game**, and paste your invite link. |

What the game sends to stats.chupathingy.org, and how to turn it off, is in [docs/privacy.md](privacy.md).

Everything here plays with OpenCE players on a matching network version: they
can join your games and you can join theirs. Games an OpenCE player hosts are
recorded from the reports of the Chupathingy players in them; a game only
one player reported counts only on that player's own record. Their hosts can
still list them on the site (Host a Game).

## If friends can't join

Most homes need nothing. If your friends still can't get in, forward **UDP 2302** on your router to your computer. Games you host use 2302 (Halo's usual port), or the next free one up to 2306 if another program has it; Server Setup shows the port and whether your router forwarded it by itself (UPnP). To pick another port, set `tunnel_port` under `[network]` in `config.toml`, or PORT in Server Setup.

## Online co-op

The campaign can be hosted for friends over the Internet:

1. Multiplayer > **CREATE GAME** > **INTERNET**.
2. On the map list, switch the chooser in the top row from MULTIPLAYER to
   **SINGLEPLAYER**, then pick a level and a difficulty.
3. Continue to **Server Setup** (friendly fire, extra enemies, player
   collisions and listing are set there) and start the game.

Friends join it like any other game. The CO-OP CAMPAIGN entry of the
Multiplayer menu is the Xbox's split screen co-op on one computer, not an
online game. The original Xbox can't host or join online co-op.
