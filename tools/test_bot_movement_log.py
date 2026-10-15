"""Asset-free regression tests for sampled bot movement; no pytest required."""
import runpy
import unittest
from pathlib import Path

checker = runpy.run_path(str(Path(__file__).with_name("check_bot_navigation_log.py")))
check_movement = checker["check_movement"]
check_movement_coverage = checker["check_movement_coverage"]
route_outcomes = checker["route_outcomes"]
check_lane_arrivals = checker["check_lane_arrivals"]


def sample(x: float, behavior: str = "fight") -> str:
    return f"bots: bot 17 at ({x:.1f} 0.0 0.0) yaw 0 {behavior} (conf 0.72)\n"


def route(bot: int, team: int = 0, lane: int = 0) -> str:
    return (f"bots: bot {bot} team {team} lane {lane} squad 0 surface patrol route goal "
            f"(1.0 2.0 3.0) waypoints 9 search 3 ticks\n")


def arrived(bot: int, team: int = 0, lane: int = 0) -> str:
    return (f"bots: bot {bot} team {team} lane {lane} surface patrol arrived goal (1.0 2.0 3.0) "
            f"at (1.1 2.0 3.0) error 0.10 waypoints 9\n")


class RouteOutcomeTests(unittest.TestCase):
    def test_every_route_is_accounted_for(self):
        text = (route(1) + arrived(1) + route(2) +
                "bots: bot 2 surface route blocked at polygon 7 (standing on 6, waypoint deadline); replanning\n" +
                route(3) + route(3) + "bots: bot 3 team 0 cleared item claims (new life)\n" + route(4))
        self.assertEqual(route_outcomes(text),
                         {"arrived": 1, "stuck": 1, "replanned": 1, "new-life": 1, "unfinished": 1})

    def test_an_arrival_without_a_route_is_not_counted(self):
        self.assertEqual(route_outcomes(arrived(5)), {"unfinished": 0})

    def test_lane_arrivals_need_every_lane_of_every_team(self):
        text = "".join(arrived(1, team, lane) for team in (0, 1) for lane in (0, 1, 2))
        check_lane_arrivals(text, {0, 1})
        with self.assertRaisesRegex(SystemExit, "team 1 completed patrols in lanes \\[0, 1\\]"):
            check_lane_arrivals(text.replace("team 1 lane 2", "team 1 lane 1"), {0, 1})


class MovementLogTests(unittest.TestCase):
    def test_stationary_fighting_bot_fails(self):
        with self.assertRaisesRegex(SystemExit, "bot 17 stayed"):
            check_movement(sample(1) * 3)

    def test_slow_drift_does_not_hide_stall(self):
        with self.assertRaises(SystemExit):
            check_movement(sample(1) + sample(1.1) + sample(1.2))

    def test_moving_bot_passes(self):
        check_movement(sample(1) + sample(2) + sample(3))

    def test_vertical_jumping_does_not_hide_horizontal_stall(self):
        text = sample(1) + sample(1).replace("1.0 0.0 0.0", "1.0 0.0 1.0") + sample(1)
        with self.assertRaisesRegex(SystemExit, "bot 17 stayed"):
            check_movement(text)

    def test_respawn_resets_stationary_window(self):
        text = sample(1) * 2 + "bots: bot 17 team 0 cleared item claims (new life)\n" + sample(1) * 2
        check_movement(text)

    def test_crash_cannot_pass_after_movement_samples(self):
        with self.assertRaisesRegex(SystemExit, "crash"):
            check_movement(sample(1) + sample(2) + sample(3) + "Assertion failed: player unit\n")

    def test_insufficient_samples_are_not_a_pass(self):
        with self.assertRaisesRegex(SystemExit, "insufficient"):
            check_movement_coverage(sample(1) + sample(2), 1)

    def test_many_samples_of_one_bot_do_not_cover_two_bots(self):
        with self.assertRaisesRegex(SystemExit, "1/2"):
            check_movement_coverage(sample(1) + sample(2) + sample(3), 2)

    def test_malformed_number_is_a_clear_failure(self):
        with self.assertRaisesRegex(SystemExit, "invalid number"):
            check_movement(sample(1).replace("1.0 0.0 0.0", "1..0 0.0 0.0"))

    def test_vehicle_resets_stationary_window(self):
        check_movement(sample(1) * 2 + sample(1, "vehicle") * 4 + sample(1) * 2)


if __name__ == "__main__":
    unittest.main()
