"""Feature modules: discovery, selection and build flags.

A feature is a directory under source/features/ with a feature.json
manifest. configure.py offers --<name> / --no-<name> for each, and the build
generators (linux_build.feature_defines, game_sources) then

- define its macro (HALO_FEATURE_<NAME>) and register its extension: the
  enabled features' names are HALO_EXTENSION_1, HALO_EXTENSION_2, ... in
  "order", and each provides `struct halo_extension const <name>_extension`
  (source/extensions/extension_dispatch.c);
- add its settings to config.toml: with "settings", the manifest's file
  (rows of port_config.c's table) is included by port_config.c as
  HALO_FEATURE_SETTINGS_1, ... ;
- compile its sources (every .c in its directory) only when it is enabled.

Adding a feature is adding its directory; nothing else lists it.

feature.json:

    {
      "name": "bots",                 # [a-z][a-z0-9_]*, the directory's name
      "define": "HALO_FEATURE_BOTS",  # optional, HALO_FEATURE_<NAME>
      "default": false,               # whether built when not asked for
      "order": 20,                    # registry (call) order, lower first
      "help": "...",                  # configure.py --help text
      "extension": true,              # it provides <name>_extension
      "settings": "settings.inc",     # optional, relative to its directory
      "sources": []                   # optional: its units outside its
                                      # directory, compiled always and gated
                                      # by its define (Forge's port units)
    }
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

FEATURES_DIR = Path("source/features")
MANIFEST = "feature.json"
# extension_dispatch.c and port_config.c have this many registry slots
MAXIMUM_EXTENSIONS = 8
NAME = re.compile(r"^[a-z][a-z0-9_]*$")
# names the C preprocessor may already define (a slot's value is expanded)
RESERVED_NAMES = {"linux", "unix", "i386", "x86_64", "arm", "aarch64", "apple", "windows", "android"}


@dataclass(frozen=True)
class Feature:
    name: str
    directory: Path
    define: str
    default: bool
    order: int
    help: str
    extension: bool
    settings: Path | None
    sources: tuple[Path, ...] = field(default=())

    @property
    def manifest(self) -> Path:
        return self.directory / MANIFEST

    @property
    def settings_include(self) -> str | None:
        """the settings file as port_config.c includes it (from source/)"""
        if not self.settings:
            return None
        return self.settings.relative_to(FEATURES_DIR.parent).as_posix()


def _load(manifest: Path) -> Feature:
    try:
        data: dict[str, Any] = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"{manifest}: cannot read the manifest: {error}") from error
    if not isinstance(data, dict):
        raise ValueError(f"{manifest}: the manifest must be a JSON object")
    name = data.get("name", "")
    if name != manifest.parent.name:
        raise ValueError(f"{manifest}: name {name!r} must be its directory's, {manifest.parent.name!r}")
    if not NAME.match(name) or name in RESERVED_NAMES:
        raise ValueError(f"{manifest}: name {name!r} must match {NAME.pattern} and not be a predefined macro")
    settings = data.get("settings")
    if settings is not None:
        if settings != "settings.inc":
            raise ValueError(f"{manifest}: settings must be settings.inc in the feature directory")
        settings = manifest.parent / settings
        if not settings.is_file():
            raise ValueError(f"{manifest}: settings file {settings} is missing")
        if settings.name != "settings.inc":
            raise ValueError(f"{manifest}: the settings file must be named settings.inc")
    sources = tuple(Path(source) for source in data.get("sources", []))
    for source in sources:
        if not source.is_file():
            raise ValueError(f"{manifest}: source {source} is missing")
    define = data.get("define", f"HALO_FEATURE_{name.upper()}")
    if not isinstance(define, str) or not re.fullmatch(r"[A-Z][A-Z0-9_]*", define):
        raise ValueError(f"{manifest}: define must be an uppercase C identifier")
    for key in ("default", "extension"):
        if key in data and not isinstance(data[key], bool):
            raise ValueError(f"{manifest}: {key} must be a boolean")
    return Feature(
        name=name,
        directory=manifest.parent,
        define=define,
        default=bool(data.get("default", False)),
        order=_integer(manifest, data, "order", 100),
        help=data.get("help", ""),
        extension=bool(data.get("extension", True)),
        settings=settings,
        sources=sources,
    )


def _integer(manifest: Path, data: dict[str, Any], key: str, default: int) -> int:
    value = data.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{manifest}: {key} must be an integer")
    return value


def discover(root: Path = FEATURES_DIR) -> list[Feature]:
    """every feature, in registry order"""
    if not root.is_dir():
        return []
    features = [_load(manifest) for manifest in sorted(root.glob(f"*/{MANIFEST}"))]
    return sorted(features, key=lambda feature: (feature.order, feature.name))


def manifests(root: Path = FEATURES_DIR) -> list[Path]:
    """the manifests (configure.py reruns when one changes)"""
    return sorted(root.glob(f"*/{MANIFEST}")) if root.is_dir() else []


def option_attribute(feature: Feature) -> str:
    """the configure.py argument's attribute (and sln's: feature_<name>)"""
    return f"feature_{feature.name}"


def enabled(sln: Any, features: list[Feature] | None = None) -> list[Feature]:
    """the features this configuration builds, in registry order"""
    features = discover() if features is None else features
    if sln is None:
        return []
    return [feature for feature in features
            if getattr(sln, option_attribute(feature), feature.default)]


def defines(sln: Any) -> list[str]:
    """the compiler flags of this configuration's features: their macros,
    the extension registry and the settings files"""
    selected = enabled(sln)
    extensions = [feature for feature in selected if feature.extension]
    settings = [feature for feature in selected if feature.settings]
    if len(extensions) > MAXIMUM_EXTENSIONS or len(settings) > MAXIMUM_EXTENSIONS:
        raise ValueError(f"more than {MAXIMUM_EXTENSIONS} features: add slots to extension_dispatch.c, "
                         "port_config.c and tools/features.py")
    return ([f"-D{feature.define}" for feature in selected]
            + [f"-DHALO_EXTENSION_{index}={feature.name}" for index, feature in enumerate(extensions, 1)]
            + [f"-DHALO_FEATURE_SETTINGS_{index}={feature.name}" for index, feature in enumerate(settings, 1)])


def excluded_sources(sln: Any) -> set[str]:
    """the sources of the features this configuration leaves out (their
    directories' units; units elsewhere stay, gated by the define)"""
    chosen = {feature.name for feature in enabled(sln)}
    return {source.as_posix()
            for feature in discover() if feature.name not in chosen
            for source in feature.directory.rglob("*.c")}


def add_arguments(parser: Any) -> None:
    """configure.py's --<name> / --no-<name> for every feature"""
    import argparse

    for feature in discover():
        state = "on unless --no-" if feature.default else "off unless --"
        parser.add_argument(
            f"--{feature.name.replace('_', '-')}",
            dest=option_attribute(feature),
            action=argparse.BooleanOptionalAction,
            default=feature.default,
            help=f"{feature.help} ({feature.define}; {feature.directory.as_posix()}); {state}{feature.name}",
        )


def selection(args: Any) -> dict[str, bool]:
    """the parsed arguments' choices, as sln's attributes"""
    return {option_attribute(feature): getattr(args, option_attribute(feature)) for feature in discover()}
