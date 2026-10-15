"""Check gunner or weapon-acquisition evidence in a controlled bot test log.

This validates observed events, not driving quality or turret accuracy.
"""

import argparse
import re


def check_log(path, mode):
    fixture = None
    seeking = set()
    inventory = {}
    acquired = False
    entered = False
    with open(path, encoding="utf-8", errors="replace") as log:
        for line in log:
            if "EXCEPTION halt" in line or "Assertion failed" in line:
                raise ValueError("Game assertion in test log")
            match = re.search(r"sandbox host drives vehicle (-?\d+); Bot 2 approaches gunner seat (\d+)", line)
            if match:
                fixture = (int(match[1]), int(match[2]))
            match = re.search(r"bot (\d+) seeking vehicle (-?\d+)", line)
            if match:
                seeking.add((int(match[1]), int(match[2])))
            match = re.search(r"bot 2 entered vehicle (-?\d+) seat (\d+)", line)
            if match:
                seat = (int(match[1]), int(match[2]))
                entered |= fixture == seat and (2, seat[0]) in seeking
            match = re.search(r"bot (\d+) inventory slot (\d+) now weapon ([0-9a-f]+)", line)
            if match:
                bot, slot, weapon = int(match[1]), int(match[2]), int(match[3], 16)
                previous = inventory.get((bot, slot))
                if slot > 0 or (previous is not None and previous != weapon):
                    acquired = True
                inventory[(bot, slot)] = weapon
    if mode == "gunner" and not entered:
        raise ValueError("Missing matching sandbox, vehicle-seeking and gunner-entry evidence")
    if mode == "weapon" and not acquired:
        raise ValueError("No additional or replacement weapon observed")
    print(f"Bot {mode} smoke test passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("gunner", "weapon"))
    parser.add_argument("log")
    args = parser.parse_args()
    try:
        check_log(args.log, args.mode)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Bot opportunity test failed: {error}\n")


if __name__ == "__main__":
    main()
