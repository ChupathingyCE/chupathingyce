"""Script commands handed an object the scripts kept after it was deleted
(#1861): each takes it as none instead of halting the game, as the cast does
since #1858. The commands' real code, run against a fake world with the object
live, deleted and its slot reused by another."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

# (source file, functions) whose real code the test runs
SOURCES = [
    ('source/objects/objects.c', ('object_try_and_get_and_verify_type', 'object_beautify', 'object_scripting_set_collideable',
                                  'objects_scripting_set_scale', 'objects_scripting_attach', 'objects_scripting_detach')),
    ('source/objects/damage.c', ('object_can_take_damage', 'object_cannot_take_damage', 'object_set_ranged_attack_inhibited',
                                 'object_set_melee_attack_inhibited')),
    ('source/objects/scenery.c', ('scenery_get_animation_time', 'scenery_animation_start_private', 'scenery_animation_start',
                                  'scenery_animation_start_at_frame')),
    ('source/scenario/scenario.c', ('scenario_trigger_volume_test_object',)),
    ('source/devices/devices.c', ('device_get_position', 'device_get_power', 'device_set_actual_position',
                                  'device_set_desired_position', 'device_set_power')),
    ('source/cutscene/recorded_animations.c', ('recorded_animation_play_internal', 'recorded_animation_play',
                                               'recorded_animation_play_and_delete', 'recorded_animation_play_and_hover')),
    ('source/units/vehicles.c', ('vehicle_hover',)),
    ('source/game/players.c', ('player_add_equipment',)),
    ('source/units/units.c', ('unit_kill', 'unit_kill_silent', 'unit_set_enterable_by_player', 'unit_aim_without_turning',
                              'unit_scripting_can_blink', 'unit_set_desired_flashlight_state', 'unit_scripting_unit_riders',
                              'unit_scripting_vehicle_test_seat_list', 'unit_scripting_vehicle_test_seat',
                              'unit_has_weapon_definition_index', 'unit_scripting_set_emotion_animation',
                              'unit_scripting_suspended', 'unit_scripting_set_seat', 'unit_get_current_flashlight_state',
                              'unit_set_emotion', 'unit_is_playing_custom_animation', 'unit_get_custom_animation_time',
                              'unit_start_user_animation', 'unit_custom_animation_at_frame', 'unit_stop_custom_animation',
                              'unit_scripting_enter_vehicle', 'vehicle_scripting_unload', 'unit_scripting_exit_vehicle',
                              'vehicle_scripting_load_magic', 'unit_open', 'unit_close')),
    ('source/units/unit_scripting_commands.c', ('unit_scripting_set_maximum_vitality', 'units_scripting_set_maximum_vitality',
                                                'unit_scripting_set_current_vitality', 'unit_scripting_set_current_vitality_of',
                                                'units_scripting_set_current_vitality', 'unit_scripting_has_weapon',
                                                'unit_scripting_has_weapon_readied')),
    ('source/ai/ai_script.c', ('ai_scripting_detach_unit', 'ai_scripting_detach_units', 'ai_scripting_stop_looking',
                               'ai_scripting_force_active_by_unit', 'ai_scripting_look_at_object',
                               'ai_scripting_vehicle_encounter', 'ai_scripting_magically_see_unit',
                               'ai_scripting_magically_see_units', 'ai_scripting_attach_unit')),
    ('source/sound/game_sound.c', ('scripted_sound_new', 'scripted_looping_sound_start')),
    ('source/hs/hs_library_external.c', ('hs_trigger_volume_test_objects', 'hs_unit_can_see_object', 'hs_any_player_can_see',
                                         'hs_objects_can_see_object', 'object_is_or_contains_player', 'hs_object_destroy',
                                         'hs_object_set_shield', 'hs_object_set_permutation', 'hs_objects_predict',
                                         'hs_effect_new_from_object_marker', 'hs_damage_object', 'hs_object_orient',
                                         'hs_object_teleport', 'hs_object_set_facing')),
]
# each script command, as main runs it (hs_deleted_objects.c's COMMANDS)
COMMANDS = [line.split('"')[1] for line in (Path(__file__).parent / 'hs_deleted_objects.c').read_text().splitlines()
            if line.lstrip().startswith('COMMAND("')]
STATES = ['live', 'none', 'deleted', 'reused']

# a guard taken back to the code before the fix: the command halts again
CONTROLS = [
    ('unit_kill', '\tif (!object_try_and_get(unit_index))\n\t\treturn;\n\tunit = unit_get(unit_index);\n\tSET_FLAG(unit->object.damage_flags, _object_die_act_of_god_bit',
     '\tunit = unit_get(unit_index);\n\tSET_FLAG(unit->object.damage_flags, _object_die_act_of_god_bit', 'unit_kill'),
    ('hs_object_set_shield', 'if (object_try_and_get(object_index))', 'if (object_index != NONE)', 'object_set_shield'),
    ('device_set_power', 'if (object_try_and_get(device_index))', 'if (device_index != NONE)', 'device_set_power'),
    ('object_cannot_take_damage', 'object_try_and_get(object_index);', 'object_get(object_index);', 'object_cannot_take_damage'),
    ('scripted_sound_new', 'if (!object_try_and_get(source_object_index))', 'if (FALSE)', 'sound_impulse_start'),
    ('ai_scripting_detach_unit', 'if (object_try_and_get(unit_index))', 'if (unit_index != NONE)', 'ai_detach'),
]


def generated(control=None):
    config = '\n'.join([
        enum_with(read('source/objects/object_types.h'), '_object_mask_all'),
        structure(read('source/objects/objects.h'), 'object_header_datum'),
    ]) + '\n'
    objects = read('source/objects/objects.h')
    code = [
        'static ' + function(read('source/memory/data.c'), 'datum_try_and_get'),
        re.search(r'#define object_try_and_get\(index\).*', objects).group(0),
    ]
    prototypes = []
    for path, names in SOURCES:
        source = read(path)
        for name in names:
            text = function(source, name)
            if control and control[0] == name:
                text = mutated(text, control[1], control[2])
            # (each function static here: the fake world declares what it calls)
            if not text.startswith('static '):
                text = 'static ' + text
            prototypes.append(text[:text.index('{')].rstrip() + ';')
            code.append(f'/* {path} */\n{text}')
    return (('config.inc', config), ('under_test.inc', '\n\n'.join(prototypes + code)))


@pytest.mark.parametrize('state', STATES)
@pytest.mark.parametrize('command', COMMANDS)
def test_command(command, state):
    status, output = run(build('hs_deleted_objects', generated()), f'{command}:{state}')
    assert status == 0, output


@pytest.mark.parametrize('control', CONTROLS, ids=[control[0] for control in CONTROLS])
def test_negative_control(control):
    status, output = run(build('hs_deleted_objects', generated(control)), f'{control[3]}:deleted')
    assert status == CHECK_FAILED, output
