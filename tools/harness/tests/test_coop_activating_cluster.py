"""A co-op client drops the host's activating cluster after its own structure BSP switch (objects.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ['same-bsp', 'switched-after', 'own-script']
CONTROLS = {
    # the host's cluster kept across the switch (main before the fix)
    'kept-across-switch': ('objects_port_activating_structure_bsp_index != global_structure_bsp_index_get()', 'FALSE',
                           'switched-after'),
    # a scripted cluster taken for the host's
    'script-not-told-apart': ('\t\t\tobjects_port_activating_structure_bsp_index = NONE;\n', '', 'own-script'),
}


def generated(control=None):
    source = read('source/objects/objects.c')
    marker = re.search(r"^static short objects_port_activating_structure_bsp_index = NONE;", source, re.M).group(0)
    code = marker + '\n' + '\n'.join(function(source, name) for name in (
        'object_pvs_set_camera_point', 'objects_port_set_activating_cluster', 'objects_get_activating_cluster_index'))
    if control:
        before, after, _ = CONTROLS[control]
        code = mutated(code, before, after)
    config = enum_with(read('source/objects/objects.h'), '_pvs_activation_cluster') + '\n'
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('coop_activating_cluster', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
    status, output = run(build('coop_activating_cluster', generated(control)), CONTROLS[control][2])
    assert status == CHECK_FAILED, f'{control} was not caught: {output}'
