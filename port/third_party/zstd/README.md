# Zstandard (decompression only)

Zstandard, by Meta Platforms, Inc. and contributors, BSD-licensed (see
`LICENSE`; zstd is dual-licensed BSD or GPLv2, and the port takes it under
the BSD license).

Upstream: https://github.com/facebook/zstd, release `v1.5.7`,
`zstd-1.5.7.tar.gz`, SHA-256
`eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`.

Only the decoder is kept, unchanged and in its own folders: `lib/zstd.h`,
`lib/zstd_errors.h`, the headers of `lib/common` that the decoder includes
with `debug.c`, `entropy_common.c`, `error_private.c`, `fse_decompress.c`,
`xxhash.c` and `zstd_common.c`, and `lib/decompress` without its assembly
(`huf_decompress_amd64.S`). It is built with `ZSTD_DISABLE_ASM=1` and
`ZSTD_LEGACY_SUPPORT=0`. To update, copy the same files from a newer
release.

The desktop builds use it to read Custom Edition maps compressed with
Chimera's `chimera-compress` (`port/linux/src/map_compressed.c`).
