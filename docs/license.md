# License

Chupathingy is free software: you can redistribute it and modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License or (at your option) any later
version (`GPL-3.0-or-later`). It comes with no warranty. The full text is in
[LICENSE](../LICENSE).

- **From 0.8.0.** The GPL applies from the commit that made the change
  onward, and so from release 0.8.0. Releases up to and including 0.7.5d, and
  the source at every commit before the change, were released under
  [CC0 1.0](../LICENSES/CC0-1.0.txt) and stay that way.
- **What Chupathingy builds on.** The Halo CE decompilation
  ([punpckhdq/halo](https://github.com/punpckhdq/halo) and
  [bnunu/halo-1](https://github.com/bnunu/halo-1)) and the port Chupathingy
  began from are released under CC0 1.0, which lets anyone build on them
  under any terms. Their repositories stay CC0; Chupathingy's code, including
  its changes to theirs, is GPL from 0.8.0.
- **Delta.** Delta's code is GPL from 0.8.0 like the rest; earlier
  versions of it stay CC0 for anyone already using them. The protocol
  itself ([docs/delta.md](delta.md)) is a description anyone may
  implement, under any license, without using this code.
- **Third-party code, fonts and art** keep their own licenses, which are
  beside them (`port/third_party`, `port/assets/fonts`,
  `port/linux/ui/fonts`, `port/assets/prompts`, `port/assets/hud`,
  `port/assets/hrtf`), and the release downloads
  carry their notices.
- **Halo itself is not covered.** The license covers the work of
  Chupathingy's authors. It gives no rights in Halo, its maps or anything
  else Microsoft owns.
- **Source.** Each release's source is its tag in this repository.

## Console SDK permission

Chupathingy's code is also built for consoles by
Warthog ([warthog-xbox](https://github.com/ChupathingyCE/warthog-xbox),
[warthog-360](https://github.com/ChupathingyCE/warthog-360),
[warthog-wiiu](https://github.com/ChupathingyCE/warthog-wiiu) and
[warthog-switch](https://github.com/ChupathingyCE/warthog-switch)), and anyone may build
Warthog themselves with a console maker's SDK. This permission covers that:

> **Additional permission under GNU GPL version 3 section 7**
>
> If you modify this Program, or any covered work, by linking or combining
> it with the libraries of a Microsoft Xbox or Xbox 360 software development
> kit (such as the Xbox Development Kit and the Xbox 360 SDK, including
> their `xnet.lib` and Direct3D libraries), or a modified version of those
> libraries, containing parts covered by the terms of those kits' licenses,
> the licensors of this Program grant you additional permission to convey
> the resulting work. Corresponding Source for a non-source form of such a
> combination shall include the source code of the covered work, and need
> not include the source code of those libraries.

The permission only lifts the GPL's own objection. It grants no rights in
Microsoft's libraries, whose own terms still apply, and you may remove it
from your copies (section 7). Chupathingy's and Warthog's own published
builds are made only with free tools and libraries.
