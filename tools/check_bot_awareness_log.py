#!/usr/bin/env python3
"""Check a bot match log: teammates do not pursue the same item.

Claims start at "seeking weapon|powerup N" and end at "gave up ... N" or
"cleared item claims". Opposing teams have independent claims and may both
pursue one item. Optionally require weapon/powerup pursuit and teleport memory.
"""

import argparse
import re
from pathlib import Path

TEAM_RE = re.compile(r"bots: bot (\d+) assigned team (-?\d+)")
SEEK_RE = re.compile(
    r"bots: bot (\d+)(?: team (-?\d+))? seeking (weapon|powerup) (-?\d+)"
)
GIVE_UP_RE = re.compile(
    r"bots: bot (\d+)(?: team (-?\d+))? gave up (weapon|powerup|vehicle) (-?\d+)"
)
RELEASE_RE = re.compile(
    r"bots: bot (\d+)(?: team (-?\d+))? (?:released its|cleared) item claims"
)
TELEPORT_RE = re.compile(r"bots: bot (\d+) saw enemy (\d+) teleport")


def _team_scope(bot: int, logged_team: str | None, bot_teams: dict[int, int]) -> int:
    team = int(logged_team) if logged_team is not None else bot_teams.get(bot, 0)
    return team if team >= 0 else 0


def check_claims(text: str) -> tuple[int, int]:
    bot_teams: dict[int, int] = {}
    holders: dict[tuple[int, int], int] = {}
    pursuits = 0
    items: set[int] = set()
    for number, line in enumerate(text.splitlines(), 1):
        if match := TEAM_RE.search(line):
            bot, team = int(match.group(1)), int(match.group(2))
            bot_teams[bot] = team if team >= 0 else 0
        elif match := SEEK_RE.search(line):
            bot, item = int(match.group(1)), int(match.group(4))
            team = _team_scope(bot, match.group(2), bot_teams)
            key = (team, item)
            holder = holders.get(key)
            if holder is not None and holder != bot:
                raise SystemExit(
                    f"line {number}: bot {bot} (team {team}) sought item {item} "
                    f"still claimed by bot {holder} on team {team}"
                )
            holders[key] = bot
            pursuits += 1
            items.add(item)
        elif match := GIVE_UP_RE.search(line):
            bot, item = int(match.group(1)), int(match.group(4))
            key = (_team_scope(bot, match.group(2), bot_teams), item)
            if holders.get(key) == bot:
                del holders[key]
        elif match := RELEASE_RE.search(line):
            bot = int(match.group(1))
            team = _team_scope(bot, match.group(2), bot_teams)
            for key, holder in list(holders.items()):
                if key[0] == team and holder == bot:
                    del holders[key]
    return pursuits, len(items)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--require-pursuit", action="store_true",
                        help="fail unless at least one weapon or powerup was sought")
    parser.add_argument("--require-teleport", action="store_true",
                        help="fail unless a bot saw an enemy teleport")
    args = parser.parse_args()
    text = args.log.read_text(errors="replace")
    pursuits, items = check_claims(text)
    teleports = len(TELEPORT_RE.findall(text))
    if args.require_pursuit and pursuits == 0:
        raise SystemExit("no bot sought a weapon or powerup")
    if args.require_teleport and teleports == 0:
        raise SystemExit("no bot saw an enemy teleport")
    print(f"passed: {pursuits} exclusive item pursuits of {items} items, "
          f"{teleports} teleport observations")


if __name__ == "__main__":
    main()
