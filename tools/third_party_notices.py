#!/usr/bin/env python3
"""Writes THIRD-PARTY-NOTICES.txt: the notices and license texts of the
other people's code, fonts and data built into a download, as their licenses
ask, in one file beside the game's own LICENSE.txt. Every package carries
those two files and no other license file (tools/ci_build.py; the Android
app has them in its assets, tools/android_build.py and android64_build.py).

    python tools/third_party_notices.py OUTPUT [--package game|server]
        [--with sdl3,bgfx,vulkan,spirv-cross,appimage,vulkan-android]

What it lists is port/notices/components.json: one entry per component, the
downloads it is in and the license files in the repository whose texts it
carries. --with names the parts only some builds have (the SDL3 library
beside the game, the bgfx renderer, its Vulkan back end, its SPIRV-Cross,
the AppImage's own runtime: tools/linux_appimage.py writes the
AppImage's notices again with it, and the 64-bit Android app's Vulkan
renderer). The header says where
the source is: this build's tag for a release, else its commit
(tools/version.py). tools/test_third_party_notices.py checks that every
library in port/third_party and every bundled font and data set has an entry.
"""

import argparse
import glob
import json
import sys
import textwrap
from pathlib import Path
from typing import Any, Dict, Iterable, List

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools.version import commit, commit_inputs, release_build, version  # noqa: E402

MANIFEST = ROOT / "port/notices/components.json"
NAME = "THIRD-PARTY-NOTICES.txt"
REPOSITORY = "https://github.com/ChupathingyCE/chupathingyce"
FEATURES = ("sdl3", "bgfx", "vulkan", "spirv-cross", "appimage", "vulkan-android")
PACKAGES = ("game", "server")
RULE = "=" * 78


def manifest() -> Dict:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def components(package: str, features: Iterable[str]) -> List[Dict]:
    """the manifest's entries in one package, given the build's features"""
    features = set(features)
    return [entry for entry in manifest()["components"]
            if package in entry["packages"] and entry.get("requires", None) in (None, *features)]


def inputs() -> List[Path]:
    """every file the notices are made from (for the builds' rules)"""
    data = manifest()
    files = {MANIFEST, Path(__file__).resolve()}
    files.update(ROOT / text["file"] for text in data["shared_texts"].values())
    for entry in data["components"]:
        files.update(ROOT / name for name in entry.get("files", []))
    return sorted(files)


def read_text(path: str) -> str:
    # (as it is, its line ends made \n and its end one newline)
    text = (ROOT / path).read_text(encoding="utf-8-sig").replace("\r\n", "\n")
    return text.strip("\n").rstrip() + "\n"


def wrap(text: str, indent: str = "") -> List[str]:
    """prose at the file's width (78)"""
    return textwrap.wrap(text, 78, initial_indent=indent, subsequent_indent=indent or "",
                         break_long_words=False, break_on_hyphens=False)


def source_line() -> str:
    """where this build's source is: a release's tag (v<version>), else its
    commit (a nightly's, a test build's or anyone's own)"""
    if release_build():
        return f"{REPOSITORY}/tree/v{version()}"
    return f"{REPOSITORY}/tree/{commit()}"


def header(package: str) -> str:
    what = "the Chupathingy dedicated server" if package == "server" else "Chupathingy"
    return "\n".join([
        "THIRD-PARTY NOTICES",
        "",
        f"This is the list of other people's code, fonts and data built into {what}",
        f"{version()} (commit {commit()}), with the copyright notices and license texts",
        "their licenses ask a download to carry.",
        "",
        "Chupathingy itself is free software under the GNU General Public License,",
        "version 3 or later: see LICENSE.txt beside this file.",
        "",
        "The source code, with each component's license beside it:",
        f"  {REPOSITORY}",
        "The source of this build:",
        f"  {source_line()}",
        "",
        "Halo is a trademark of Microsoft. No game files are included, and none of",
        "these licenses gives any rights in Halo.",
        "",
    ])


def render(package: str, features: Iterable[str]) -> str:
    if package not in PACKAGES:
        raise ValueError(f"package {package}: not one of {', '.join(PACKAGES)}")
    unknown = set(features) - set(FEATURES)
    if unknown:
        raise ValueError(f"unknown features: {', '.join(sorted(unknown))}")
    data = manifest()
    entries = components(package, features)
    out = [header(package), "CONTENTS", ""]
    out += [f"  {index}. {entry['name']}" for index, entry in enumerate(entries, 1)]
    shared = sorted({entry["shared"] for entry in entries if entry.get("shared")})
    if shared:
        out.append(f"  {len(entries) + 1}. License texts used by more than one component")
    out.append("")
    for index, entry in enumerate(entries, 1):
        out += [RULE, f"{index}. {entry['name']}", RULE, ""]
        out += wrap(f"Used for: {entry['use']}")
        out += wrap(f"Source: {entry['source']}")
        out += wrap(f"License: {entry['license']}")
        out.append("")
        for line in entry["copyright"]:
            out += wrap(line)
        out.append("")
        if entry.get("credit"):
            out += wrap(entry["credit"]) + [""]
        if entry.get("shared"):
            title = data["shared_texts"][entry["shared"]]["title"]
            out += wrap(f"The full text of the {title} is in section {len(entries) + 1}, at the end of this "
                        "file.") + [""]
        for name in entry.get("files", []):
            out += [f"--- {name} ---", "", read_text(name)]
    if shared:
        out += [RULE, f"{len(entries) + 1}. License texts used by more than one component", RULE, ""]
        for key in shared:
            text = data["shared_texts"][key]
            users = ", ".join(entry["name"] for entry in entries if entry.get("shared") == key)
            out += [f"--- {text['title']} ({users}) ---", "", read_text(text["file"])]
    return "\n".join(out).rstrip() + "\n"


def features_in(path: Path) -> List[str]:
    """the features a written THIRD-PARTY-NOTICES.txt was made with, from its
    contents list (tools/linux_appimage.py adds the AppImage's to the
    linux64 build's)"""
    text = path.read_text(encoding="utf-8")
    listed = {line.split(". ", 1)[1].strip() for line in text.split(RULE, 1)[0].splitlines()
              if line.startswith("  ") and ". " in line}
    return [feature for feature in FEATURES
            if any(entry.get("requires") == feature and entry["name"] in listed for entry in manifest()["components"])]


def write(output: Path, package: str = "game", features: Iterable[str] = ()) -> Path:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(render(package, features).encode("utf-8"))
    return output


def ninja_build(n: Any, prefix: str, directory: Path, features: Iterable[str]) -> List[Path]:
    """the rules that put LICENSE.txt and THIRD-PARTY-NOTICES.txt (the game's)
    in directory, for a build whose package is made elsewhere (the Android
    app's assets); returns both"""
    features = list(features)
    notices, license_copy = directory / NAME, directory / "LICENSE.txt"
    n.rule(name=f"{prefix}_notices",
           command=f"$python tools/third_party_notices.py $out --package game --with {','.join(features)}",
           description=f"{prefix.upper()} NOTICES $out")
    n.build(outputs=notices, rule=f"{prefix}_notices",
            implicit=[path.relative_to(ROOT) for path in inputs()] + commit_inputs())
    n.rule(name=f"{prefix}_license", command="cp $in $out", description=f"{prefix.upper()} STAGE $out")
    n.build(outputs=license_copy, rule=f"{prefix}_license", inputs=Path("LICENSE"))
    return [license_copy, notices]


def check_fetched() -> List[str]:
    """the libraries a build downloads (SDL3, bgfx's): where the build's own
    copy of a license is not in port/notices' (or the shared text), a
    warning to update ours"""
    warnings = []
    data = manifest()
    for entry in data["components"]:
        if not entry.get("fetched"):
            continue
        ours = entry.get("files") or [data["shared_texts"][entry["shared"]]["file"]]
        for pattern in entry.get("fetched", []):
            for found in sorted(glob.glob(str(ROOT / pattern))):
                theirs = Path(found).read_text(encoding="utf-8-sig", errors="replace").replace("\r\n", "\n")
                # (the same words, or all of theirs in ours: the ASTC encoder's
                # Apache License without its appendix)
                if not any(" ".join(theirs.split()) in " ".join(read_text(name).split()) for name in ours):
                    warnings.append(f"{Path(found).relative_to(ROOT)} differs from {', '.join(ours)}: "
                                    f"update port/notices for {entry['name']}")
    return warnings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("output", type=Path)
    parser.add_argument("--package", choices=PACKAGES, default="game")
    parser.add_argument("--with", dest="features", default="",
                        help=f"comma-separated: {', '.join(FEATURES)}")
    args = parser.parse_args()
    features = [name for name in args.features.split(",") if name]
    write(args.output, args.package, features)
    return 0


if __name__ == "__main__":
    sys.exit(main())
