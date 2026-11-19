# Lua

The Lua programming language's library, release 5.4.8 (2025-05-21), by
Lua.org, PUC-Rio, MIT licensed (see `LICENSE`).

Upstream: https://www.lua.org/ftp/lua-5.4.8.tar.gz (SHA-256
4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae). The
`src` folder's C files and headers are copied unchanged, without the
standalone interpreter and compiler (`lua.c`, `luac.c`) and the Makefile.

It runs mods' Lua scripts written for Chimera's Lua API
(`port/linux/game/lua_scripting.c`); it builds into the desktop builds only.
