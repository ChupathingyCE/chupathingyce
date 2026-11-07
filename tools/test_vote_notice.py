"""OpenCE votes on a Chupathingy host (network_distributed.c): every vote
message is still dropped unread; a client's own vote (78, 95) is logged by
kind once a game per machine and its player gets one chat line; message 92
is still dropped unread and unlogged."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DISTRIBUTED_C = ROOT / "port/linux/game/network_distributed.c"
CHAT_C = ROOT / "port/linux/game/chat.c"


def _body(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}\n", start)]


def test_votes_dropped_and_noted_once():
    text = DISTRIBUTED_C.read_text(encoding="utf-8")
    unknown = _body(text, "static void distributed_unknown_kind(")
    build = unknown.index("if (type == DISTRIBUTED_CLIENT_BUILD_KIND)\n\t\treturn;")
    votes = unknown.index("type == NETWORK_RULES_VOTE_REQUEST_KIND || type == NETWORK_RULES_VOTE_STATUS_KIND")
    assert build < votes, "message 92 stays dropped unread before anything else"
    vote_block = unknown[votes:unknown.index("\t\treturn;\n\t}", votes)]
    assert "machine_index != NONE" in vote_block
    assert "distributed_vote_dropped(machine_index, type);" in vote_block
    dropped = _body(text, "static void distributed_vote_dropped(")
    assert "distributed_vote_machines_told[machine_index >> 3] |=" in dropped
    assert "chat_host_notice_to_machine((short)machine_index, \"Voting isn't supported on this server; ask a moderator.\");" in dropped
    assert "csmemset(distributed_vote_machines_told, 0, sizeof(distributed_vote_machines_told));" in text


def test_notice_only_to_opence_machines():
    text = CHAT_C.read_text(encoding="utf-8")
    notice = text[text.index("int chat_host_notice_to_machine(short machine, char const *text)\n{"):]
    notice = notice[:notice.index("\n}\n")]
    assert "bridge_hosting()" in notice and "bridge_machine(machine)" in notice
    assert "DELTA_CHAT_BRIDGE_ALLOWED" in notice
