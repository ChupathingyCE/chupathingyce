"""MCC's skulls on MCC maps (port/linux/game/mcc_skulls.c, docs/mcc_maps.md
"Skulls").

MCC Halo CE keeps every skull as a boolean engine global a map's scripts
read and set, debug_ice_cream_flavor_status_<skull> (its HEK's hs_doc: 29
of them, plus debug_ice_cream_flavor_force_coop). Firefight CE sets eight
of them between rounds. Checks, without a build:

- the port names exactly MCC's globals, in hs_doc's order;
- they are appended after every existing entry of the engine's external
  global table (so no index an Xbox or CE map was compiled with moves), one
  "may a map set it" row each, and the table's declared size counts them;
- the campaign snapshot carries them (version 7) and still takes the older
  sizes;
- every documented effect has its hook in the shared code, each asking
  mcc_skulls.c (the hooks are inert off MCC maps: mcc_skull_active);
- the game.mcc_skulls setting exists;
- the old design (skulls held at false, an enable refused) is gone.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKULLS_C = ROOT / "port" / "linux" / "game" / "mcc_skulls.c"
SKULLS_H = ROOT / "port" / "linux" / "game" / "mcc_skulls.h"
EXTERNALS = ROOT / "source" / "hs" / "hs_globals_external.c"
CAMPAIGN_C = ROOT / "port" / "linux" / "game" / "mcc_campaign.c"
CAMPAIGN_H = ROOT / "port" / "linux" / "game" / "mcc_campaign.h"
SCRIPTS_C = ROOT / "port" / "linux" / "game" / "mcc_scripts.c"
CONFIG_C = ROOT / "port" / "linux" / "src" / "port_config.c"

# The MCC HEK's hs_doc (also c20's hs_doc_sapien.txt / hs_doc_standalone.txt
# and Assembly's H1AMCC_U1_Scripting.xml, opcodes 0x1BA to 0x1D6), in order.
HS_DOC_GLOBALS = [
    "debug_ice_cream_flavor_status_grunt_birthday_party",
    "debug_ice_cream_flavor_status_assassins",
    "debug_ice_cream_flavor_status_famine",
    "debug_ice_cream_flavor_status_i_would_have_been_your_daddy",
    "debug_ice_cream_flavor_status_blind",
    "debug_ice_cream_flavor_status_ghost",
    "debug_ice_cream_flavor_status_black_eye",
    "debug_ice_cream_flavor_status_catch",
    "debug_ice_cream_flavor_status_sputnik",
    "debug_ice_cream_flavor_status_mythic",
    "debug_ice_cream_flavor_status_anger",
    "debug_ice_cream_flavor_status_thunderstorm",
    "debug_ice_cream_flavor_status_iron",
    "debug_ice_cream_flavor_status_thats_just_wrong",
    "debug_ice_cream_flavor_status_bandanna",
    "debug_ice_cream_flavor_status_boom",
    "debug_ice_cream_flavor_status_eye_patch",
    "debug_ice_cream_flavor_status_fog",
    "debug_ice_cream_flavor_status_foreign",
    "debug_ice_cream_flavor_status_grunt_funeral",
    "debug_ice_cream_flavor_status_pinata",
    "debug_ice_cream_flavor_status_recession",
    "debug_ice_cream_flavor_status_malfunction",
    "debug_ice_cream_flavor_status_streaking",
    "debug_ice_cream_flavor_status_styx",
    "debug_ice_cream_flavor_status_feather",
    "debug_ice_cream_flavor_status_bonded_pair",
    "debug_ice_cream_flavor_status_masterblaster",
    "debug_ice_cream_flavor_force_coop",
]

# The skulls with an effect here: the shared-code hook and what it asks.
HOOKS = [
    ("source/game/game_globals.c", "mcc_skulls_difficulty_scale"),  # Mythic, Anger, Catch
    ("source/ai/ai.c", "MCC_SKULL_THUNDERSTORM"),
    ("source/ai/actors.c", "mcc_skulls_actor_grenades"),  # Catch
    ("source/ai/actors.c", "mcc_skulls_actor_cloaked"),  # Assassins
    ("source/ai/actor_perception.c", "mcc_skulls_perception_scale"),  # That's Just... Wrong
    ("source/units/units.c", "mcc_skulls_unit_dropping_weapons"),  # Famine
    ("source/units/units.c", "MCC_SKULL_GHOST"),
    ("source/units/units.c", "mcc_campaign_unlimited_ammo"),  # Bandanna's grenades
    ("source/objects/damage.c", "mcc_skulls_shields_recharge"),  # Black Eye
    ("source/objects/damage.c", "mcc_skulls_melee_hit"),  # Black Eye
    ("source/objects/damage.c", "mcc_skulls_blast_scale"),  # Boom
    ("source/objects/damage.c", "mcc_skulls_impulse_scale"),  # Sputnik
    ("source/items/weapons.c", "mcc_skulls_rounds_per_shot"),  # Recession
    ("source/interface/hud_unit.c", "MCC_SKULL_FOG"),
    ("source/interface/hud.c", "MCC_SKULL_BLIND"),
    ("source/interface/first_person_weapons.c", "MCC_SKULL_BLIND"),
    ("source/game/aim_assist.c", "MCC_SKULL_EYE_PATCH"),
    ("source/main/main.c", "mcc_skulls_restart_on_death"),  # Iron
    ("port/linux/game/mcc_campaign.c", "MCC_SKULL_BANDANNA"),
]


def test_globals_are_mccs_in_hs_doc_order():
    text = SKULLS_C.read_text()
    names = re.findall(r'\{ "(debug_ice_cream_flavor_[a-z_]+)", _hs_type_boolean', text)
    names += re.findall(r'MCC_SKULL_GLOBAL\(MCC_SKULL_[A-Z_]+, "([a-z_]+)"\)', text)
    # the macro spells the prefix once
    assert '"debug_ice_cream_flavor_status_" name' in text
    listed = ["debug_ice_cream_flavor_status_" + short for short in
              re.findall(r'MCC_SKULL_GLOBAL\(MCC_SKULL_[A-Z_]+, "([a-z_]+)"\)', text)]
    listed += re.findall(r'\{ "(debug_ice_cream_flavor_force_coop)"', text)
    assert listed == HS_DOC_GLOBALS
    header = SKULLS_H.read_text()
    enum = re.search(r"enum mcc_skull \{(.*?)MCC_SKULL_COUNT", header, re.S).group(1)
    members = re.findall(r"MCC_SKULL_([A-Z_]+)", enum)
    expected = [name.replace("debug_ice_cream_flavor_status_", "").replace("debug_ice_cream_flavor_", "").upper()
                for name in HS_DOC_GLOBALS]
    assert members == expected
    assert "#define MCC_SKULL_MASK 0x1fffffffu" in header  # 29 bits


def test_table_appends_the_skulls_last():
    text = EXTERNALS.read_text()
    start = text.index("*hs_external_globals[443 + 14 + HS_EXTERNAL_GLOBAL_MCC_COUNT] =")
    assert "*hs_external_globals[443 + HS_EXTERNAL_GLOBAL_MCC_COUNT] =" in text
    end = text.index("\n};", start)
    table = text[start:end]
    last_old = max(table.rfind(line) for line in ("&rasterizer_frame_drop_ms_definition,", "&run_game_scripts_definition,"))
    skulls = re.findall(r"&mcc_skull_definitions\[(\d+)\]", table)
    assert [int(index) for index in skulls] == list(range(29))
    assert table.index("&mcc_skull_definitions[0]") > last_old
    # the count the engine walks is the table's size
    assert "short const hs_external_global_count = 443 + 14 + HS_EXTERNAL_GLOBAL_MCC_COUNT;" in text
    assert "short const hs_external_global_count = 443 + HS_EXTERNAL_GLOBAL_MCC_COUNT;" in text
    assert "#define HS_EXTERNAL_GLOBAL_MCC_COUNT MCC_SKULL_COUNT" in text
    # one may-a-map-set-it row per skull, FALSE: an MCC map's permission comes
    # from mcc_campaign_global_settable, no other map names them
    start = text.index("hs_external_global_settable_in_maps[]=")
    end = text.index("\n};", start)
    rows = text[start:end]
    mcc = rows[rows.index("MCC's skulls"):]
    assert len(re.findall(r"\bFALSE\b", mcc)) == 29
    assert "TRUE" not in mcc
    assert "mcc_skull_global(name)" in CAMPAIGN_C.read_text()


def test_snapshot_carries_the_skulls():
    header = CAMPAIGN_H.read_text()
    assert "#define MCC_CAMPAIGN_SNAPSHOT_V6_BYTES 772u" in header
    assert "#define MCC_CAMPAIGN_SNAPSHOT_BYTES 776u" in header
    text = CAMPAIGN_C.read_text()
    assert "uint32_t skulls;" in text
    assert "mcc_campaign.version=7;" in text
    assert "bytes==MCC_CAMPAIGN_SNAPSHOT_V6_BYTES && state.version!=6u" in text
    assert "state.version!=7u" in text
    assert "mcc_skulls_restore(mcc_campaign.skulls);" in text
    assert "mcc_campaign.skulls=mcc_skulls_snapshot();" in text
    assert "mcc_skulls_begin();" in text and "mcc_skulls_dispose();" in text


def test_every_effect_has_its_hook():
    for path, token in HOOKS:
        text = (ROOT / path).read_text()
        assert token in text, (path, token)
        if path.startswith("source/"):
            assert '#include "mcc_skulls.h"' in text, path
    header = SKULLS_H.read_text()
    # a console build without MCC maps gets inert hooks
    for name in ("mcc_skull_active", "mcc_skulls_difficulty_scale", "mcc_skulls_blast_scale",
                 "mcc_skulls_unit_dropping_weapons", "mcc_skulls_rounds_per_shot",
                 "mcc_skulls_restart_on_death", "mcc_skulls_shields_recharge"):
        assert re.search(r"#define %s\(" % name, header), name


def test_skulls_only_on_mcc_maps():
    text = SKULLS_C.read_text()
    active = text[text.index("boolean mcc_skull_active(short skull)"):]
    active = active[:active.index("}")]
    assert "mcc_cache_tags_loaded()" in active and "mcc_skulls_owned" in active


def test_setting_and_old_design_gone():
    assert '"game.mcc_skulls", _config_string' in CONFIG_C.read_text()
    assert 'config_string("game.mcc_skulls")' in SKULLS_C.read_text()
    scripts = SCRIPTS_C.read_text()
    assert "only a literal disable" not in scripts
    assert "mcc_disabled_skull" not in scripts
    assert "don't change play yet" not in scripts
