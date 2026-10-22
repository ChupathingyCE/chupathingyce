# Contributing to Chupathingy

Chupathingy is a port of the Halo: Combat Evolved decompilation, built for stability and community games. It has its own code and releases, and takes changes through pull requests here.

## Getting involved

You don't have to write code to help.

- **Play and test.** New builds that need trying are in the Testing column of the [roadmap](https://github.com/orgs/ChupathingyCE/projects/1). Pick up the Testers role in the Discord to get pinged when one is ready.
- **Report bugs and share ideas** with the [bug report form](https://github.com/ChupathingyCE/chupathingyce/issues/new?template=bug.yml), or in #bug-reports and #ideas on the [Discord](https://discord.gg/chupathingy). New reports go into the roadmap's Inbox and get sorted each week.
- **Write code.** Issues labeled [good first issue](https://github.com/ChupathingyCE/chupathingyce/labels/good%20first%20issue) are small and self-contained, and each one says where to start. [help wanted](https://github.com/ChupathingyCE/chupathingyce/labels/help%20wanted) issues are a bit bigger. Ask in #dev on the Discord if you get stuck.
- **Host a server, make maps, or help run events.** Ask for the Server Hosts, Mappers or Game Night role in the Discord.

Contributors are credited in the release notes.

## Pull requests

- **Open them against `ChupathingyCE/chupathingyce`'s `main`.**
- One change per pull request, with how you tested it (which platforms you built and ran).
- The shared game protocol stays as it is, so cross-play keeps working: don't change how games are joined or what's sent on the wire. New networked features go on Delta, Chupathingy's own channel, falling back silently when the other side doesn't speak it.
- The 32-bit builds must stay byte-identical unless the change is meant for them: `tools/port_neutrality_check.py` checks it.
- Commit messages, pull requests and issues are public, and they're posted to the Chupathingy Discord.

## License

Chupathingy is licensed under the GNU GPL, version 3 or later (`GPL-3.0-or-later`, [LICENSE](LICENSE)). By opening a pull request, you agree that your contribution is under the same license, with the console SDK permission in the README's [License](README.md#license) section, so that it can also be built into Warthog.

- Code or assets from somewhere else must be under a license that works with the GPL (MIT, BSD, zlib, Apache-2.0, CC0, SIL OFL for fonts, and so on), credited, with the license beside them (`port/third_party` for code). Say where they come from in the pull request.

## Using AI tools

You can use AI tools when you contribute. You're responsible for what you submit: understand every line, test it, and be able to explain it in review. Say in the pull request if a large part of it was generated. Pull requests that are mostly generated and untested will be closed.

## Versions

- **`0.MINOR.0`** is a major release, with a theme.
- **`0.MINOR.PATCH`** is a regular release, with features and fixes.
- **A letter after it** is a hotfix of that release: crash, security or network fixes only, no features. Letters start at `a`, in order: 0.7.5, then 0.7.5a, 0.7.5b.
- **1.0.0** comes once the game is feature-complete and stable on every platform.

Test builds are the `nightly` release and `-testing` tags, outside the numbering. The network version is separate from the release version: a hotfix changes it only when cross-play needs it.

## Bugs

Use [the bug report form](https://github.com/ChupathingyCE/chupathingyce/issues/new?template=bug.yml), or #bug-reports on the [Discord](https://discord.gg/chupathingy).

## Game files

Chupathingy never provides game files. Don't commit, attach or link maps, disc images or other copyrighted game data.
