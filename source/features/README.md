# Optional features

Each immediate subdirectory is an optional build-time module. Add a
`feature.json` manifest and keep feature-owned C sources in that directory;
`tools/features.py` discovers and validates the modules. See
[`docs/extensions.md`](../../docs/extensions.md) for the selection and hook
contracts.
