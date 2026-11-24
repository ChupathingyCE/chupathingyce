"""usprintf doesn't read the destination it is about to write (the scoreboard's unwritten score strings)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, read, run  # noqa: E402

CASES = ['unwritten', 'written']
# the assertion the fix took out: with it the unwritten destination is read past its end
REMOVED = ('\tmatch_assert(\n\t\t"c:\\\\halo\\\\SOURCE\\\\text\\\\unicode.c",\n\t\t752,\n'
           '\t\twcslen(string) < MAXIMUM_STRING_SIZE);\n')


def generated(fault=False):
    source = read('source/text/unicode.c')
    config = '#define MAXIMUM_STRING_SIZE %d\n' % constant(source, 'MAXIMUM_STRING_SIZE')
    code = function(source, 'usprintf')
    if fault:
        at = code.index('\tmatch_assert(', code.index('751'))
        code = code[:at] + REMOVED + code[at:]
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('usprintf_destination', generated()), case)
    assert status == 0, output


def test_negative_control():
    status, output = run(build('usprintf_destination', generated(True)), 'unwritten')
    assert status == CHECK_FAILED, output
