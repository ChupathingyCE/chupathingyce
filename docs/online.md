# Playing online

The short version is on the [front page](../README.md#play-online).

## Playing with other builds

Chupathingy players on every platform always play together.

Chupathingy also plays with OpenCE players when the network versions match. This release plays with OpenCE's network version 44, and still joins their 41 and 43 hosts. The console test builds of November 13 do the same.

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
- **Delta** is Chupathingy's network family ([docs/delta](delta/README.md)):
  the game list, stats, profile links and dedicated server admin tools on
  [stats.chupathingy.org](https://stats.chupathingy.org), and a channel between
  Chupathingy machines for features of our own (how builds play together:
  [docs/delta/versioning.md](delta/versioning.md)). Games with OpenCE players
  use the shared game protocol, so everyone plays together. Other apps can
  sign players in there too, once registered
  ([docs/delta/clients.md](delta/clients.md)).

## Joining, hosting and invites

| You want to | Do this |
| --- | --- |
| Join a game | **Multiplayer → Online Games**, pick a game, press **A**. **LB** and **RB** (**Q** and **C** on the keyboard) switch the list: ALL, CHUPATHINGY + PUBLIC (the default: our servers and everyone's public games, without OpenCE's dedicated servers), CHUPATHINGY, and OPENCE (OpenCE's dedicated servers, the empty ones in one row at the end that **A** opens). Or press **Join** on [stats.chupathingy.org](https://stats.chupathingy.org). Co-op games are listed too. A game with a lock has a password: **A** asks for it first. |
| Host a game | **Multiplayer → Online Games → Y (Create Game)**, or host from System Link as usual. Your game is listed online by itself, on stats.chupathingy.org and in OpenCE's in-game Server Browser (`public_lobby`/`host_public` under `[network]` in `config.toml` turn that off). |
| Invite a friend | When you host, the game copies an invite link (`halo://join/…`). Send it; opening it joins your game. |
| Join by address | **Multiplayer → System Link**, pick **DIRECT IP** at the end of the list (or press **X**), enter the host's address or name under **SERVER ADDRESS**, then **JOIN GAME**. Or type `connect <address>` in the console, or start the game with `--connect <address>`. See [Join by address](#join-by-address). |
| Make an account | On [stats.chupathingy.org/profile](https://stats.chupathingy.org/profile), or press **Start** (or **F2** on the keyboard) in Online Games to make one for the player you already are. |
| Link the game without a browser (Steam Deck, Game Mode) | In Online Games, press **Start** (or **F2** on the keyboard), then pick **LINK PROFILE**. On your phone or computer, go to [stats.chupathingy.org/connect](https://stats.chupathingy.org/connect), enter the code the game shows (or scan its QR code), then press **A** in the game to confirm. |
| List a game from a build that can't list itself | Sign in on the site, open **Host a Game**, and paste your invite link. |

What the game sends to stats.chupathingy.org, and how to turn it off, is in [docs/privacy.md](privacy.md).

Anyone who has a game's invite can find out the host's internet address, and a public game's invite is listed for everyone (games you host are public by default). Players who join a public game can have their addresses seen by anyone watching that invite. To keep yours private, set `host_public = false` and send invites only to people you trust ([privacy.md](privacy.md#your-internet-address)).

Everything here plays with OpenCE players on a matching network version: they
can join your games and you can join theirs. Games an OpenCE player hosts are
recorded from the reports of the Chupathingy players in them; a game only
one player reported counts only on that player's own record. Their hosts can
still list them on the site (Host a Game).

## Join by address

System Link finds the games on your own network. **DIRECT IP** joins one it
can't see: a LAN party across two networks, a VPN, your own server, or a
friend's game whose port is open.

- **Multiplayer → System Link**, then **DIRECT IP**, the last row of the
  list (or press **X**). In the Xbox menus, which stay as the Xbox had them,
  use the console's `connect` instead. Enter the
  host's IPv4 address or host name under **SERVER ADDRESS**, with
  `:port` after it if the host isn't on the game's port, 5150. A controller
  gets the on-screen keyboard. **JOIN GAME** joins.
- **RECENT** goes through the last 5 addresses you joined. They're kept in
  `direct_ip.txt` in your save folder, on this computer only.
- The console's `connect <address>[:port]` and the command line's
  `--connect <address>` (or `HALO_CONNECT`) join the same way.

The game is checked and joined as System Link joins it: the same version
checks and the same messages when it can't be joined. It works with any host
System Link works with: Chupathingy hosts and dedicated servers, OpenCE
players' hosts and Xbox-style hosts. Across the Internet, a host answers
your search only when its `network.direct_ip_host` is on (DIRECT IP HOST in
the setup screen's Network settings; off by default). A host that doesn't
answer (that setting off, an older build, OpenCE's, the Xbox's) is joined
directly after a few seconds.

Over the Internet, the host opens **TCP and UDP 5150** (and UDP 5160 for
Delta's extras) on its router or firewall; games found through Online Games
and invites don't need this. IPv6
addresses can't be joined yet: the game's own network is IPv4. A direct join
has no password; a game with a password is joined from its invite link (or
Online Games), and DIRECT IP says so.

Once entered, an address is shown as stars, and saved ones by name
("Saved server 1"). The log names a public address only by a tag, as it
names every address ([docs/privacy.md](privacy.md)).

The Xbox menus (`display.menus = "xbox"`) stay as the Xbox had them, without
DIRECT IP. The consoles don't have it yet.

## The Delta Guide: friends and parties

The Delta Guide is Delta in the game. Open it with **F3** (on a Mac, F3 or the
backslash key), a controller's Guide button, or the pause menu. Android also
has it in the main menu and the Multiplayer menu (`delta.menu_item` turns
that on elsewhere). **LB** and **RB** (**Q** and **E**)
switch its sections:

- **FRIENDS**: who's online, invites and friend requests. Pick a friend to
  join their game or invite them to yours.
- **PARTY**: play together as a group of up to 8. Start a party, invite
  friends, or join with a code. When the leader joins or hosts a game, the
  party follows.
- **PLAYERS HERE**: the players in your game. It's listed only while you're in one. Mute someone, or add them
  as a friend.
- **RECENT**: Delta players from your last games.
- **CLAN**: your clan, if you're in one. Clans are made on the site.
- **SERVICE RECORD**: your stats and medals.
- **SETTINGS**: the Delta Guide's own settings, and ABOUT.

Friends, clans and recent players need a Delta profile. Sign in from the
Delta Guide, or see the table above.

### Party links

**COPY PARTY LINK** in PARTY copies a link like
`https://stats.chupathingy.org/party/ABCDEF`. Paste it in Discord or any chat.
Your friends open it, and the game joins your party. The same screen shows
the link as a QR code for phones.

## Voice chat

Voice chat is off until you turn it on. Pick PUSH TO TALK or OPEN MIC on
the VOICE CHAT row of the Delta Guide's SETTINGS. With push to talk, hold **V**,
or the D-pad right, to talk. **B**, or the D-pad left, switches who hears you: your party,
your team, players near you, or everyone if the host allows it. Voice goes
through the game's host and is never recorded. Voice chat works between
Chupathingy players on Windows, Mac, Linux and Android. Every voice setting
is in [docs/settings.md](settings.md).

## Hosting rules

Games you host play like the Xbox by default: no names above heads, no
follow camera while you wait to respawn, and players keep their team. You can
allow each one in `config.toml`: `allow_player_names`,
`allow_follow_camera` and `team_change` under `[network]`
([docs/settings.md](settings.md)).

Your game also protects itself while others are in it:

- Hits that look like they went through a wall are written to your log,
  and refused if you turn on `hit_line_of_sight` (off by default for now).
- In a public internet game, your own console can't cheat or change the
  game (`host_cheats`). Online Games shows "Host cheats on" when you allow it.
- The console and cheats can make only so many objects in each game
  (`host_console_objects`).
- You can limit how many machines join from one internet address
  (`max_machines_per_address`).

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
