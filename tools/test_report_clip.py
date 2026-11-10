"""A report's clip goes to the site with the token the server made
(REPORT_RESULT's), never one the reporter's copy makes, and the copy waits
for the report to reach the site (409) before it gives up
(port/linux/game/report_player.c, port/linux/src/browser.c)."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def _function(source, head):
    start = source.index(head)
    return source[start:source.index("\n}\n", start)]


def test_the_servers_token():
    report = (ROOT / "port/linux/game/report_player.c").read_text(encoding="utf-8")
    assert "random" not in report
    assert "browser_clip_upload(report.clip_path, result.clip_token)" in report
    assert "delta_peer_game_report((int)report.machine, (int)report.player, report.reason, report.clip)" in report
    game = (ROOT / "port/linux/src/delta_peer_game.c").read_text(encoding="utf-8")
    sent = _function(game, "unsigned int delta_peer_game_report(")
    assert "clip_token" not in sent and "report.flags = DELTA_WIRE_REPORT_CLIP;" in sent


def test_waits_for_the_report():
    browser = (ROOT / "port/linux/src/browser.c").read_text(encoding="utf-8")
    upload = _function(browser, "static void update_clip_upload(void)")
    assert "time(NULL) < clip_job.next_try" in upload
    assert "if (status == 409)" in upload
    waiting = upload[upload.index("if (status == 409)"):]
    assert waiting.index("now - clip_job.asked < CLIP_WAIT_SECONDS") < waiting.index("clip_done(BROWSER_CLIP_FAILED)")
    assert "CLIP_RETRY_SECONDS = 5," in browser and "CLIP_WAIT_SECONDS = 120," in browser
    # (the file is read only once the site takes the clip, not on every ask)
    assert upload.index("/v1/game/clip/start") < upload.index("malloc((size_t)length)")
    begin = _function(browser, "int browser_clip_upload(")
    assert "clip_job.next_try = clip_job.asked + CLIP_RETRY_SECONDS;" in begin
