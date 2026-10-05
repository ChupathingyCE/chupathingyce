# Contributing to ChupathingyCE

ChupathingyCE is a community build of [OpenCE](https://github.com/OpenCommunityEdition/OpenCE). Fixes that belong in OpenCE itself are best sent there; we merge OpenCE regularly.

## Pull requests

- **Open them against `ChupathingyCE/chupathingyce`'s `main`.** This repository is a GitHub fork, so GitHub's "New pull request" page suggests `OpenCommunityEdition/OpenCE` first: change the base repository to ChupathingyCE. With the GitHub CLI, run `gh repo set-default ChupathingyCE/chupathingyce` once in your clone.
- One change per pull request, with how you tested it (which platforms you built and ran).
- The game protocol stays OpenCE's: don't change how games are joined or what's sent on the wire. New networked features go on ChupathingyCE's own channel, falling back silently when the other side is OpenCE.
- The 32-bit builds must stay byte-identical unless the change is meant for them: `tools/port_neutrality_check.py` checks it.
- Commit messages, pull requests and issues are public, and they're posted to the ChupathingyCE Discord.

## Bugs

Use [the bug report form](https://github.com/ChupathingyCE/chupathingyce/issues/new?template=bug.yml), or #bug-reports on the [Discord](https://discord.gg/4BUm2FwuCB).

## Game files

ChupathingyCE never provides game files. Don't commit, attach or link maps, disc images or other copyrighted game data.
