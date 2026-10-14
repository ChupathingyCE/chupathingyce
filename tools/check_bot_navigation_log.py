#!/usr/bin/env python3
"""Check a bot-enabled Blood Gulch run patrols distinct BSP lanes."""

import argparse
import math
import re
from pathlib import Path
from typing import TypeVar

LANE_RE = re.compile(
    r"bots: bot (\d+) assigned team (\d+) lane (\d+) squad (\d+) side (-?\d+) phase (\d+)"
)
LANE_GOAL_RE = re.compile(
    r"bots: bot (\d+) lane target team (\d+) lane (\d+) phase (\d+).*?"
    r"goal \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)"
)
ROUTE_RE = re.compile(
    r"bots: bot (\d+) team (\d+) lane (\d+) squad (\d+) surface (patrol|tactical) "
    r"route goal \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) waypoints (\d+)"
)
STATUS_RE = re.compile(
    r"bots: bot (\d+) at \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) "
    r"yaw -?[\d.]+ (\w+) \(conf"
)
ARRIVED_RE = re.compile(
    r"bots: bot (\d+) team (\d+) lane (-?\d+) surface (patrol|tactical) arrived goal"
)
ROUTE_END_RES = (
    ("stuck", re.compile(r"bots: bot (\d+) surface route blocked at")),
    ("blocked-ahead", re.compile(r"bots: bot (\d+) surface route crosses blocked")),
    ("new-life", re.compile(r"bots: bot (\d+) team -?\d+ cleared item claims \(new life\)")),
    ("behavior", re.compile(r"bots: bot (\d+) tick \d+ \w+ -> ")),
)
LIFE_RE = re.compile(r"bots: bot (\d+) team -?\d+ cleared item claims \(new life\)")
CRASH_RE = re.compile(r"FAILURE|assertion failed|Assertion failed|AddressSanitizer|runtime error:")
STATIONARY_DISTANCE = 0.5
STATIONARY_SAMPLES = 3


Number = TypeVar("Number", int, float)


def parse_number(value: str, kind: type[Number]) -> Number:
    try:
        return kind(value)
    except ValueError as error:
        raise SystemExit(f"invalid number in bot navigation log: {value!r}") from error


def check_movement(text: str) -> None:
    if CRASH_RE.search(text):
        raise SystemExit("crash/assertion/sanitizer failure in bot navigation log")
    stationary: dict[int, tuple[tuple[float, float, float], int]] = {}
    for line in text.splitlines():
        life = LIFE_RE.search(line)
        if life:
            stationary.pop(parse_number(life.group(1), int), None)
            continue
        match = STATUS_RE.search(line)
        if not match:
            continue
        bot = parse_number(match.group(1), int)
        position: tuple[float, float, float] = (
            parse_number(match.group(2), float), parse_number(match.group(3), float),
            parse_number(match.group(4), float)
        )
        behavior = match.group(5)
        if behavior == "vehicle":
            stationary.pop(bot, None)
            continue
        previous = stationary.get(bot)
        # Jumping against a wall is not horizontal walking progress.
        if previous and math.dist(previous[0][:2], position[:2]) < STATIONARY_DISTANCE:
            stationary[bot] = (previous[0], previous[1] + 1)
            if stationary[bot][1] >= STATIONARY_SAMPLES:
                raise SystemExit(
                    f"bot {bot} stayed within {STATIONARY_DISTANCE:.1f} m for "
                    f"{STATIONARY_SAMPLES} consecutive same-life status samples horizontally"
                )
        else:
            stationary[bot] = (position, 1)


def check_movement_coverage(text: str, minimum_bots: int) -> int:
    check_movement(text)
    samples: dict[int, int] = {}
    for match in STATUS_RE.finditer(text):
        bot = parse_number(match.group(1), int)
        samples[bot] = samples.get(bot, 0) + 1
    covered = sum(count >= STATIONARY_SAMPLES for count in samples.values())
    if covered < minimum_bots:
        raise SystemExit(f"insufficient bot status samples for movement coverage: {covered}/{minimum_bots}")
    return covered


def route_outcomes(text: str) -> dict[str, int]:
    """What ended each route found: arrival, a stuck replan, a blockage ahead,
    death, a behavior change, a new route (replan), or the end of the log."""
    open_routes: set[int] = set()
    outcomes: dict[str, int] = {}

    def end(kind: str, bot: int) -> None:
        open_routes.discard(bot)
        outcomes[kind] = outcomes.get(kind, 0) + 1

    for line in text.splitlines():
        match = ROUTE_RE.search(line)
        if match:
            bot = parse_number(match.group(1), int)
            if bot in open_routes:
                end("replanned", bot)
            open_routes.add(bot)
            continue
        match = ARRIVED_RE.search(line)
        if match and parse_number(match.group(1), int) in open_routes:
            end("arrived", parse_number(match.group(1), int))
            continue
        for kind, pattern in ROUTE_END_RES:
            match = pattern.search(line)
            if match and parse_number(match.group(1), int) in open_routes:
                end(kind, parse_number(match.group(1), int))
                break
    outcomes["unfinished"] = len(open_routes)
    return outcomes


def check_lane_arrivals(text: str, teams: set[int]) -> None:
    arrived = {(parse_number(team, int), parse_number(lane, int))
               for _, team, lane, kind in ARRIVED_RE.findall(text) if kind == "patrol"}
    for team in sorted(teams):
        lanes = sorted(lane for arrived_team, lane in arrived if arrived_team == team)
        if set(lanes) != {0, 1, 2}:
            raise SystemExit(f"team {team} completed patrols in lanes {lanes}, expected 0, 1, 2")


def check_log(path: Path, minimum_bots: int, require_lane_arrivals: bool = False) -> None:
    text = path.read_text(errors="replace")
    check_movement_coverage(text, minimum_bots)
    assignments = [
        tuple(parse_number(value, int) for value in match)
        for match in LANE_RE.findall(text)
    ]
    lane_goals = [
        tuple(parse_number(value, int if index < 4 else float)
              for index, value in enumerate(match))
        for match in LANE_GOAL_RE.findall(text)
    ]
    routes = [
        tuple(
            parse_number(value, int if index in (0, 1, 2, 3, 8) else float)
            if index != 4 else value
            for index, value in enumerate(match)
        )
        for match in ROUTE_RE.findall(text)
    ]

    assigned_bots = {assignment[0] for assignment in assignments}
    if len(assigned_bots) < minimum_bots:
        raise SystemExit(
            f"expected at least {minimum_bots} distinct assigned bots, found {len(assigned_bots)}"
        )
    teams = {team for _, team, *_ in assignments}
    if len(teams) < 2:
        raise SystemExit(f"expected both teams to receive lanes, found teams {sorted(teams)}")
    for team in teams:
        lanes = {lane for _, assigned_team, lane, *_ in assignments if assigned_team == team}
        if lanes != {0, 1, 2}:
            raise SystemExit(f"team {team} uses lanes {sorted(lanes)}, expected 0, 1, 2")

        team_goals = [goal for goal in lane_goals if goal[1] == team]
        goal_lanes = {goal[2] for goal in team_goals}
        if goal_lanes != {0, 1, 2}:
            raise SystemExit(
                f"team {team} has patrol goals in lanes {sorted(goal_lanes)}, expected 0, 1, 2"
            )
        points = [(goal[4], goal[5], goal[6]) for goal in team_goals]
        spread = max(
            math.dist(a, b)
            for index, a in enumerate(points)
            for b in points[index + 1 :]
        ) if len(points) > 1 else 0.0
        if spread < 10.0:
            raise SystemExit(f"team {team} patrol goals are not spread out ({spread:.1f} m)")

        patrol_routes = [route for route in routes if route[1] == team and route[4] == "patrol"]
        route_lanes = {route[2] for route in patrol_routes}
        if len(patrol_routes) < 3 or len(route_lanes) < 2:
            raise SystemExit(
                f"team {team} has only {len(patrol_routes)} patrol routes in lanes "
                f"{sorted(route_lanes)}"
            )
        if any(route[8] < 2 for route in patrol_routes):
            raise SystemExit(f"team {team} has a patrol route with fewer than two waypoints")

    patrol_count = sum(route[4] == "patrol" for route in routes)
    if require_lane_arrivals:
        check_lane_arrivals(text, teams)
    outcomes = route_outcomes(text)
    print("route outcomes: " + ", ".join(f"{kind} {count}" for kind, count in sorted(outcomes.items())))
    print(
        f"passed: {len(assignments)} lane assignments, {len(lane_goals)} lane goals, "
        f"{patrol_count} patrol routes, teams {sorted(teams)}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--minimum-bots", type=int, default=24)
    parser.add_argument("--require-lane-arrivals", action="store_true",
                        help="each team must complete a patrol in every lane (debug.bot_navigation_only runs)")
    parser.add_argument("--movement-only", action="store_true",
                        help="check sampled movement without requiring BSP lane coverage")
    args = parser.parse_args()
    if args.movement_only:
        text = args.log.read_text(errors="replace")
        covered = check_movement_coverage(text, args.minimum_bots)
        print(f"passed: sampled horizontal movement for {covered} bots (vehicle seats exempt)")
    else:
        check_log(args.log, args.minimum_bots, args.require_lane_arrivals)


if __name__ == "__main__":
    main()
