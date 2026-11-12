# LZMA SDK (decoder only)

The LZMA SDK by Igor Pavlov, version 25.01, public domain (see `LICENSE`,
from the SDK's `DOC/lzma-sdk.txt`).

Upstream: https://www.7-zip.org/sdk.html (`lzma2501.7z`). `LzmaDec.c`,
`LzmaDec.h`, `7zTypes.h`, `Compiler.h` and `Precomp.h` are copied unchanged
from its `C/` directory; nothing else of the SDK is used.

The Steam content client (`port/linux/src/steam_content`) unpacks Steam's
LZMA ("VZa") Workshop chunks with it. It builds into the 64-bit desktop
platform layers (macOS, Linux, the dedicated server).
