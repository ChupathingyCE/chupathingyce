# Help

What to do when something goes wrong, and how to tell us about it.

## Send your logs

When you report a bug, send your logs. They tell us what happened.

While the game is running, on Windows, Mac and Linux:

1. Open the Delta Guide (**F3**, or a controller's Guide button) and go to
   **SETTINGS**.
2. Pick **SEND MY LOGS**. It asks first. Pick **SEND**.
3. You get a short code. Pick **COPY** and paste it in #bug-reports on the
   Discord with your report.

**OPEN LOGS FOLDER**, right beside it, opens the folder with the files.

If the game won't start, use the setup screen instead: hold **Shift** (or
**Back** on a controller) while the game starts, go to **LOGS & HELP** and
pick **SEND MY LOGS**. It also asks whether to send your `config.toml`.

The logs go to dump.chupathingy.org. Your computer's account name and
network addresses are taken out before they're sent. Player names stay in.

You can also attach the file yourself. It's `logs/debug.txt` in the folder
that holds your maps ([where that is](install.md#where-your-files-go)). On
Android it's in `/sdcard/Android/data/dev.horrible.chupathingyce/files/logs`.
Attach it to a post on the [Discord](https://discord.gg/chupathingy) or to an
[issue](https://github.com/ChupathingyCE/chupathingyce/issues).

Tell us your system, your graphics card, which build you play (the version
is at the bottom of the main menu), and what you did just before it went
wrong.

## Blank screen or a crash at start

The renderer is the usual cause. Go back to OpenGL:

1. Hold **Shift** (or **Back** on a controller) while the game starts.
2. Go to **SETTINGS**, then the **DISPLAY** tab.
3. Set **RENDERER** to **OPENGL**.
4. Start the game.

If the setup screen doesn't open either, edit `config.toml`
([where it is](settings.md)). Find `renderer` under `[display]` and set it to
`"gl"`, or put a `#` in front of the line.

### Vulkan didn't start

If the VULKAN renderer closes before a map has drawn its first frames, the
next start draws with OpenGL and asks once whether to switch to OpenGL for
good. **Switch to OpenGL** changes the RENDERER setting. **Not now** keeps
VULKAN, so it gets another try next time. Some crashes on AMD graphics and
Mesa are fixed in 0.9.1, but we haven't confirmed it on real AMD cards
yet. If VULKAN still fails for you, tell us and send your logs.

### Older graphics cards

The game skips Vulkan drivers that run badly on older computers and uses
OpenGL. If the game is slow, set **PERFORMANCE** to **POTATO** in the
display settings, and try **LIGHT EFFECTS** at **LOW** or **OFF**. More in
[docs/settings.md](settings.md#renderers).

## The game says your maps are wrong

The `maps` folder needs the Xbox maps from your disc image. Halo PC and
Custom Edition maps go in `maps_ce`, `maps_md` and `maps_pc` beside it. On
Windows, Mac and Linux the setup screen opens on **MAPS** and says what's
missing. See [docs/install.md](install.md#where-your-files-go) and
[docs/maps.md](maps.md).

## Windows or macOS won't open it

The builds aren't code signed yet.

- **Windows**: choose **More info**, then **Run anyway**.
- **Mac**: go to **System Settings > Privacy & Security** and choose
  **Open Anyway**.

## Friends can't join your game

See [If friends can't join](online.md#if-friends-cant-join).

## A map is missing when you join

The game downloads most Custom Edition maps by itself. See
[docs/maps.md](maps.md).

## A setting does nothing

Restart the game after you change `config.toml`. Then look in
`logs/debug.txt` for lines that start with `config.toml` or `settings:`.
See [docs/settings.md](settings.md).

## No update is offered

The game only offers builds newer than yours, from your update channel. A
test build stays until the next newer release comes out. On a Mac, the game
opens the download page instead of updating itself. Packaged installs update
through the package manager. See
[Update channels](install.md#update-channels).
