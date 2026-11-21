"""The [tuning] knobs' table in docs/settings.md is the code's (tools/tuning_table.py): every knob in
port_config.c's config_tuning_knobs has a row, with what it costs, what each preset sets, and what it does on
the original Xbox."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tuning_table  # noqa: E402


def test_settings_doc_is_the_code():
    result = subprocess.run([sys.executable, str(ROOT / "tools" / "tuning_table.py"), "--check",
                             str(ROOT / "docs" / "settings.md")], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr


def test_every_knob_has_a_row():
    source = tuning_table.read(tuning_table.PORT_CONFIG)
    table = source[source.index("} config_tuning_knobs[] ="):]
    names = re.findall(r'^\t\{ "([a-z_.]+)", "[a-z_]+", ', table[:table.index("};")], re.M)
    assert names, "no knobs read"
    made = tuning_table.table()
    for name in names:
        assert f"| `{name}` |" in made, f"{name}: no row"
    assert set(names) == {row["name"] for row in tuning_table.rows()}


def test_a_knob_without_a_cost_fails(monkeypatch):
    knob = next(iter(tuning_table.COSTS))
    costs = dict(tuning_table.COSTS)
    del costs[knob]
    monkeypatch.setattr(tuning_table, "COSTS", costs)
    try:
        tuning_table.rows()
    except KeyError as error:
        assert knob in str(error)
    else:
        raise AssertionError("a knob without a cost made a table")


def test_xbox_presets():
    """(the tester's 11-19 log: potato was retail on the original Xbox, and lan-party only the present mode)
    Each preset sets real knobs on the original Xbox now, every one of them the console's own setting,
    and none of them above the console's own limits."""
    rows = {row["name"]: row for row in tuning_table.rows()}
    potato = {name for name, row in rows.items() if row["on_xbox"] and row["xbox_presets"][2] not in (None, "-")}
    lan_party = {name for name, row in rows.items() if row["on_xbox"] and row["xbox_presets"][1] not in (None, "-")}
    assert {"display.dynamic_light_limit", "display.specular_light_limit", "display.particle_limit",
            "tuning.effects", "tuning.shadow_pixels", "tuning.particle_thinning"} <= potato, potato
    assert {"tuning.effects", "tuning.shadow_pixels", "tuning.object_cull_pixels", "tuning.present_mode"} <= lan_party, \
        lan_party
    # (lan-party is for network games, which hold the light limits at the console's own: a light cut there would
    # change nothing, so it has none)
    assert not {"display.dynamic_light_limit", "display.specular_light_limit"} & lan_party, lan_party
    # (a preset value on a knob that is not the console's setting would change nothing there)
    for row in tuning_table.knobs(tuning_table.read(tuning_table.PORT_CONFIG)):
        if any(row["xbox_presets"]):
            assert rows[row["name"]]["on_xbox"], f"{row['name']}: an Xbox preset value, not an Xbox setting"
    assert rows["display.dynamic_light_limit"]["xbox_default"] == "8"
    assert rows["display.specular_light_limit"]["xbox_default"] == "2"
    assert rows["display.particle_limit"]["xbox_default"] == "1024"
    for name in ("display.dynamic_light_limit", "display.specular_light_limit", "display.particle_limit"):
        for value in rows[name]["xbox_presets"]:
            assert value is None or 0 < int(value) < int(rows[name]["xbox_default"]), (name, value)


def test_xbox_table_has_only_its_settings():
    """A knob that isn't the original Xbox's setting changes nothing there: its table leaves it out."""
    made = tuning_table.table(xbox=True)
    for row in tuning_table.rows():
        assert (f"| `{row['name']}` |" in made) == row["on_xbox"], row["name"]
    assert "not a console setting" not in made
