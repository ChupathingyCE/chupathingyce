"""A script's cast of an object to a unit: a deleted object casts to none instead of halting the game (#1829)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure

CASES = ['live-unit', 'wrong-type', 'none', 'deleted', 'reused']


def generated(fault=None):
    types = read('source/objects/object_types.h')
    objects = read('source/objects/objects.h')
    masks = re.search(r'word const hs_object_type_masks\[NUMBER_OF_HS_OBJECT_TYPES\]=\s*\{[^}]*\};', read('source/hs/hs.c'))
    config = '\n'.join([
        enum_with(types, '_object_mask_all'),
        structure(objects, 'object_header_datum'),
        'enum { NUMBER_OF_HS_OBJECT_TYPES = 6 };',
        'static ' + masks.group(0),
    ]) + '\n'
    code = '\n'.join([
        'static ' + function(read('source/memory/data.c'), 'datum_try_and_get'),
        'static ' + function(read('source/objects/objects.c'), 'object_try_and_get_and_verify_type'),
        re.search(r'#define object_try_and_get\(index\).*', objects).group(0),
        'static struct object_datum *object_get(long index);',
        function(read('source/hs/hs_runtime.c'), 'hs_evaluate_object_cast_up'),
    ])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('hs_object_cast', generated()), case)
    assert status == 0, output


def test_negative_control():
    # (the cast as it was: object_get halts on the deleted object)
    fault = ('object_try_and_get(*object_index);', 'object_get(*object_index);')
    status, output = run(build('hs_object_cast', generated(fault)), 'deleted')
    assert status == CHECK_FAILED, output
