# Steam content client

Chupathingy's own Steam content client: sign-in by QR code (the Steam app
scans it; the game never sees a password), and Steam Workshop items fetched
from Steam's content servers with every chunk and file checked. The game's
Workshop calls it through `steam_content.h`; the Steam bridge
(`steam_client_bridge.h`, `steam_bridge_*.c`) plugs it in as the installer.

This folder, the Steam bridge files and their tests are licensed under the
MIT license (`LICENSES/MIT.txt`), not the GPL like the rest of the game, so
other projects can reuse them. Please credit Milenko x Horrible Dev and the
Chupathingy project when you do.

It uses Mbed TLS, zlib, Zstandard and the LZMA SDK's decoder, which keep
their own licenses (`port/third_party`).

<!--
SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
-->
