"""Validate sampled Team Slayer bot smoke-test evidence (not every-tick AI).

Usage: python3 tools/check_bot_team_slayer_log.py <network-test.log>
The host must run team_slayer with one local player and three in-game bots.
"""

import argparse
import re
from collections import Counter


def check_log(path):
    teams = {}
    bot_players = set()
    targets = []
    last_tick = 0
    kills = 0
    friendly_kills = 0
    variant_seen = False
    player_pattern = re.compile(r"player (\d+): (.*?)(?= player \d+:| \||$)")
    score_pattern = re.compile(r" s(-?\d+) k(\d+) d(\d+) f(\d+) t(-?\d+) m(\d+)")
    with open(path, encoding="utf-8", errors="replace") as log:
        for line in log:
            variant_seen |= "network test: game 1, team_slayer" in line
            tick = re.search(r"network test: tick (\d+)", line)
            if tick:
                last_tick = max(last_tick, int(tick[1]))
                for player in player_pattern.finditer(line):
                    score = score_pattern.search(player[2])
                    if not score:
                        continue
                    index = int(player[1])
                    team = int(score[5])
                    if index in teams and teams[index] != team:
                        raise ValueError("Team changes require a different test fixture")
                    teams[index] = team
                    if index != 0:
                        bot_players.add(index)
                        kills = max(kills, int(score[2]))
                        friendly_kills = max(friendly_kills, int(score[4]))
            target = re.search(r"bots: bot (\d+) .* target (-?\d+)", line)
            if target and int(target[2]) >= 0:
                targets.append((int(target[1]), int(target[2])))
            if "Assertion failed" in line or "assertion failed" in line:
                raise ValueError("Assertion failure in match log")
    if not variant_seen or last_tick < 900:
        raise ValueError("Expected at least 30 game seconds of Team Slayer")
    if bot_players != {1, 2, 3} or Counter(teams.values()) != {0: 2, 1: 2}:
        raise ValueError(f"Expected balanced 2-vs-2 teams, got {teams}")
    if not targets or any(teams[bot] == teams[target] for bot, target in targets):
        raise ValueError("Missing target samples or teammate targeting detected")
    if kills < 1 or friendly_kills != 0:
        raise ValueError("Expected a bot kill without a friendly-fire kill")
    print(f"Team Slayer passed: {last_tick} ticks, teams {teams}, "
          f"{len(targets)} enemy-only target samples, bot kill, no friendly-fire kills")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log")
    args = parser.parse_args()
    try:
        check_log(args.log)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Team Slayer failed: {error}\n")


if __name__ == "__main__":
    main()
