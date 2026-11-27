"""System Link's DIRECT IP row (#1880): the Chupathingy menus' list ends with
it, the Xbox menus' list is the Xbox's."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

SOURCE = 'source/interface/ui_widget_game_data_input_functions.c'
CONTROLS = {
    # the row never added: the Chupathingy menus' list is the Xbox's
    'no-row': ([('\t\tif (direct_ip_row)\n\t\t{\n\t\t\tdisplayed_servers[displayed_server_count] = NULL;',
                 '\t\tif (FALSE)\n\t\t{\n\t\t\tdisplayed_servers[displayed_server_count] = NULL;')], 'empty'),
    # the row on every list, the Xbox menus' too
    'xbox-row': ([('if (pc_menus_direct_ip_list() != NONE && widget->definition_tag_index == pc_menus_direct_ip_list())',
                   'if (TRUE)')], 'xbox'),
    # the row not kept chosen as games come
    'not-kept': ([('if (direct_ip_chosen || displayed_server_count == 0)', 'if ((direct_ip_chosen && FALSE) || displayed_server_count == 0)')],
                 'games'),
}


def generated(control=None):
    source = read(SOURCE)
    instance = read('source/interface/ui_widget_instance.h')
    update = function(source, 'server_list_menu_update')
    if control:
        for before, after in CONTROLS[control][0]:
            update = mutated(update, before, after)
    types = [
        enum_with(source, '_ui_widget_type_text_box,'),
        enum_with(source, 'MAXIMUM_NETWORK_ADVERTISED_GAMES = 9').replace(
            'HALO_PORT_MAXIMUM_NETWORK_MACHINES', '16'),
        enum_with(source, '_network_game_platform_xbox,'),
        enum_with(source, '_multiplayer_game_text_string_captures = 22'),
        enum_with(source, '_multiplayer_game_bitmap_ctf = 0'),
        enum_with(read('source/game/game_engine.h'), 'game_engine_bomb,'),
        structure(source, 'network_advertised_game'),
        structure(instance, 'widget_animation_data'),
        structure(instance, 'widget_instance').replace('ui_widget_animation_data', 'widget_animation_data'),
    ]
    return (('under_test_types.inc', '\n\n'.join(types)), ('under_test.inc', update))


@pytest.mark.parametrize('case', ['xbox', 'empty', 'games', 'full'])
def test_system_link_direct_ip(case):
    status, output = run(build('system_link_direct_ip', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('control', sorted(CONTROLS))
def test_negative_control(control):
    status, output = run(build('system_link_direct_ip', generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, output
