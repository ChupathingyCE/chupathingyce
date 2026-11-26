"""A text box's text in the widget pool: when the pool is full, the text box keeps its own block, and the next
frames draw without halting in stack_memory_pool.c (#1832)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure

CASES = ['grows', 'full', 'spinner', 'kept']


def generated(fault=None):
    pool = read('source/memory/stack_memory_pool.c')
    config = '\n'.join([
        structure(pool, 'stack_memory_pool_block'),
        structure(pool, 'stack_memory_pool'),
    ]) + '\n'
    # (the pool's code after its structures: its prototypes, then every function)
    code = pool[pool.index('/* ---------- prototypes */'):] + '\n' + \
        function(read('source/interface/ui_widget.c'), 'widget_instance_text_box_text_set')
    # (the spinner's give-back of its item text, from widget_instance_render_spinner_list)
    render = function(read('source/interface/ui_widget.c'), 'widget_instance_render_spinner_list')
    give_back = re.search(r'\n\t\tif \(definition->text_label_string_list\.index != NONE[^\n]*\n\t\t\tdispose_pointer\(widget_memory_pool, item_text\);',
                          render).group(0)
    code += ('\nstatic void spinner_item_text_dispose(wchar_t *item_text)\n{\n'
             '\tstruct { struct { long index; } text_label_string_list; } spinner = { { 0 } }, *definition = &spinner;\n'
             + give_back + '\n}\n')
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('text_box_pool', generated()), case)
    assert status == 0, output


def test_negative_control():
    # (the text as the Xbox's code set it: the "<out of memory>" string kept in the block's place)
    fault = ('\tif (text)\n\t{\n\t\twidget->parameters.text_box.text = text;',
             '\twidget->parameters.text_box.text = text ? text : (wchar_t *)L"<out of memory>";\n\tif (text)\n\t{')
    status, output = run(build('text_box_pool', generated(fault)), 'full')
    assert status == CHECK_FAILED and 'stack_memory_pool_valid_block' in output, output


def test_negative_control_spinner():
    # (the spinner's give-back as the Xbox's code had it: the NULL given back too)
    fault = ('index != NONE && item_text)', 'index != NONE)')
    status, output = run(build('text_box_pool', generated(fault)), 'spinner')
    assert status == CHECK_FAILED and 'invalid pointer' in output, output
