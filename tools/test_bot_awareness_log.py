"""Asset-free tests for the item-claim log checker; no pytest required."""
import runpy
import unittest
from pathlib import Path

check_claims = runpy.run_path(
    str(Path(__file__).with_name("check_bot_awareness_log.py"))
)["check_claims"]


def assigned(bot: int, team: int) -> str:
    return f"chupathingyce: bots: bot {bot} assigned team {team} lane 0 squad 0\n"


def seek(bot: int, item: int, kind: str = "weapon", team: int | None = None) -> str:
    team_tag = f" team {team}" if team is not None else ""
    return f"chupathingyce: bots: bot {bot}{team_tag} seeking {kind} {item}\n"


def give_up(bot: int, item: int, kind: str = "weapon", team: int | None = None) -> str:
    team_tag = f" team {team}" if team is not None else ""
    return f"chupathingyce: bots: bot {bot}{team_tag} gave up {kind} {item} (taken or gone)\n"


class ClaimLogTests(unittest.TestCase):
    def test_two_bots_on_one_item_fails(self):
        with self.assertRaisesRegex(SystemExit, "still claimed by bot 1"):
            check_claims(seek(1, -5) + seek(2, -5))

    def test_handoff_after_give_up_passes(self):
        self.assertEqual(check_claims(seek(1, -5) + give_up(1, -5) + seek(2, -5)), (2, 1))

    def test_opposing_teams_can_claim_the_same_item(self):
        text = seek(1, -5, team=0) + seek(2, -5, team=1)
        self.assertEqual(check_claims(text), (2, 1))

    def test_ffa_assignment_uses_the_shared_scope(self):
        text = assigned(1, -1) + seek(1, -5) + seek(2, -5)
        with self.assertRaisesRegex(SystemExit, "still claimed by bot 1"):
            check_claims(text)

    def test_scavenge_to_fight_releases_for_teammate(self):
        text = seek(1, -5, team=0)
        text += "bots: bot 1 tick 8 scavenge -> fight (target seen)\n"
        text += give_up(1, -5, "weapon", team=0)
        text += seek(2, -5, team=0)
        self.assertEqual(check_claims(text), (2, 1))

    def test_behavior_change_without_release_keeps_claim(self):
        text = seek(1, -5, team=0)
        text += "bots: bot 1 tick 8 scavenge -> fight (target seen)\n"
        with self.assertRaisesRegex(SystemExit, "still claimed by bot 1"):
            check_claims(text + seek(2, -5, team=0))

    def test_opponent_release_does_not_free_teammates_claim(self):
        text = seek(1, -5, team=0) + seek(2, -5, team=1)
        text += give_up(2, -5, team=1)
        with self.assertRaisesRegex(SystemExit, "still claimed by bot 1"):
            check_claims(text + seek(3, -5, team=0))

    def test_handoff_after_death_release_passes(self):
        text = seek(1, -5, "powerup", 0)
        text += "bots: bot 1 team 0 cleared item claims (new life)\n"
        self.assertEqual(check_claims(text + seek(2, -5, "powerup", 0)), (2, 1))

    def test_legacy_release_log_still_passes(self):
        text = seek(1, -5) + "bots: bot 1 released its item claims (left match)\n"
        self.assertEqual(check_claims(text + seek(2, -5)), (2, 1))

    def test_another_bots_give_up_does_not_release(self):
        with self.assertRaises(SystemExit):
            check_claims(seek(1, -5) + give_up(2, -5) + seek(2, -5))

    def test_same_bot_reseeking_passes(self):
        self.assertEqual(check_claims(seek(1, -5) + seek(1, -5) + seek(2, -6)), (3, 2))


if __name__ == "__main__":
    unittest.main()
