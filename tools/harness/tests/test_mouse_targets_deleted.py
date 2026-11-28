"""The pointer's targets forget the widgets ui_widget_delete deletes (#1498)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CONTROLS = {
    'delete-keeps-targets': ('ui_mouse_forget_widget', 'ui_widget_delete',
                             'ui_mouse_forget_widget(widget);', ';', 'screen'),
    'screen-keeps-click': ('ui_mouse_forget_widget', 'ui_mouse_forget_widget',
                           'ui_mouse_click_pending = FALSE;', ';', 'screen'),
    'label-kept': ('ui_mouse_forget_widget', 'ui_mouse_forget_widget',
                   'target->label = NULL;\n\t\t}', '}', 'label'),
}


def generated(control=None):
    source = read('source/interface/ui_widget.c')
    names = ('ui_widget_delete', 'ui_widget_delete_children_recursive', 'ui_mouse_target_at',
             'ui_mouse_forget_widget')
    functions = {name: function(source, name) for name in names}
    if control:
        _, name, before, after, _ = CONTROLS[control]
        functions[name] = mutated(functions[name], before, after)
    code = [
        enum_with(source, '_ui_mouse_target_item'),
        structure(source, 'ui_mouse_target'),
        f"#define UI_MOUSE_MAXIMUM_TARGETS {constant(source, 'UI_MOUSE_MAXIMUM_TARGETS')}",
        'static struct ui_mouse_target ui_mouse_targets[UI_MOUSE_MAXIMUM_TARGETS];',
        'static long ui_mouse_target_count;',
        'static boolean ui_mouse_hover_pending, ui_mouse_click_pending;',
        'static void ui_widget_delete(struct widget_instance *widget);',
        'static void ui_widget_delete_children_recursive(struct widget_instance *widget);',
        'static void ui_mouse_forget_widget(struct widget_instance *widget);',
        'static struct widget_instance *ui_widget_launch_widget(struct widget_instance *widget, long tag_index)'
        ' { (void)widget; (void)tag_index; return NULL; }',
        'static void ui_widget_port_deleted_handler_script(struct ui_widget_event_handler_reference const *handler)'
        ' { (void)handler; }',
    ]
    # (ui_widget_delete is public in the game; static here, as the others)
    code += [functions[name].replace('void ui_widget_delete(', 'static void ui_widget_delete(', 1)
             if name == 'ui_widget_delete' else functions[name] for name in names]
    return (('under_test.inc', '\n\n'.join(code)),)


@pytest.mark.parametrize('case', ['screen', 'row', 'label'])
def test_mouse_targets_deleted(case):
    status, output = run(build('mouse_targets_deleted', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('control', sorted(CONTROLS))
def test_negative_control(control):
    status, output = run(build('mouse_targets_deleted', generated(control)), CONTROLS[control][4])
    assert status == CHECK_FAILED, output
