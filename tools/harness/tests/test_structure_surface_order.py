"""structure_render.c's surface lists: sorted and walked into the same surfaces, in the same order, as before."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ['sort', 'walk']
CONTROLS = [
    # an off-by-one in the radix sort's digits
    ('counts[((unsigned long)from[index] >> shift) & 2047]++;\n', 'counts[((unsigned long)from[index] >> shift) & 1023]++;\n',
     'sort'),
    # an ascending check that takes a list with one out of place as sorted
    ('if (index && surface_indices[index] < surface_indices[index - 1])', 'if (index == 1 && surface_indices[1] < surface_indices[0])',
     'sort'),
    # the last word not masked to the surfaces the structure has
    ('bits &= ((unsigned long)1 << (structure_bsp->surfaces.count % LONG_BITS)) - 1;', ';', 'walk'),
]


def generated(fault=None):
    source = read('source/structures/structure_render.c')
    config = '#define LONG_BITS %d\n' % constant(read('source/cseries/cseries.h'), 'LONG_BITS')
    config += '#define NUMBER_OF_VERTICES_PER_TRIANGLE 3\n#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))\n'
    config += '#define MAXIMUM_LOCALLY_RENDERED_SURFACES %d\n' % constant(source, 'MAXIMUM_LOCALLY_RENDERED_SURFACES')
    code = '\n'.join(function(source, name) for name in (
        'structure_render_radix_sort_surface_indices', 'structure_render_sort_surface_indices',
        'structure_render_dynamic_triangles_from_bitvector'))
    if fault:
        code = mutated(code, *fault[:2])
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('structure_surface_order', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('structure_surface_order', generated((before, after))), case)
    assert status == CHECK_FAILED, output
