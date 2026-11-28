#!/usr/bin/env python3
"""Forge leaves out a weapon nobody can aim, and its ammunition (#1589).

Every Xbox multiplayer map carries the unfinished flamethrower's tags,
though no game puts it down. Its one trigger is marked "projectiles cannot
be aimed", so weapons.c fires its flames along the gun's own marker, and the
Spartan has no animations for it: the flames always landed on the ground in
front of the player's feet, wherever they looked, and its HUD had no reticle.
Forge's palette (port/linux/game/forge.c, forge_tag_category) offered it, and
its ammunition, as any other weapon.

This builds forge_tag_category and the two checks it now makes
(forge_weapon_cannot_be_aimed, forge_equipment_only_unaimed_ammunition) from
the sources with a small set of tags standing in for a map's, and checks:
    - the assault rifle and a weapon with one aimed trigger of two are offered;
    - a weapon whose every trigger can't be aimed (the Xbox flamethrower) is not;
    - its ammunition is not, while ammunition another, aimed, weapon also uses is;
    - the rest of the palette (vehicles, the gravity rifle, grenades) as before;
    - an AI character (an actor variant that names its unit) is offered as AI,
      a vehicle's crew and a variant with no unit are not;
and that the checks fail when the flag is ignored (a negative control).
Needs clang (or cc) and Python 3 alone.

    python3 -m pytest tools/test_forge_unaimed_weapons.py
"""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
FORGE = ROOT / "port" / "linux" / "game" / "forge.c"
WEAPON_DEFINITIONS = ROOT / "source" / "items" / "weapon_definitions.h"


def function(source, name):
    match = re.search(r"^(?:static )?[\w *]+\b" + name + r"\(\s*[^;{]*\)\s*\{", source, re.M)
    assert match, f"function not found: {name}"
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def trigger_bit(name):
    """the bit of a weapon trigger flag, from the enum in weapon_definitions.h"""
    text = WEAPON_DEFINITIONS.read_text()
    body = re.search(r"enum weapon_trigger_definition_flags\s*\{\s*(.*?)\};", text, re.S)
    assert body, "the weapon trigger flags' enum is not found"
    names = [entry.split("=")[0].strip() for entry in body.group(1).split(",") if entry.strip()]
    return names.index(name)


PRELUDE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int boolean;
typedef unsigned short word;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define TEST_FLAG(flags, bit) (((flags) >> (bit)) & 1)
#define VEHICLE_DEFINITION_TAG 'vehi'
#define WEAPON_DEFINITION_TAG 'weap'
#define EQUIPMENT_DEFINITION_TAG 'eqip'
#define FORGE_SCENERY_DEFINITION_TAG 'scen'
#define MACHINE_DEFINITION_TAG 'mach'
#define CONTROL_DEFINITION_TAG 'ctrl'
#define ACTOR_VARIANT_DEFINITION_TAG 'actv'
#define LIGHT_FIXTURE_DEFINITION_TAG 'lifi'
enum { _weapon_trigger_projectiles_cannot_be_aimed_bit = CANNOT_BE_AIMED_BIT };
enum { _forge_category_vehicles = 0, _forge_category_weapons, _forge_category_equipment, _forge_category_scenery,
    _forge_category_ai, _forge_category_campaign_vehicles, _forge_category_campaign_objects };
/* (no tag here was brought in from a campaign map: forge_ai_tags.c) */
static char const *forge_ai_tags_source_map(long tag_index) { (void)tag_index; return 0; }
static int forge_ai_tags_offered(long tag_index) { (void)tag_index; return 1; }
struct tag_block { long count; void *address; };
struct tag_reference { unsigned long group_tag; long index; };
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->address)[index])
struct weapon_trigger_definition { unsigned long flags; };
struct weapon_ammunition_object { short rounds; struct tag_reference object; };
struct weapon_magazine_definition { struct tag_block ammunition_objects; };
struct weapon_definition { struct { struct tag_block magazines; struct tag_block triggers; } weapon; };
struct actor_variant_definition { struct tag_reference unit_reference; };

/* the map's tags */
struct tag { unsigned long group; char const *name; struct weapon_definition weapon;
    struct actor_variant_definition variant; };
enum { TAG_COUNT = 15 };
static struct tag tags[TAG_COUNT];
static unsigned long tag_get_group_tag(long index) { return tags[index].group; }
static char const *tag_get_name(long index) { return tags[index].name; }
static struct weapon_definition *weapon_definition_get(long index) {
    if (tags[index].group != WEAPON_DEFINITION_TAG) abort();
    return &tags[index].weapon;
}
static struct actor_variant_definition *actor_variant_definition_get(long index) {
    if (tags[index].group != ACTOR_VARIANT_DEFINITION_TAG) abort();
    return &tags[index].variant;
}
struct tag_iterator { unsigned long group; long next; };
static void tag_iterator_new(struct tag_iterator *iterator, unsigned long group) { iterator->group = group; iterator->next = 0; }
static long tag_iterator_next(struct tag_iterator *iterator) {
    while (iterator->next < TAG_COUNT)
        if (tags[iterator->next++].group == iterator->group)
            return iterator->next - 1;
    return NONE;
}
'''

MAIN = r'''
static int failures;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); failures++; } } while (0)

enum { AIMED = 0, UNAIMED = 1u << _weapon_trigger_projectiles_cannot_be_aimed_bit };

static struct weapon_trigger_definition triggers[6];
static struct weapon_magazine_definition magazines[4];
static struct weapon_ammunition_object ammunition[4];

/* a weapon of one magazine (fed by ammunition tag `ammo`, NONE for none) and
`count` triggers from `first` */
static void weapon(long index, char const *name, int first, int count, int magazine, long ammo) {
    tags[index].group = WEAPON_DEFINITION_TAG;
    tags[index].name = name;
    tags[index].weapon.weapon.triggers.count = count;
    tags[index].weapon.weapon.triggers.address = &triggers[first];
    tags[index].weapon.weapon.magazines.count = 1;
    tags[index].weapon.weapon.magazines.address = &magazines[magazine];
    magazines[magazine].ammunition_objects.count = ammo == NONE ? 0 : 1;
    magazines[magazine].ammunition_objects.address = &ammunition[magazine];
    ammunition[magazine].object.group_tag = EQUIPMENT_DEFINITION_TAG;
    ammunition[magazine].object.index = ammo;
}

static void other(long index, unsigned long group, char const *name) { tags[index].group = group; tags[index].name = name; }

int main(void) {
    triggers[0].flags = AIMED;          /* assault rifle */
    triggers[1].flags = UNAIMED;        /* flamethrower */
    triggers[2].flags = AIMED;          /* two triggers, one aimed */
    triggers[3].flags = UNAIMED;
    triggers[4].flags = UNAIMED;        /* a second unaimed weapon sharing aimed ammunition */
    weapon(0, "weapons\\assault rifle\\assault rifle", 0, 1, 0, 6);
    weapon(1, "weapons\\flamethrower\\flamethrower", 1, 1, 1, 7);
    weapon(2, "weapons\\mixed\\mixed", 2, 2, 2, NONE);
    weapon(3, "weapons\\odd\\odd", 4, 1, 3, 6);
    other(4, VEHICLE_DEFINITION_TAG, "vehicles\\warthog\\mp_warthog");
    other(5, WEAPON_DEFINITION_TAG, "weapons\\gravity rifle\\gravity rifle");
    tags[5].weapon.weapon.triggers.count = 1;
    tags[5].weapon.weapon.triggers.address = &triggers[0];
    other(6, EQUIPMENT_DEFINITION_TAG, "powerups\\assault rifle ammo\\assault rifle ammo");
    other(7, EQUIPMENT_DEFINITION_TAG, "powerups\\flamethrower ammo\\flamethrower ammo");
    other(8, EQUIPMENT_DEFINITION_TAG, "weapons\\frag grenade\\frag grenade");
    other(9, EQUIPMENT_DEFINITION_TAG, "powerups\\health pack");
    other(10, FORGE_SCENERY_DEFINITION_TAG, "scenery\\rocks\\boulder");
    other(11, WEAPON_DEFINITION_TAG, "vehicles\\warthog\\warthog gun");
    tags[11].weapon.weapon.triggers.count = 1;
    tags[11].weapon.weapon.triggers.address = &triggers[0];
    /* (AI characters: an actor variant with its unit; a vehicle's crew; one
    that names no unit) */
    other(12, ACTOR_VARIANT_DEFINITION_TAG, "characters\\elite\\elite major\\elite major plasma rifle");
    tags[12].variant.unit_reference.index = 4;
    other(13, ACTOR_VARIANT_DEFINITION_TAG, "vehicles\\banshee\\banshee pilot");
    tags[13].variant.unit_reference.index = 4;
    other(14, ACTOR_VARIANT_DEFINITION_TAG, "characters\\elite\\elite unitless");
    tags[14].variant.unit_reference.index = NONE;

    CHECK(forge_tag_category(0) == _forge_category_weapons, "the assault rifle is left out");
    CHECK(forge_tag_category(1) == NONE, "the flamethrower nobody can aim is offered");
    CHECK(forge_tag_category(2) == _forge_category_weapons, "a weapon with one aimed trigger is left out");
    CHECK(forge_tag_category(3) == NONE, "a second weapon nobody can aim is offered");
    CHECK(forge_tag_category(4) == _forge_category_vehicles, "the warthog is not a vehicle");
    CHECK(forge_tag_category(5) == NONE, "the gravity rifle is offered");
    CHECK(forge_tag_category(6) == _forge_category_equipment, "ammunition an aimed weapon uses is left out");
    CHECK(forge_tag_category(7) == NONE, "the flamethrower's ammunition is offered");
    CHECK(forge_tag_category(8) == _forge_category_weapons, "the frag grenade is not a weapon");
    CHECK(forge_tag_category(9) == _forge_category_equipment, "the health pack is left out");
    CHECK(forge_tag_category(10) == _forge_category_scenery, "the boulder is left out");
    CHECK(forge_tag_category(11) == NONE, "a vehicle's gun is offered");
    CHECK(forge_tag_category(12) == _forge_category_ai, "an AI character is left out");
    CHECK(forge_tag_category(13) == NONE, "a vehicle's crew is offered");
    CHECK(forge_tag_category(14) == NONE, "an actor variant with no unit is offered");
    printf("%d failure(s)\n", failures);
    return failures != 0;
}
'''


def build(tmp, mutate=None):
    compiler = shutil.which("clang") or shutil.which("cc")
    if not compiler:
        pytest.skip("needs clang or cc")
    source = FORGE.read_text()
    code = "\n".join(function(source, name) for name in
                     ("forge_weapon_cannot_be_aimed", "forge_equipment_only_unaimed_ammunition", "forge_tag_category"))
    if mutate:
        code = mutate(code)
    bit = trigger_bit("_weapon_trigger_projectiles_cannot_be_aimed_bit")
    program = Path(tmp) / "forge_unaimed.c"
    program.write_text(PRELUDE.replace("CANNOT_BE_AIMED_BIT", str(bit)) + code + MAIN)
    binary = Path(tmp) / "forge_unaimed"
    subprocess.run([compiler, "-std=c99", "-Wall", "-Werror", "-Wno-multichar", "-Wno-unused-function",
                    "-o", str(binary), str(program)], check=True)
    return subprocess.run([str(binary)], capture_output=True, text=True)


def test_forge_leaves_out_weapons_nobody_can_aim():
    with tempfile.TemporaryDirectory() as tmp:
        result = build(tmp)
    assert result.returncode == 0, result.stdout + result.stderr


def test_the_check_fails_when_the_flag_is_ignored():
    # (negative control: a palette that ignores the trigger flag offers the
    # flamethrower and its ammunition again, and the checks say so)
    def ignore_flag(code):
        changed = code.replace("!TEST_FLAG(trigger->flags, _weapon_trigger_projectiles_cannot_be_aimed_bit)",
                               "(trigger->flags || TRUE)")
        assert changed != code
        return changed

    with tempfile.TemporaryDirectory() as tmp:
        result = build(tmp, ignore_flag)
    assert result.returncode != 0
    assert "the flamethrower nobody can aim is offered" in result.stderr
    assert "the flamethrower's ammunition is offered" in result.stderr


def test_the_bit_is_the_xbox_flamethrowers():
    # (the Xbox maps' flamethrower trigger's flags are 0x800: bit 11)
    assert trigger_bit("_weapon_trigger_projectiles_cannot_be_aimed_bit") == 11


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-q"]))
