#!/usr/bin/env python3
"""The macOS Metal build's source manifest and upstream contract.

port/macos/metal/manifest.json says, for the Metal build, what becomes of
every upstream platform source (port/linux/src, ChupathingyCE's): compiled
unchanged (shared), replaced by a Mac file, or dropped; and lists every Mac
file. tools/macos_build.py builds from it and from nothing else, so a new or
renamed file on either side is an error, not a silent change of build.

  check [--against REF]   the manifest's structure, and upstream drift: Mac
                          forks whose upstream file changed since they were
                          last reconciled (exit 1 on anything new)
  accept FILE [--seen]    after review, record the current upstream FILE as
                          reconciled (or, with --seen, as acknowledged drift
                          still to reconcile); identical copies are re-copied
  symbols                 (ninja) every symbol the replaced and dropped
                          upstream files define is defined by the Metal build,
                          or listed in not_provided with a reason
"""
import argparse
import json
import re
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from collections.abc import Iterable

ROOT = Path(__file__).resolve().parents[1]
METAL = Path("port/macos/metal")
MANIFEST = METAL / "manifest.json"
# the Mac source folders, searched (in this order) before upstream's
MAC_DIRS = ("xbox_compat", "shaders", "platform", "renderer")
MAC_SUFFIXES = (".c", ".h", ".inc", ".m")


class ManifestError(Exception):
    def __init__(self, problems: list[str]) -> None:
        super().__init__("\n".join(problems))
        self.problems = problems


@dataclass
class Entry:
    upstream: str
    why: str
    mac: str | None = None       # replacement/shadow, relative to METAL
    base: str | None = None      # checked-out platform blob last reconciled
    upstream_base: str | None = None  # reviewed canonical upstream variant, if different
    seen: str | None = None      # upstream blob acknowledged, not reconciled
    identical: bool = False

    def baseline(self, against: str | None) -> str | None:
        return self.upstream_base if against and self.upstream_base else self.base


@dataclass
class Manifest:
    upstream: Path
    replaced: dict[str, Entry]
    dropped: dict[str, Entry]
    shared: list[str]
    headers: dict[str, Entry]
    mac_sources: dict[str, str]
    objc: dict[str, str]
    mac_headers: list[str]
    tools: dict[str, str]
    not_provided: dict[str, str]
    raw: dict = field(repr=False, default_factory=dict)

    def entries(self) -> list[Entry]:
        return [*self.replaced.values(), *self.dropped.values(), *self.headers.values()]

    def mac_files(self) -> list[str]:
        """every Mac file the manifest names, relative to METAL"""
        forks = [e.mac for e in (*self.replaced.values(), *self.headers.values()) if e.mac is not None]
        return forks + list(self.mac_sources) + list(self.objc) + self.mac_headers + list(self.tools)


def _entries(section: dict) -> dict[str, Entry]:
    return {name: Entry(upstream=name, why=value.get("why", ""), mac=value.get("with"), base=value.get("base"),
                        upstream_base=value.get("upstream_base"), seen=value.get("seen"),
                        identical=bool(value.get("identical")))
            for name, value in section.items() if name != "comment"}


def load(root: Path = ROOT) -> Manifest:
    try:
        raw = json.loads((root / MANIFEST).read_text())
    except (OSError, ValueError) as error:
        raise ManifestError([f"cannot read {MANIFEST}: {error}"]) from error
    mac_only = raw["mac_only"]
    return Manifest(
        upstream=Path(raw["upstream"]),
        replaced=_entries(raw["replaced"]),
        dropped=_entries(raw["dropped"]),
        shared=list(raw["shared"]["files"]),
        headers=_entries(raw["headers"]),
        mac_sources=dict(mac_only["sources"]),
        objc=dict(mac_only["objc"]),
        mac_headers=list(mac_only["headers"]),
        tools=dict(mac_only["tools"]),
        not_provided={k: v for k, v in raw["not_provided"].items() if k != "comment"},
        raw=raw,
    )


def structure_problems(manifest: Manifest, root: Path = ROOT) -> list[str]:
    """what makes the manifest not describe the tree: each a sentence that
    says what to do"""
    problems = []
    upstream_dir = root / manifest.upstream
    classified: dict[str, list[str]] = {}
    classes: tuple[tuple[str, Iterable[str]], ...] = (("replaced", manifest.replaced.keys()),
                                                     ("dropped", manifest.dropped.keys()), ("shared", manifest.shared))
    for kind, names in classes:
        for name in names:
            classified.setdefault(name, []).append(kind)
    for name, kinds in sorted(classified.items()):
        if len(kinds) > 1:
            problems.append(f"{manifest.upstream / name} is listed as {' and '.join(kinds)}: pick one")
        if not (upstream_dir / name).is_file():
            problems.append(f"{manifest.upstream / name} is in the manifest but not upstream (renamed or "
                            "removed?): update port/macos/metal/manifest.json")
    for source in sorted(upstream_dir.glob("*.c")):
        if source.name not in classified:
            problems.append(f"new upstream source {manifest.upstream / source.name}: add it to manifest.json "
                            "as shared (compiled unchanged into the Metal build), replaced or dropped")
    for name in manifest.headers:
        if not (upstream_dir / name).is_file():
            problems.append(f"{manifest.upstream / name} is shadowed in the manifest but not upstream: update it")

    listed: dict[str, int] = {}
    for path in manifest.mac_files():
        listed[path] = listed.get(path, 0) + 1
    for path, count in sorted(listed.items()):
        if count > 1:
            problems.append(f"{METAL / path} is listed {count} times in manifest.json")
        if not (root / METAL / path).is_file():
            problems.append(f"{METAL / path} is in manifest.json but does not exist")
    on_disk = sorted(str(p.relative_to(root / METAL)) for d in MAC_DIRS for p in (root / METAL / d).rglob("*")
                     if p.is_file() and p.suffix in MAC_SUFFIXES)
    for path in on_disk:
        if path not in listed:
            problems.append(f"{METAL / path} is not in manifest.json: list it (a Mac-only source or header, "
                            "or the replacement of an upstream file)")
    # one folder's header must not hide another's: they are all on the
    # include path, and quoted includes from another folder search it in order
    by_name: dict[str, list[str]] = {}
    for path in on_disk:
        by_name.setdefault(Path(path).name, []).append(path)
    for name, paths in sorted(by_name.items()):
        if len(paths) > 1:
            problems.append(f"{name} exists in more than one Mac folder ({', '.join(paths)}): includes are "
                            "ambiguous; rename one")
    for entry in manifest.entries():
        if entry.mac is None and entry.upstream not in manifest.dropped:
            problems.append(f"{entry.upstream}: say which Mac file replaces it in \"with\"")
        if entry.identical and entry.base:
            problems.append(f"{entry.upstream}: an identical copy has no base pin; remove \"base\"")
        if not entry.identical and not entry.base:
            problems.append(f"{entry.upstream}: a fork needs \"base\" (the upstream blob it was reconciled with)")
        if not entry.why:
            problems.append(f"{entry.upstream}: say why in \"why\"")
    return problems


def blob_id(data: bytes) -> str:
    """Git's content identifier, using the repository's object format."""
    return subprocess.run(["git", "hash-object", "--stdin"], input=data, cwd=ROOT,
                          capture_output=True, check=True).stdout.decode().strip()


def git(*args: str, root: Path = ROOT) -> str:
    return subprocess.run(["git", *args], cwd=root, capture_output=True, text=True, check=True).stdout


def upstream_blob(manifest: Manifest, name: str, against: str | None, root: Path = ROOT) -> str | None:
    path = manifest.upstream / name
    if against is None:
        file = root / path
        return blob_id(file.read_bytes()) if file.is_file() else None
    try:
        return git("rev-parse", f"{against}:{path.as_posix()}", root=root).strip()
    except subprocess.CalledProcessError:
        return None


def missed_commits(path: Path, base: str, tip: str, root: Path = ROOT, limit: int = 40) -> list[str]:
    """the commits on tip that changed path since its blob was base, newest first"""
    lines = []
    for line in git("log", "--format=%H %h %s", tip, "--", path.as_posix(), root=root).splitlines():
        commit, short, subject = line.split(" ", 2)
        try:
            if git("rev-parse", f"{commit}:{path.as_posix()}", root=root).strip() == base:
                return lines
        except subprocess.CalledProcessError:
            pass
        lines.append(f"{short} {subject}")
        if len(lines) >= limit:
            return lines + ["..."]
    return lines


@dataclass
class Drift:
    entry: Entry
    current: str | None
    known: bool          # the drift was acknowledged (seen) and has not moved on

    def describe(self, manifest: Manifest, against: str | None, root: Path = ROOT) -> str:
        e = self.entry
        path = manifest.upstream / e.upstream
        mac = f"port/macos/metal/{e.mac}" if e.mac else "(dropped)"
        if self.current is None:
            return f"{path} no longer exists upstream; {mac} must be reviewed and the manifest updated"
        if e.identical:
            return (f"{mac} must stay identical to {path}, which changed: review it, then "
                    f"`python3 tools/metal_manifest.py accept {e.upstream}` copies it")
        base = e.baseline(against)
        assert base is not None  # structure_problems requires a fork's baseline
        state = "known drift, not yet reconciled" if self.known else "CHANGED upstream since last reconciled"
        current = f"{against}:{path.as_posix()}" if against else path.as_posix()
        text = [f"{path} -> {mac}: {state}",
                f"    review: git diff {base[:12]} {self.current[:12]}" if against else
                f"    review: git show {base[:12]} | diff -u - {current}"]
        try:
            commits = missed_commits(path, base, against or "HEAD", root)
        except subprocess.CalledProcessError:
            commits = []
        if commits:
            text.append("    upstream commits since: " + "; ".join(commits[:8]) + (" ..." if len(commits) > 8 else ""))
        if not self.known:
            action = f" --against {against}" if against else "   (or --seen to defer)"
            text.append(f"    then: python3 tools/metal_manifest.py accept {e.upstream}{action}")
        return "\n".join(text)


def reference_problems(manifest: Manifest, against: str, root: Path = ROOT) -> list[str]:
    """A preview must also catch new upstream sources not yet checked out."""
    files = git("ls-tree", "--name-only", f"{against}:{manifest.upstream.as_posix()}", root=root).splitlines()
    classified = set(manifest.shared) | set(manifest.replaced) | set(manifest.dropped)
    problems = [f"new upstream source {manifest.upstream / name} at {against}: classify it in manifest.json"
                for name in files if name.endswith(".c") and name not in classified]
    problems += [f"upstream source {manifest.upstream / name} is absent at {against}: review its classification"
                 for name in sorted(classified - set(files))]
    return problems


def drift(manifest: Manifest, against: str | None = None, root: Path = ROOT) -> list[Drift]:
    result = []
    for entry in manifest.entries():
        current = upstream_blob(manifest, entry.upstream, against, root)
        if entry.identical:
            if current is None or entry.mac is None:
                result.append(Drift(entry, current, False))
                continue
            if blob_id((root / METAL / entry.mac).read_bytes()) != current:
                result.append(Drift(entry, current, False))
        elif current != entry.baseline(against):
            result.append(Drift(entry, current, current is not None and current == entry.seen))
    return result


def accept(name: str, seen: bool, root: Path = ROOT, against: str | None = None) -> str:
    manifest = load(root)
    entry = next((e for e in manifest.entries() if e.upstream == name), None)
    if entry is None:
        raise SystemExit(f"{name} is not a replaced, dropped or shadowed upstream file in the manifest")
    upstream = root / manifest.upstream / name
    if against and (seen or entry.identical):
        raise SystemExit("--against records a reviewed canonical fork baseline; not valid with --seen or identical copies")
    if entry.identical:
        if entry.mac is None:
            raise SystemExit(f"{name} has no Mac copy to update")
        if seen:
            raise SystemExit(f"{name} is an identical copy: there is no deferred state; accept copies it")
        shutil.copyfile(upstream, root / METAL / entry.mac)
        return f"copied {manifest.upstream / name} to {METAL / entry.mac}"
    current = upstream_blob(manifest, name, against, root)
    if current is None:
        raise SystemExit(f"cannot read {name} at {against or 'the checkout'}")
    text = (root / MANIFEST).read_text()
    block = re.search(rf'\n    "{re.escape(name)}": \{{.*?\n    \}}', text, re.S)
    if not block:
        raise SystemExit(f"cannot find {name}'s entry in {MANIFEST}")
    body = block.group(0)
    if against:
        if '"upstream_base":' in body:
            body = re.sub(r'"upstream_base": "[0-9a-f]+"', f'"upstream_base": "{current}"', body)
        else:
            body = re.sub(r'("base": "[0-9a-f]+",)', rf'\1\n      "upstream_base": "{current}",', body)
        message = f"{name}: reviewed canonical {against} at {current[:12]} (checkout pin unchanged)"
    elif seen:
        if '"seen":' in body:
            body = re.sub(r'"seen": "[0-9a-f]+"', f'"seen": "{current}"', body)
        else:
            body = re.sub(r'("base": "[0-9a-f]+",)', rf'\1\n      "seen": "{current}",', body)
        message = f"{name}: drift up to {current[:12]} acknowledged (still to reconcile)"
    else:
        body = re.sub(r'"base": "[0-9a-f]+"', f'"base": "{current}"', body)
        body = re.sub(r'\n      "seen": "[0-9a-f]+",', "", body)
        message = f"{name}: reconciled with upstream {current[:12]}"
    (root / MANIFEST).write_text(text[:block.start()] + body + text[block.end():])
    return message


def defined_symbols(objects: list[Path]) -> set[str]:
    """external symbols the objects define (nm; LLVM bitcode included)"""
    symbols: set[str] = set()
    for chunk in range(0, len(objects), 200):
        out = subprocess.run(["nm", "-gU", *map(str, objects[chunk:chunk + 200])],
                             capture_output=True, text=True, check=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[-2] in "TDBSCtdbs":
                name = parts[-1]
                symbols.add(name[1:] if name.startswith("_") else name)
    return symbols


def symbol_problems(manifest: Manifest, upstream_objects: list[Path], metal_objects: list[Path]) -> list[str]:
    metal = defined_symbols(metal_objects)
    problems = []
    for obj in upstream_objects:
        for name in sorted(defined_symbols([obj]) - metal):
            if name not in manifest.not_provided:
                problems.append(f"upstream {obj.stem}.c defines {name}, which the Metal build does not: implement "
                                f"it in port/macos/metal, or list it in manifest.json's not_provided with a reason")
    stale = sorted(set(manifest.not_provided) - {s for o in upstream_objects for s in defined_symbols([o])})
    for name, reason in manifest.not_provided.items():
        if not reason.strip():
            problems.append(f"not_provided lists {name} without a reason: document the exception")
    for name in stale:
        problems.append(f"not_provided lists {name}, which no replaced or dropped upstream file defines any more: "
                        "remove it")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("check", help="structure and upstream drift")
    check.add_argument("--against", metavar="REF",
                       help="compare with a git ref (upstream/main) instead of the checked-out port/linux/src")
    accept_parser = commands.add_parser("accept", help="record an upstream file as reconciled")
    accept_parser.add_argument("file", help="the upstream file's name (port_config.c)")
    accept_parser.add_argument("--seen", action="store_true", help="acknowledge the drift without reconciling it")
    accept_parser.add_argument("--against", metavar="REF", help="record a reviewed canonical upstream variant without changing the checkout pin")
    symbols = commands.add_parser("symbols", help="the symbol contract (from ninja)")
    symbols.add_argument("--stamp", type=Path, required=True)
    symbols.add_argument("--upstream", type=Path, nargs="+", required=True, help="the replaced/dropped upstream objects")
    symbols.add_argument("--metal-list", type=Path, required=True, help="a file listing the Metal build's objects")
    args = parser.parse_args(argv)

    if args.command == "accept":
        print(accept(args.file, args.seen, against=args.against))
        return 0
    manifest = load()
    problems = structure_problems(manifest)
    if args.command == "symbols":
        if not problems:
            # (ninja's response file quotes paths with spaces, as the linker's does)
            metal = [Path(word) for word in shlex.split(args.metal_list.read_text())]
            problems = symbol_problems(manifest, args.upstream, metal)
        if problems:
            print("Metal build contract (port/macos/metal/manifest.json):", file=sys.stderr)
            for problem in problems:
                print("  " + problem, file=sys.stderr)
            return 1
        args.stamp.write_text("ok\n")
        return 0
    if problems:
        print("manifest.json does not describe the tree:")
        for problem in problems:
            print("  " + problem)
        return 1
    preview_problems = reference_problems(manifest, args.against) if args.against else []
    if preview_problems:
        print("Upstream source classifications to review:")
        for problem in preview_problems:
            print("  " + problem)
    drifts = drift(manifest, args.against)
    new = [d for d in drifts if not d.known]
    for title, group in (("Upstream changes to review", new), ("Known drift, not yet reconciled",
                                                              [d for d in drifts if d.known])):
        if group:
            print(f"{title} ({len(group)}):")
            for item in group:
                print(item.describe(manifest, args.against))
    if not drifts and not preview_problems:
        print("Metal manifest: every Mac fork is reconciled with upstream.")
    return 1 if new or preview_problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
