# Build-time feature extensions

Extensions are statically linked feature modules. There is no plugin loader or
binary ABI. The game calls permanent hooks declared in
`source/extensions/extension_api.h`; `source/extensions/extension_dispatch.c`
keeps feature code behind those hooks, so the engine does not name individual
features.

## Selecting a feature

Each module has a directory under `source/features/` and a `feature.json`
manifest. `configure.py` discovers manifests and offers `--<name>` and
`--no-<name>` switches. A selected module can provide:

- a compile-time feature define;
- a descriptor registered in manifest order;
- settings rows included in `port_config.c`'s setting table; and
- C sources compiled only while the feature is selected.

Features are off by default unless their manifest says otherwise. Registry and
settings tables have eight fixed slots; discovery rejects a selection that
would exceed those limits. A disabled feature's source files are excluded from
the build rather than compiled as inert units.

A minimal manifest is:

```json
{
  "name": "experiment",
  "default": false,
  "order": 40,
  "help": "Experimental local feature",
  "extension": true
}
```

An extension supplies one constant descriptor:

```c
#include "extensions/extension_api.h"

static void experiment_objects_placed(void)
{
    /* Feature-owned map setup. */
}

struct halo_extension const experiment_extension =
{
    .name = "experiment",
    .objects_placed = experiment_objects_placed,
};
```

If there is no descriptor, set `"extension": false`. A `settings.inc`, when
present, contains initializer rows using the existing `config_setting` schema
in `port_config.c`; it is included only for selected manifests. `sources` may
name units outside the feature directory when their implementation has to
remain in an existing build-discovery path. Such units must gate feature-only
code with the feature's compile-time define.

## Hook contracts

- Descriptors are constant, and callbacks run on the game thread.
- Lifecycle notifications run in registry order. Suppression hooks use
  veto-wins semantics.
- A ruleset claims a session only during engine selection. Its provided hooks
  replace the corresponding stock operation; absent hooks fall through. A
  local-only ruleset has no network-state representation.
- Player controllers update before player actions. They do not change host
  authority or create a network protocol.
- Editors arbitrate ownership in registry order. The active editor owns input,
  update and camera until it releases them; inactive editors may poll for
  activation. World-geometry hooks are additive and do not change tag storage.
- A new engine event still needs a permanent, carefully placed call site. The
  engine sources should not acquire direct references to a particular feature.

The dispatch tests exercise empty registries, precedence, lifecycle callbacks,
editor ownership, world geometry, and fallthrough. Build and test with:

```sh
python3 configure.py
ninja macos
uv run --no-project --with pytest python -m pytest -q tools/test_features.py
```
