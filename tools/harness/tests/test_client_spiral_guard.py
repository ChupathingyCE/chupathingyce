"""The client's spiral guard (source/game/game_time.c): a client that can't keep up runs a few ticks a frame
instead of a second's; every frame that keeps up, and every host, local game and film, ticks as before."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

GUARD_CALL = ('\t\t\tif (game_connection() == _game_connection_network_client && !game_time_held())\n'
              '\t\t\t\tconnection = game_time_client_guard_cap(ticks_elapsed, connection,\n'
              '\t\t\t\t\tticks_per_second/TICKS_PER_SECOND);\n')
CONTROLS = {
    # no guard: the spiral runs a second's ticks a frame
    'no-guard': (GUARD_CALL, '', 'spiral-capped'),
    # the guard on from the first late frame: a single hitch is capped too
    'first-late-frame': ('guard->previous_due > limit && ticks_due > guard->previous_run && ticks_due > limit',
                         'ticks_due > limit', 'client:heavy:16:150'),
    # the few not grown with the game's speed: a fast game that keeps up is held to them
    'unscaled': ('MAX(speed, 1.0f)', '1.0f', 'client:desktop:30:0:6'),
    # never off again: a client that fell behind once stays at a few
    'never-off': ('\t\tguard->engaged = FALSE;\n', '\t\t;\n', 'client:burst:16:150'),
    # the guard while the clock waits for the host: a slow load reads as falling behind
    'while-held': (' && !game_time_held())\n\t\t\t\tconnection = game_time_client_guard_cap',
                   ')\n\t\t\t\tconnection = game_time_client_guard_cap', 'held-load'),
    # the host's clock never taken sooner
    'catch-up-second': ('game_time_client_guard.ticks_left <= 0', '1', 'spiral-capped'),
}

# frames that keep up: the same ticks with the guard as without
KEEPING_UP = [
    'client:quiet:16:0',      # an Xbox at 16 players
    'client:quiet:16:150',    # ... with a second's hitch every 5 s or so
    'client:heavy:16:0',      # a heavy game, ticks of 18.7 to 27.6 ms
    'client:heavy:16:150',
    'client:heavy:16:40',     # a hitch every 40 frames
    'client:fight:16:0',      # ticks of 30 ms: 5 a frame, slow but keeping up
    'client:desktop:4:0',     # a desktop client drawing 200 frames a second
    'client:desktop:4:40',
    'client:desktop:30:0:6',  # an MCC map at game speed 6, 30 frames a second: 6-7 ticks a frame
    'client:desktop:30:150:6',
    'client:desktop:4:0:6',
]
# never a client: the same ticks even when the machine can't keep up
NOT_CLIENTS = [f'{who}:{model}:16:{hitch}' for who in ('host', 'local', 'film')
               for model in ('heavy', 'spiral') for hitch in (0, 150)]


def generated(control=None):
    source = read('source/game/game_time.c')
    connections = enum_with(read('source/game/game.h'), '_game_connection_local')
    code = '\n'.join([
        'enum { SOME_LARGE_NUMBER_OF_TICKS = %d };' % constant(source, 'SOME_LARGE_NUMBER_OF_TICKS'),
        'enum { GAME_TIME_CLIENT_GUARD_TICKS = %d };' % constant(source, 'GAME_TIME_CLIENT_GUARD_TICKS'),
        structure(source, 'game_time_globals_struct'),
        'static struct game_time_globals_struct *game_time_globals;',
        'static boolean game_time_ticks_waiting = FALSE;',
        structure(source, 'game_time_client_guard_state'),
        'static struct game_time_client_guard_state game_time_client_guard;',
    ] + [function(source, name) for name in (
        'game_time_initialize_for_new_map', 'game_time_set_distributed', 'game_time_held', 'game_time_get_speed',
        'game_time_client_guard_cap', 'game_time_client_catch_up_ticks', 'game_time_update')])
    if control:
        before, after = CONTROLS[control][:2]
        code = mutated(code, before, after)
    return (('connections.inc', connections), ('under_test.inc', code))


def ticks(output):
    return output.splitlines()[0]


@pytest.mark.parametrize('case', KEEPING_UP + NOT_CLIENTS)
def test_same_ticks_as_without_the_guard(case):
    status, guarded = run(build('client_spiral_guard', generated()), case)
    assert status == 0, guarded
    status, unguarded = run(build('client_spiral_guard', generated('no-guard')), case)
    assert status == 0, unguarded
    assert ticks(guarded) == ticks(unguarded), case
    assert 'logged 0 ' in guarded, guarded.splitlines()[1]


def test_spiral_capped():
    status, output = run(build('client_spiral_guard', generated()), 'spiral-capped')
    assert status == 0, output


def test_held_load():
    status, output = run(build('client_spiral_guard', generated()), 'held-load')
    assert status == 0, output


def test_spiral_trace_differs():
    """(a client in the spiral: the guard does change its ticks, at most its few a frame once on)"""
    status, guarded = run(build('client_spiral_guard', generated()), 'client:spiral:16:0')
    assert status == 0, guarded
    status, unguarded = run(build('client_spiral_guard', generated('no-guard')), 'client:spiral:16:0')
    guard = constant(read('source/game/game_time.c'), 'GAME_TIME_CLIENT_GUARD_TICKS')
    assert ticks(guarded) != ticks(unguarded)
    assert max(int(n) for n in ticks(guarded).split()[100:]) <= guard
    assert max(int(n) for n in ticks(unguarded).split()[100:]) == 30


@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
    case = CONTROLS[control][2]
    executable = build('client_spiral_guard', generated(control))
    status, output = run(executable, case)
    if case in ('spiral-capped', 'held-load'):
        assert status == CHECK_FAILED, f'{control} was not caught: {output}'
    else:
        status, guarded = run(build('client_spiral_guard', generated()), case)
        assert ticks(output) != ticks(guarded), f'{control} was not caught'
