#!/usr/bin/env python3
"""Makes, signs and checks Delta's legacy table (docs/delta.md, "The legacy
table as config"; the game's side is port/linux/src/delta.c).

The table is a JSON document, legacy.json:

    {"delta_legacy": 1, "serial": 42, "issued": 1791331200,
     "wires": {"chupa-24a": {"announce": 24, "minimum": 11, "maximum": 24,
                             "follows": "build-154"}},
     "disabled_capabilities": [],
     "platform_policy": {"xbox": {"host_players": 16}}}

("platform_policy" is optional and reserved: per-platform limits Delta Peer
is to read, each bounded by the build's own hard limits). A row of a wire
from chupa-38a on may carry "layouts" (docs/delta.md, "Layouts"): facts
about OpenCE versions, by version, that its builds read by data:

    "chupa-38a": {"announce": 32, "minimum": 11, "maximum": 39,
                  "layouts": {"39": {"options_size": 48, "dropped_messages": [92]}}}

each fact's fields optional ("settings_size", "options_size",
"shield_effects_maximum", "dropped_messages"), a fact holding until the next,
and used by a build only past its own DELTA_LEGACY_NEWEST and within
network_rules.h's limits (this tool refuses a fact past them). Its signature,
legacy.json.sig: the Ed25519 signature of legacy.json's exact bytes, as 128
hex digits and a line feed. A build reads only its own wire's row
(DELTA_WIRE in port/linux/include/delta.h), and takes it only if it widens
the numbers the build was made with. At most 16384 bytes.

    delta_table.py make --serial N [--from OLD.json] [--row WIRE=A,MIN,MAX[@build-N] ...] [--out legacy.json]
        this build's wire and numbers (delta.h, halo_port_limits.h) as a
        table; --from keeps another table's rows for the other wires;
        --row sets a wire's row (announce, minimum, maximum) as the
        cross-play gate proved it (tools/crossplay_test.py), with the
        OpenCE build it was proved against ("follows"), and may only
        widen that wire's row in --from and, for this build's wire, its
        built-in numbers; --take-back WIRE=A,MIN,MAX sets a wire's row back
        to its release's own numbers; a row keeps its layouts. This build's
        wire's row has this build's own facts (its versions past the one it
        announces); --layout WIRE=VERSION:FIELD=VALUE[,FIELD=VALUE...] adds
        a fact past the wire's own versions (dropped_messages: kinds joined
        by +), for a wire from chupa-38a on
    delta_table.py sign --key KEY.pem legacy.json      writes legacy.json.sig
    delta_table.py verify [--public-key HEX [--last-epoch N]] legacy.json
        checks legacy.json.sig against the key (or delta_key.h's keys), and
        that the key may sign the serial's epoch
    delta_table.py next-serial [--from legacy.json] [--new-epoch]
        the next table's serial: one more, or the next epoch's first (a
        serial's top byte is its epoch: only the recovery key opens one)
    delta_table.py keygen --out KEY.pem
        a new key pair: the private key into KEY.pem (readable by you alone:
        keep it out of every repository; CI has it as a secret), the public
        half printed, as hex and as delta_key.h's C array

It uses the openssl command (OpenSSL 3, or the one $OPENSSL names) for
Ed25519, so nothing beyond Python's own library is needed.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DELTA = ROOT / "port" / "linux" / "include" / "delta.h"
RULES = ROOT / "port" / "linux" / "game" / "network_rules.h"
LIMITS = ROOT / "port" / "linux" / "include" / "halo_port_limits.h"
KEY_HEADER = ROOT / "port" / "linux" / "src" / "delta_key.h"

FORMAT = 1
DOCUMENT_SIZE = 16384
MAXIMUM_VERSION = 65535
# platform_policy (reserved for Delta Peer's per-platform limits): platform
# and limit names, and each limit a small whole number
NAME = re.compile(r"[a-z0-9_]{1,32}")
# a row's "follows": the OpenCE build the cross-play gate proved it with
FOLLOWS = re.compile(r"[A-Za-z0-9._-]{1,31}")
POLICY_MAXIMUM = 4096
# the first wire whose builds read a row's layouts
LAYOUT_WIRE = (38, "a")
WIRE_ID = re.compile(r"chupa-(\d{1,3})([a-z])")
LAYOUT_FIELDS = ("settings_size", "options_size", "shield_effects_maximum")
# (how deep the game's parser nests a document: delta.c's JSON_DEPTH, the
# document itself 1, another wire's row 3)
JSON_DEPTH = 8
# the game settings record and the gametype's options (network_rules.h): to
# 34, from 35; the record's bytes before the options
SETTINGS_SIZE, SETTINGS_EXTENDED_SIZE = 13120, 13136
OPTIONS_SIZE, OPTIONS_EXTENDED_SIZE = 0x1C, 0x2C
EXTENDED_OPTIONS_VERSION, SHIELD_EFFECTS_VERSION, SHIELD_EFFECTS_KIND = 35, 36, 91
# (an Ed25519 public key's DER: this prefix, then its 32 bytes)
ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")


class TableError(ValueError):
    pass


# ---------- the build's own numbers


def wire() -> str:
    match = re.search(r'^#define DELTA_WIRE "([^"]+)"', DELTA.read_text(encoding="utf-8"), re.M)
    if not match:
        raise SystemExit(f"no DELTA_WIRE in {DELTA}")
    return match.group(1)


def built_in() -> dict:
    """the numbers this build was made with (halo_port_limits.h)"""
    text = LIMITS.read_text(encoding="utf-8")

    def define(name):
        match = re.search(rf"^#define {name} (\d+)", text, re.M)
        if not match:
            raise SystemExit(f"{name} is not a number in {LIMITS}")
        return int(match.group(1))

    return {"announce": define("HALO_PORT_NETWORK_VERSION"),
            "minimum": define("HALO_PORT_NETWORK_VERSION_MINIMUM"),
            "maximum": define("HALO_PORT_NETWORK_VERSION_MAXIMUM")}


def header_number(path: Path, name: str) -> int:
    match = re.search(rf"^#define {name} (\d+)", path.read_text(encoding="utf-8"), re.M)
    if not match:
        raise SystemExit(f"{name} is not a number in {path}")
    return int(match.group(1))


def newest() -> int:
    """the newest OpenCE version whose rules this build has (DELTA_LEGACY_NEWEST)"""
    return header_number(DELTA, "DELTA_LEGACY_NEWEST")


def wire_newest() -> dict:
    """each wire's newest version every build of it plays by its own rules
    (DELTA_WIRE_LEGACY_NEWEST)"""
    text = DELTA.read_text(encoding="utf-8")
    block = text[text.index("#define DELTA_WIRE_LEGACY_NEWEST(X)"):]
    block = block[:block.index("\n\n")]
    return {name: int(number) for name, number in re.findall(r'X\("([^"]+)", (\d+)\)', block)}


def limits() -> dict:
    """network_rules.h's limits on a table's facts, and delta.h's count"""
    growth = header_number(RULES, "NETWORK_RULES_LAYOUT_GROWTH_MAXIMUM")
    return {"settings": SETTINGS_EXTENDED_SIZE + growth, "shield": header_number(RULES, "NETWORK_RULES_SHIELD_EFFECTS_LIMIT"),
            "facts": header_number(DELTA, "DELTA_LAYOUT_FACTS"),
            "shield_built_in": header_number(RULES, "NETWORK_RULES_SHIELD_EFFECTS_MAXIMUM")}


def reads_layouts(name: str) -> bool:
    """whether a wire's builds read layouts (chupa-38a on)"""
    match = WIRE_ID.fullmatch(name)
    return bool(match) and (int(match.group(1)), match.group(2)) >= LAYOUT_WIRE


# ---------- layouts (as network_rules.c reads them)


def built_in_layout(version: int, own_newest: int = None) -> dict:
    """a version's built-in layout (network_rules_layout_built_in)"""
    rules = min(version, newest() if own_newest is None else own_newest)
    return {"settings_size": SETTINGS_EXTENDED_SIZE if rules >= EXTENDED_OPTIONS_VERSION else SETTINGS_SIZE,
            "options_size": OPTIONS_EXTENDED_SIZE if rules >= EXTENDED_OPTIONS_VERSION else OPTIONS_SIZE,
            "shield_effects_maximum": limits()["shield_built_in"],
            "dropped_messages": [SHIELD_EFFECTS_KIND] if rules >= SHIELD_EFFECTS_VERSION else []}


def apply_layouts(version: int, facts: dict, own_newest: int) -> tuple:
    """a version's layout with a row's facts (network_rules_layout_apply):
    the layout, and the fields past the limits, not taken"""
    layout = built_in_layout(version, own_newest)
    bounds = limits()
    refused = []
    for fact_version in sorted(int(number) for number in facts):
        if fact_version <= own_newest or fact_version > version:
            continue
        fact = facts[str(fact_version)]
        if fact.get("options_size"):
            settings = layout["settings_size"] + fact["options_size"] - layout["options_size"]
            if fact["options_size"] >= layout["options_size"] and settings <= bounds["settings"]:
                layout["options_size"], layout["settings_size"] = fact["options_size"], settings
            else:
                refused.append(f"{fact_version}: options_size {fact['options_size']}")
        if fact.get("settings_size"):
            if layout["settings_size"] <= fact["settings_size"] <= bounds["settings"]:
                layout["settings_size"] = fact["settings_size"]
            else:
                refused.append(f"{fact_version}: settings_size {fact['settings_size']}")
        if fact.get("shield_effects_maximum"):
            if layout["shield_effects_maximum"] <= fact["shield_effects_maximum"] <= bounds["shield"]:
                layout["shield_effects_maximum"] = fact["shield_effects_maximum"]
            else:
                refused.append(f"{fact_version}: shield_effects_maximum {fact['shield_effects_maximum']}")
        layout["dropped_messages"] = sorted(set(layout["dropped_messages"]) | set(fact.get("dropped_messages", [])))
    return layout, refused


def built_in_facts() -> dict:
    """this build's own facts, for its wire's row: each version past the one
    it announces to the newest whose rules it has (informational: its builds
    go by their own rules for them)"""
    facts = {}
    for version in range(built_in()["announce"] + 1, newest() + 1):
        layout = built_in_layout(version)
        fact = {"settings_size": layout["settings_size"], "options_size": layout["options_size"]}
        if version >= SHIELD_EFFECTS_VERSION:
            fact["shield_effects_maximum"] = layout["shield_effects_maximum"]
            fact["dropped_messages"] = layout["dropped_messages"]
        facts[str(version)] = fact
    return facts


def parse_layout(text: str) -> tuple:
    """--layout's WIRE=VERSION:FIELD=VALUE[,FIELD=VALUE...]"""
    match = re.fullmatch(r"([a-z0-9-]{1,31})=(\d{1,5}):([a-z_]+=[0-9+]+(?:,[a-z_]+=[0-9+]+)*)", text)
    if not match:
        raise TableError(f"--layout {text}: not WIRE=VERSION:FIELD=VALUE[,FIELD=VALUE...]")
    version = int(match.group(2))
    if not 1 <= version <= MAXIMUM_VERSION - 1:
        raise TableError(f"--layout {text}: not a version")
    fact = {}
    for pair in match.group(3).split(","):
        field, value = pair.split("=")
        if field in fact:
            raise TableError(f"--layout {text}: {field} twice")
        if field == "dropped_messages":
            kinds = [int(kind) for kind in value.split("+") if kind]
            if not kinds or not all(1 <= kind <= 255 for kind in kinds):
                raise TableError(f"--layout {text}: dropped_messages are kinds 1 to 255")
            fact[field] = sorted(set(kinds))
        elif field in LAYOUT_FIELDS and "+" not in value:
            fact[field] = int(value)
        else:
            raise TableError(f"--layout {text}: no field {field}={value}")
    return match.group(1), str(version), fact


# ---------- the document


def _no_duplicates(pairs):
    keys = [key for key, _ in pairs]
    if len(keys) != len(set(keys)):
        raise TableError("a key appears twice")
    return dict(pairs)


def _integer(value, minimum, maximum, name):
    if type(value) is not int or not minimum <= value <= maximum:
        raise TableError(f"{name} is not a whole number from {minimum} to {maximum}")
    return value


def parse(document: bytes, own_wire: str = None) -> dict:
    """the table, checked as the game checks it (delta.c), or TableError"""
    if len(document) > DOCUMENT_SIZE:
        raise TableError(f"it is {len(document)} bytes, more than {DOCUMENT_SIZE}")
    try:
        table = json.loads(document.decode("utf-8"), object_pairs_hook=_no_duplicates,
                           parse_constant=lambda name: (_ for _ in ()).throw(TableError(name)))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise TableError(f"it is not JSON: {error}") from None
    if not isinstance(table, dict):
        raise TableError("it is not an object")
    for key in ("delta_legacy", "serial", "wires"):
        if key not in table:
            raise TableError(f"it has no {key}")
    if _integer(table["delta_legacy"], 0, 1000000, "delta_legacy") != FORMAT:
        raise TableError(f"its format is {table['delta_legacy']}, not {FORMAT}")
    _integer(table["serial"], 1, 2 ** 32 - 2, "serial")
    if "issued" in table:
        _integer(table["issued"], 0, 2 ** 53, "issued")
    if not isinstance(table["wires"], dict):
        raise TableError("wires is not an object")
    for name, row in table["wires"].items():
        if not isinstance(row, dict) or not all(key in row for key in ("announce", "minimum", "maximum")):
            raise TableError(f"wire {name}'s row has no announce, minimum and maximum")
        for key in ("announce", "minimum", "maximum"):
            _integer(row[key], 1, MAXIMUM_VERSION, f"wire {name}'s {key}")
        if not row["minimum"] <= row["announce"] <= row["maximum"]:
            raise TableError(f"wire {name}'s row is not a range")
        if "follows" in row and not (isinstance(row["follows"], str) and FOLLOWS.fullmatch(row["follows"])):
            raise TableError(f"wire {name}'s follows is not an OpenCE build's tag")
        if "layouts" in row:
            check_layouts(name, row["layouts"])
    policy = table.get("platform_policy", {})
    if not isinstance(policy, dict):
        raise TableError("platform_policy is not an object")
    for platform, limits in policy.items():
        if not NAME.fullmatch(platform) or not isinstance(limits, dict):
            raise TableError(f"platform_policy's {platform!r} is not a platform's limits")
        for name, value in limits.items():
            if not NAME.fullmatch(name):
                raise TableError(f"platform_policy's {platform} has a limit named {name!r}")
            _integer(value, 0, POLICY_MAXIMUM, f"platform_policy's {platform}.{name}")
    if depth(table) > JSON_DEPTH:
        raise TableError(f"it nests deeper than {JSON_DEPTH} (the game's parser drops it)")
    disabled = table.get("disabled_capabilities", [])
    if not isinstance(disabled, list) or len(disabled) > 64 or not all(isinstance(name, str) for name in disabled):
        raise TableError("disabled_capabilities is not a list of names")
    if own_wire is not None:
        check_widens(table, own_wire, built_in())
    return table


def depth(value, level=1) -> int:
    """how deep a value nests, the document 1 (as delta.c counts; another
    wire's row is 3)"""
    if isinstance(value, dict):
        return max([level] + [depth(item, level + 1) for item in value.values()])
    if isinstance(value, list):
        return max([level] + [depth(item, level + 1) for item in value])
    return level - 1


def check_layouts(name: str, layouts) -> None:
    """a row's layouts, as the game reads them (delta.c's json_layouts)"""
    if not isinstance(layouts, dict):
        raise TableError(f"wire {name}'s layouts is not an object")
    if len(layouts) > limits()["facts"]:
        raise TableError(f"wire {name} has more than {limits()['facts']} layout facts")
    for version, fact in layouts.items():
        if not re.fullmatch(r"[1-9]\d{0,4}", version) or int(version) > MAXIMUM_VERSION:
            raise TableError(f"wire {name}'s layout {version!r} is not a version")
        if not isinstance(fact, dict):
            raise TableError(f"wire {name}'s layout {version} is not an object")
        for field in LAYOUT_FIELDS:
            if field in fact:
                _integer(fact[field], 0, MAXIMUM_VERSION, f"wire {name}'s layout {version}'s {field}")
        kinds = fact.get("dropped_messages", [])
        if not isinstance(kinds, list) or len(kinds) > 255:
            raise TableError(f"wire {name}'s layout {version}'s dropped_messages is not a list of kinds")
        for kind in kinds:
            _integer(kind, 1, 255, f"wire {name}'s layout {version}'s dropped message")


def check_widens(table: dict, own_wire: str, floor: dict) -> None:
    """a row may only widen the built-in numbers (as the game takes it)"""
    row = table["wires"].get(own_wire)
    if row is None:
        return
    if row["announce"] < floor["announce"] or row["minimum"] > floor["minimum"] or \
            row["maximum"] < floor["maximum"]:
        raise TableError(f"wire {own_wire}'s row {row} narrows the built-in {floor}")


def parse_row(text: str) -> tuple:
    """--row's WIRE=ANNOUNCE,MINIMUM,MAXIMUM[@OPENCE_BUILD]"""
    match = re.fullmatch(r"([a-z0-9-]{1,31})=(\d{1,5}),(\d{1,5}),(\d{1,5})(?:@([A-Za-z0-9._-]{1,31}))?", text)
    if not match:
        raise TableError(f"--row {text}: not WIRE=ANNOUNCE,MINIMUM,MAXIMUM[@OPENCE_BUILD]")
    announce, minimum, maximum = (int(match.group(index)) for index in (2, 3, 4))
    if not (1 <= minimum <= announce <= maximum <= MAXIMUM_VERSION):
        raise TableError(f"--row {text}: needs 1 <= minimum <= announce <= maximum <= {MAXIMUM_VERSION}")
    row = {"announce": announce, "minimum": minimum, "maximum": maximum}
    if match.group(5):
        row["follows"] = match.group(5)
    return match.group(1), row


def widens(row: dict, floor: dict) -> bool:
    return row["announce"] >= floor["announce"] and row["minimum"] <= floor["minimum"] and \
        row["maximum"] >= floor["maximum"]


def numbers(row: dict) -> dict:
    return {key: row[key] for key in ("announce", "minimum", "maximum", "follows") if key in row}


def make(serial: int, issued: int = None, previous: dict = None, rows: dict = None, take_back: dict = None,
         layouts: list = None) -> bytes:
    wires = {name: dict(row) for name, row in previous["wires"].items()} if previous else {}
    old = wires.get(wire())
    wires[wire()] = built_in()
    if old and widens(old, wires[wire()]):
        wires[wire()] = numbers(old)
    # (this build's wire: its own facts, and the facts past them a table has)
    if reads_layouts(wire()):
        facts = {version: fact for version, fact in ((old or {}).get("layouts") or {}).items()
                 if int(version) > newest()}
        wires[wire()]["layouts"] = dict(built_in_facts(), **facts)
    elif old and "layouts" in old:
        wires[wire()]["layouts"] = old["layouts"]
    for name, row in (rows or {}).items():
        floor = wires.get(name)
        if floor and not widens(row, floor):
            raise TableError(f"--row {name}: {row} narrows {floor} (a table only widens)")
        if floor and "layouts" in floor:
            row = dict(row, layouts=floor["layouts"])
        wires[name] = row
    # (a follow taken back: the row set to the wire's shipped numbers again,
    # which builds of that wire take, being no less than what they shipped
    # with; this build's own wire never below its built-in numbers)
    for name, row in (take_back or {}).items():
        if name == wire() and not widens(row, built_in()):
            raise TableError(f"--take-back {name}: {row} is below this build's own numbers {built_in()}")
        if name in wires and "layouts" in wires[name]:
            row = dict(row, layouts=wires[name]["layouts"])
        wires[name] = row
    # (a fact past a wire's own versions: what its builds read by data)
    for name, version, fact in layouts or []:
        if not reads_layouts(name):
            raise TableError(f"--layout {name}: its builds read no layouts (wires from chupa-{LAYOUT_WIRE[0]}"
                             f"{LAYOUT_WIRE[1]} on do)")
        if name not in wires:
            raise TableError(f"--layout {name}: the table has no row for it")
        own = wire_newest().get(name)
        if own is None:
            raise TableError(f"--layout {name}: not a wire of DELTA_WIRE_LEGACY_NEWEST")
        if int(version) <= own:
            raise TableError(f"--layout {name}={version}: {name}'s builds have {version}'s rules (to {own})")
        facts = dict(wires[name].get("layouts") or {})
        facts[version] = dict(facts.get(version, {}), **fact)
        _, refused = apply_layouts(int(version), facts, own)
        if refused:
            raise TableError(f"--layout {name}={version}: past what its builds take: {', '.join(refused)}")
        wires[name]["layouts"] = dict(sorted(facts.items(), key=lambda item: int(item[0])))
    table = {
        "delta_legacy": FORMAT,
        "serial": serial,
        "issued": int(time.time()) if issued is None else issued,
        "wires": dict(sorted(wires.items())),
        "disabled_capabilities": list(previous.get("disabled_capabilities", [])) if previous else [],
    }
    if previous and "platform_policy" in previous:
        table["platform_policy"] = previous["platform_policy"]
    document = (json.dumps(table, indent=2) + "\n").encode()
    parse(document, wire())
    return document


# ---------- Ed25519, with openssl


def openssl() -> str:
    """an openssl command that does Ed25519 (OpenSSL 3; macOS's LibreSSL does not)"""
    candidates = [os.environ["OPENSSL"]] if os.environ.get("OPENSSL") else \
        ["openssl", "/opt/homebrew/opt/openssl@3/bin/openssl", "/usr/local/opt/openssl@3/bin/openssl"]
    for candidate in candidates:
        path = shutil.which(candidate)
        if not path:
            continue
        version = subprocess.run([path, "version"], capture_output=True, text=True).stdout
        if version.startswith("OpenSSL 3"):
            return path
    raise SystemExit("needs OpenSSL 3's openssl command (or $OPENSSL naming one)")


def run(arguments, **kwargs) -> bytes:
    return subprocess.run([openssl(), *arguments], check=True, capture_output=True, **kwargs).stdout


def public_key(key: Path) -> bytes:
    der = run(["pkey", "-in", str(key), "-pubout", "-outform", "DER"])
    if not der.startswith(ED25519_SPKI_PREFIX) or len(der) != len(ED25519_SPKI_PREFIX) + 32:
        raise SystemExit(f"{key} is not an Ed25519 key")
    return der[len(ED25519_SPKI_PREFIX):]


def keygen(out: Path) -> bytes:
    if out.exists():
        raise SystemExit(f"{out} exists: not replaced")
    descriptor = os.open(str(out), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    os.close(descriptor)
    run(["genpkey", "-algorithm", "ed25519", "-out", str(out)])
    os.chmod(out, 0o600)
    return public_key(out)


def c_array(key: bytes) -> str:
    return "\n".join("\t\t" + " ".join(f"0x{byte:02x}," for byte in half) for half in (key[:16], key[16:]))


def sign_bytes(key: Path, document: bytes) -> bytes:
    with tempfile.TemporaryDirectory() as directory:
        text = Path(directory) / "document"
        signature = Path(directory) / "signature"
        text.write_bytes(document)
        run(["pkeyutl", "-sign", "-inkey", str(key), "-rawin", "-in", str(text), "-out", str(signature)])
        raw = signature.read_bytes()
    if len(raw) != 64:
        raise SystemExit(f"openssl gave a {len(raw)}-byte signature")
    return raw


def verify_bytes(key: bytes, document: bytes, signature: bytes) -> bool:
    with tempfile.TemporaryDirectory() as directory:
        text = Path(directory) / "document"
        signature_file = Path(directory) / "signature"
        public = Path(directory) / "public.der"
        text.write_bytes(document)
        signature_file.write_bytes(signature)
        public.write_bytes(ED25519_SPKI_PREFIX + key)
        result = subprocess.run([openssl(), "pkeyutl", "-verify", "-pubin", "-keyform", "DER", "-inkey",
                                 str(public), "-rawin", "-in", str(text), "-sigfile", str(signature_file)],
                                capture_output=True)
    return result.returncode == 0


def signature_from_text(text: str) -> bytes:
    text = text.rstrip(" \t\r\n")
    if not re.fullmatch(r"[0-9a-fA-F]{128}", text):
        raise TableError("the signature is not 128 hex digits")
    return bytes.fromhex(text)


def header_keys():
    """delta_key.h's keys (the block before the test key's #else), but zeros"""
    text = KEY_HEADER.read_text().split("#else", 1)[0]
    keys = []
    for block in re.findall(r"\{([^{}]*)\}", text):
        values = re.findall(r"0x([0-9a-fA-F]{2})", block)
        if len(values) == 32:
            key = bytes(int(value, 16) for value in values)
            if any(key):
                keys.append(key)
    return keys


def epoch(serial: int) -> int:
    """a serial's epoch: its top byte (delta_key.h)"""
    return serial >> 24


def next_serial(previous: int, new_epoch: bool = False) -> int:
    """the serial after previous: one more, or with new_epoch the first of
    the next epoch, which only the recovery key may sign"""
    if not new_epoch:
        if epoch(previous + 1) != epoch(previous):
            raise TableError(f"serial {previous} is its epoch's last: a new epoch needs the recovery key")
        return previous + 1
    if epoch(previous) >= 255:
        raise TableError("no epoch after 255")
    return ((epoch(previous) + 1) << 24) | 1


def header_last_epochs():
    """delta_key.h's last epoch of each key, in its keys' order"""
    text = KEY_HEADER.read_text().split("#else", 1)[0]
    match = re.search(r"delta_key_last_epochs\[\]\s*=\s*\{([^}]*)\}", text)
    return [int(value) for value in re.findall(r"\d+", match.group(1))] if match else []


def check(document: bytes, signature_text: str, keys, last_epochs=None) -> dict:
    """the signed table, checked as the game does: size, signature, the
    signing key's last epoch (each key's in last_epochs, by default every
    one), then the document (but not against a build's numbers); TableError
    if not"""
    if len(document) > DOCUMENT_SIZE:
        raise TableError(f"it is {len(document)} bytes, more than {DOCUMENT_SIZE}")
    signature = signature_from_text(signature_text)
    signer = next((index for index, key in enumerate(keys) if verify_bytes(key, document, signature)), None)
    if signer is None:
        raise TableError("its signature does not match")
    table = parse(document)
    last = (last_epochs or [255] * len(keys))[signer]
    if epoch(table["serial"]) > last:
        raise TableError(f"its epoch ({epoch(table['serial'])}) is past its key's last ({last})")
    return table


def signed_table(document: bytes, signature: bytes) -> bytes:
    """the table as the game caches it and passes it on: the signature's hex
    digits, a line feed, the document"""
    return signature.hex().encode() + b"\n" + document


# ---------- the commands


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("make")
    command.add_argument("--serial", type=int, required=True)
    command.add_argument("--issued", type=int)
    command.add_argument("--from", dest="previous", type=Path, help="a table whose other wires' rows to keep")
    command.add_argument("--row", action="append", default=[],
                         help="WIRE=ANNOUNCE,MINIMUM,MAXIMUM[@OPENCE_BUILD], a wire's row (and the OpenCE build it follows)")
    command.add_argument("--take-back", action="append", default=[], metavar="WIRE=A,MIN,MAX",
                         help="a wire's row set back to its release's own numbers (a follow taken back); exactly "
                              "the shipped numbers: a row below them makes every build of the wire drop the table")
    command.add_argument("--layout", action="append", default=[], metavar="WIRE=VERSION:FIELD=VALUE,...",
                         help="a layout fact for a wire's row (chupa-38a on): settings_size, options_size, "
                              "shield_effects_maximum, dropped_messages (kinds joined by +), past the wire's own "
                              "versions and within what its builds take")
    command.add_argument("--out", type=Path)
    command = commands.add_parser("sign")
    command.add_argument("--key", type=Path, required=True)
    command.add_argument("document", type=Path)
    command = commands.add_parser("verify")
    command.add_argument("--public-key", help="a public key's 64 hex digits, in place of delta_key.h's")
    command.add_argument("--last-epoch", type=int, default=255,
                         help="--public-key's last epoch (delta_key.h: the primary's 0, the recovery key's 255)")
    command.add_argument("document", type=Path)
    command = commands.add_parser("next-serial")
    command.add_argument("--from", dest="previous", type=Path, help="the table published now (none: the first)")
    command.add_argument("--new-epoch", action="store_true", help="the next epoch's first (the recovery key's)")
    command = commands.add_parser("keygen")
    command.add_argument("--out", type=Path, required=True)
    arguments = parser.parse_args()

    try:
        if arguments.command == "make":
            previous = parse(arguments.previous.read_bytes()) if arguments.previous else None
            if previous and arguments.serial <= previous["serial"]:
                raise TableError(f"serial {arguments.serial} is not newer than {previous['serial']}")
            rows = dict(parse_row(row) for row in arguments.row)
            take_back = dict(parse_row(row) for row in arguments.take_back)
            layouts = [parse_layout(layout) for layout in arguments.layout]
            document = make(arguments.serial, arguments.issued, previous, rows, take_back, layouts)
            if arguments.out:
                arguments.out.write_bytes(document)
            else:
                sys.stdout.write(document.decode())
        elif arguments.command == "sign":
            document = arguments.document.read_bytes()
            parse(document, wire())
            signature = sign_bytes(arguments.key, document)
            if not verify_bytes(public_key(arguments.key), document, signature):
                raise SystemExit("openssl's signature does not verify")
            Path(f"{arguments.document}.sig").write_text(signature.hex() + "\n")
            print(f"{arguments.document}.sig")
        elif arguments.command == "verify":
            keys = [bytes.fromhex(arguments.public_key)] if arguments.public_key else header_keys()
            epochs = [arguments.last_epoch] if arguments.public_key else header_last_epochs()
            if not keys or any(len(key) != 32 for key in keys):
                print("no key in delta_key.h: give --public-key", file=sys.stderr)
                return 1
            table = check(arguments.document.read_bytes(), Path(f"{arguments.document}.sig").read_text(), keys,
                          epochs)
            row = table["wires"].get(wire())
            print(f"{arguments.document}: signed, serial {table['serial']} (epoch {epoch(table['serial'])}); "
                  f"{wire()}: {row or 'no row'}")
        elif arguments.command == "next-serial":
            previous = parse(arguments.previous.read_bytes())["serial"] if arguments.previous else 0
            print(next_serial(previous, arguments.new_epoch))
        else:
            key = keygen(arguments.out)
            print(f"private key: {arguments.out} (keep it out of every repository: a CI secret)")
            print(f"public key: {key.hex()}")
            print("for port/linux/src/delta_key.h:")
            print(c_array(key))
    except TableError as error:
        print(f"not a legacy table: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
