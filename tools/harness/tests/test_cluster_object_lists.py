# Dynamic Lighting, Dynamic Shadows and shared renderer optimisations by
# chestahh (https://github.com/chestahh), originally developed for GulchCE
# (https://github.com/chestahh/GulchCE). Original OpenCE port:
# https://github.com/chestahh/GulchCE/tree/035f47bb8fc682675456323dbb3095b506502ec6
# From GulchCE by chestahh (granted 2026-11-26). Changed for Chupathingy (see the git history).
#
# SPDX-FileCopyrightText: 2026 chestahh (GulchCE)
# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""The lists of a cluster's objects (object_bounds_cache.c) are the objects collision's loop would have walked, in
order; the cluster partition counts each change to a cluster's list; collision's vector loop goes through the list.

    python -m pytest -q tools/harness/tests/test_cluster_object_lists.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["matches", "deleted", "invalidated", "detects-stale", "refused"]

# a fault in the lists, and the case that must catch it
NEGATIVE_CONTROLS = {
    "ignores-changes": (("entry->changes != changes || ", ""), "matches"),
    "ignores-invalidation": (("entry->epoch != object_bounds_epoch || ", ""), "invalidated"),
    "ignores-types": (("entry->changes != changes || entry->types != types", "entry->changes != changes"), "matches"),
    "lists-for-bipeds": (("if (TEST_FLAG(flags, _collision_test_objects_bipeds_bit) ||\n", "if (FALSE ||\n"),
                         "refused"),
}

START = "#ifdef HALO_PORT_CLUSTER_OBJECT_LISTS\n/* Each cluster's"


def lf(text):
    return text.replace("\r\n", "\n")


def lists_code():
    cache = lf(read("port/linux/game/object_bounds_cache.c"))
    code = cache[cache.index(START):]
    return code.split("\n", 1)[1].rsplit("#endif", 1)[0]


def under_test(fault=None):
    enums = enum_with(read("source/objects/object_types.h"), "_object_type_biped") + "\n"
    enums += enum_with(read("source/physics/collisions.h"), "_collision_test_objects_bit") + "\n"
    enums += ("#define MAXIMUM_CLUSTERS_PER_STRUCTURE "
              f"{constant(read('source/structures/structures.h'), 'MAXIMUM_CLUSTERS_PER_STRUCTURE')}\n")
    code = lists_code()
    return (("enums.inc", enums), ("under_test.inc", mutated(code, *fault) if fault else code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("cluster_object_lists", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("cluster_object_lists", under_test(fault)), case)
    assert status == CHECK_FAILED, f"{control} was not caught by {case}: {output}"


def test_partition_counts_each_cluster_change():
    """Joining a cluster, leaving it and every forgetting (a game state loaded, a new map, a copy) count a change to
    the cluster's list; only the desktop builds keep the counts."""
    partitions = lf(read("source/structures/cluster_partitions.c"))
    changed = "cluster_partition_port_cluster_changed(partition, cluster_index);"
    for name in ("cluster_partition_reconnect", "cluster_partition_disconnect", "cluster_partition_port_forget"):
        body = function(partitions, name)
        assert changed in body, name
        assert body.rindex("#ifdef HALO_PORT_CLUSTER_OBJECT_LISTS", 0, body.index(changed)) >= 0, name
    forget = function(partitions, "cluster_partition_port_forget")
    assert "for (cluster_index = 0; cluster_index < MAXIMUM_CLUSTERS_PER_STRUCTURE; cluster_index++)" in forget
    new = function(partitions, "cluster_partition_new")
    assert new.index("port_cluster_modification_counts = debug_malloc(") < new.index("cluster_partition_port_forget(")
    for name in ("cluster_partition_make_valid", "cluster_partition_copy"):
        assert "cluster_partition_port_forget(" in function(partitions, name), name
    features = lf(read("port/linux/include/halo_port_features.h"))
    block = features[features.index("#define HALO_PORT_CLUSTER_OBJECT_LISTS") - 400:]
    assert "#if !(defined(HALO_CONSOLE) ||" in block and "defined(HALO_XBOX_CONSOLE)" in block


def test_vector_loop_walks_the_list():
    """Collision's vector loop takes the list when there is one and the cluster's own list when not, and tests each
    object as before; a console walks the cluster's list."""
    collisions = lf(read("source/physics/collisions.c"))
    call = "object_bounds_cache_cluster_objects((short)cluster_index, flags, &cached_count)"
    loop = collisions[collisions.index(call):]
    loop = loop[:loop.index("object_test_vector(")]
    assert "cached_count > 0 ? cached[0] : NONE" in loop
    assert "++cached_next < cached_count ? cached[cached_next] : NONE" in loop
    assert "cluster_get_first_collideable_object(&reference_index, cluster_index)" in loop
    assert "object_mark_function(object_index)" in loop
    assert "#define object_bounds_cache_cluster_objects(cluster_index, flags, count) ((long const *)NULL)" in collisions
