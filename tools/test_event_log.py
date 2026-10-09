"""Delta Stats' event log and its compression (port/linux/src/event_log.c,
event_gzip.c), through server/tests/events_test.c built with the build
machine's C compiler (under AddressSanitizer and UndefinedBehaviorSanitizer
where it has them): the batch is the documented schema (docs/delta.md,
"Delta Stats"), its limits hold, its gzip inflates with zlib, and random
calls never make a batch that is not JSON."""
import gzip
import json
import random
import re
import shutil
import subprocess
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [
    ROOT / "server" / "tests" / "events_test.c",
    ROOT / "port" / "linux" / "src" / "event_log.c",
    ROOT / "port" / "linux" / "src" / "event_gzip.c",
    ROOT / "port" / "third_party" / "monocypher" / "monocypher.c",
]
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
HEX64 = re.compile(r"^[0-9a-f]{64}$")


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.fixture(scope="module")
def program(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    out = tmp_path_factory.mktemp("events") / "events_test"
    base = [cc, "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
            "-I", str(ROOT / "port" / "third_party" / "monocypher"), "-o", str(out), *map(str, SOURCES), "-lm"]
    if subprocess.run(base[:1] + SANITIZERS + base[1:], capture_output=True).returncode != 0:
        result = subprocess.run(base, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
    return out


def run(program, *arguments):
    result = subprocess.run([str(program), *map(str, arguments)], capture_output=True)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    return result.stdout


def test_sample_batch(program):
    batch = json.loads(run(program, "sample"))
    assert batch["schema"] == 1
    assert batch["server"] == {"name": 'Test "server"', "build": "0.7.0b", "platform": "linux-x64"}
    game = batch["game"]
    assert game["id"] == "00112233445566778899aabbccddeeff"
    assert game["part"] == 1 and game["final"] is True
    assert game["map"] == "levels\\test\\bloodgulch\\bloodgulch"
    assert game["started"] == 1791234567 and game["ended"] == 1791234567 + 40
    assert game["team_scores"] == [3, 1] and game["end_reason"] == "score"
    walter, jo, guest = batch["players"]
    assert walter["name"] == "Walter" and jo["name"] == "Jo ✓" and guest["name"] == "guest?"[:5] + "\u0001"
    # (a keyed hash of the hardware ID, the same for either case; none without one)
    assert HEX64.match(walter["ident"]) and HEX64.match(jo["ident"]) and "ident" not in guest
    assert "00112233" not in json.dumps(batch["players"]) and "ffeeddcc" not in json.dumps(batch)
    assert walter["client"] == "chupathingyce" and walter["platform"] == "linux"
    assert guest["client"] == "other" and "platform" not in guest
    assert walter["place"] == 1 and walter["score"] == 2 and walter["kills"] == 2
    # (no totals from the game: the kills seen)
    assert guest["kills"] == 0 and guest["deaths"] == 2
    pistol = next(w for w in walter["weapons"] if w["weapon"] == "weapons\\pistol")
    assert pistol == {"weapon": "weapons\\pistol", "shots": 10, "hits": 2, "kills": 2, "headshots": 1, "damage": 0.8}
    assert walter["shots"] == 10 and walter["hits"] == 2
    assert walter["grenades"] == {"frag": 0, "plasma": 1}
    assert walter["damage_dealt"] == 1.5 and jo["damage_taken"] == 1.5
    assert walter["best_spree"] == 2 and walter["medals"] == {"double_kill": 1}
    assert jo["medals"] == {"beat_down": 1}
    assert jo["vehicles"] == [{"vehicle": "vehicles\\warthog\\mp_warthog", "seat": "driver", "seconds": 10.0}]
    assert walter["pickups"] == {"weapons\\sniper rifle\\sniper rifle": 1}
    kills = batch["kills"]
    assert [(k["killer"], k["victim"], k["damage"]) for k in kills] == [
        (0, 1, "bullet"), (0, 2, "bullet"), (1, 0, "melee"), (-1, 2, "fall"), (1, 1, "grenade")]
    assert kills[0]["headshot"] is True and kills[0]["t"] == pytest.approx(3.33)
    assert kills[0]["killer_pos"] == [62.5, -120.25, 1.0] and kills[0]["victim_pos"] == [70.0, -118.0, 0.5]
    assert kills[4]["suicide"] is True and "headshot" not in kills[1]
    assert {(m["player"], m["medal"]) for m in batch["medals"]} == {(0, "double_kill"), (1, "beat_down")}
    assert batch["objectives"] == [{"t": pytest.approx(16.67), "player": 0, "team": 0, "kind": "flag_grab", "pos": [90.0, -150.0, 0.25]}]
    assert batch["rides"] == [[20.0, 30.0, 1, "vehicles\\warthog\\mp_warthog", "driver"]]
    assert batch["spawns"] == [[1.0, 0, 40.0, -100.0, 0.25]]
    assert batch["positions"] == [[1.0, 0, 41.0, -101.0, 0.25], [1.0, 1, 51.0, -111.0, 0.25]]
    assert batch["pings"] == [[30.3, 1, 85]]
    assert [p["item"] for p in batch["pickups"]] == ["weapons\\sniper rifle\\sniper rifle", "powerups\\active camouflage"]
    assert batch["moderation"][0]["kind"] == "kick" and batch["moderation"][0]["player"] == 2
    assert batch["moderation"][1] == {"t": pytest.approx(30.33), "kind": "ban", "player": -1, "name": "Nobody",
                                      "by": "admin:milenko", "reason": ""}
    sessions = {s["player"]: s for s in batch["sessions"]}
    # (left after a moderator's kick: kicked)
    assert sessions[2] == {"player": 2, "joined": 3.0, "left": pytest.approx(31.67), "reason": "kick"}
    assert sessions[0]["left"] is None
    assert batch["limits"]["dropped"] == {}


def test_killjoy_from_the_grave_playlist_and_color(program):
    batch = json.loads(run(program, "medals"))
    walter, jo, guest = batch["players"]
    assert batch["game"]["playlist"] == "team_slayer"
    assert walter["color"] == 3 and "color" not in guest
    # (Walter's fifth kill in a row: a Killing Spree; Jo ends it, dead already)
    assert walter["medals"].get("killing_spree") == 1
    assert jo["medals"].get("killjoy") == 1 and jo["medals"].get("from_the_grave") == 1
    assert batch["kills"][-1]["from_grave"] is True


def test_a_part_then_the_end(program):
    first, last = run(program, "part").split(b"\f")
    first, last = json.loads(first), json.loads(last)
    assert first["game"]["id"] == last["game"]["id"]
    assert (first["game"]["part"], first["game"]["final"]) == (1, False)
    assert (last["game"]["part"], last["game"]["final"]) == (2, True)
    # (the end has everything the part had, and what came after)
    assert len(last["kills"]) == len(first["kills"]) + 1
    assert last["players"][0]["kills"] >= first["players"][0]["kills"]


def test_an_abandoned_game_is_freed_as_allocated(program):
    run(program, "abandon")
    # (the game's units free through debug_free, cseries.h's free, which
    # takes neither the C library's memory nor NULL: a batch freed there
    # crashed hosts whose game stopped before its end)
    source = (ROOT / "port" / "linux" / "game" / "game_events.c").read_text()
    assert not re.search(r"(?<![\w.>])free\s*\(", source)
    assert "event_log_free(json)" in source


@pytest.mark.parametrize("capacity", [64, 2000, 40000])
def test_limits(program, capacity):
    output = run(program, "limits", capacity).decode()
    text, _, counts = output.rpartition("}\n")
    batch = json.loads(text + "}")
    kept, dropped = map(int, re.findall(r"\d+", counts))
    assert kept <= capacity
    # (the kills are kept above the positions and pickups)
    # (and the first second's samples, which are never thinned)
    assert min(1200, capacity - 16) <= len(batch["kills"]) <= min(1200, capacity)
    if capacity < 1200 + 16 * 3600:
        assert dropped > 0 and batch["limits"]["sample_seconds"] > 1
    # (the totals count what was dropped too)
    assert sum(p["kills"] for p in batch["players"]) == 1200
    assert len(json.dumps(batch)) < 8 * 1024 * 1024


def test_gzip_round_trip(program, tmp_path):
    batch = run(program, "limits", 40000)
    cases = [b"", b"a", b"abc" * 1000, bytes(range(256)) * 300, random.Random(5).randbytes(70000), batch]
    for index, data in enumerate(cases):
        source, packed = tmp_path / f"in{index}", tmp_path / f"out{index}.gz"
        source.write_bytes(data)
        run(program, "gzip", source, packed)
        assert gzip.decompress(packed.read_bytes()) == data
        stream = zlib.decompressobj(16 + zlib.MAX_WBITS)
        assert stream.decompress(packed.read_bytes()) == data and stream.eof
    # (a batch's numbers compress well with the fixed codes)
    assert packed.stat().st_size * 4 < len(batch)


def test_fuzz(program):
    output = run(program, "fuzz", 7, 60)
    for chunk in output.split(b"\f"):
        if chunk.strip():
            batch = json.loads(chunk.decode("utf-8"))
            assert batch["schema"] == 1
            count = len(batch["players"])
            for kill in batch["kills"]:
                assert -1 <= kill["killer"] < count and 0 <= kill["victim"] < count
            for row in batch["positions"]:
                assert all(abs(v) <= 5000 for v in row[2:])
