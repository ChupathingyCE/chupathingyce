"""tools/renderer_gate.py's parts that need no game: the view list, the
picture comparison on made-up pictures, and the frame-time rules."""
import sys
from argparse import Namespace
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import renderer_gate  # noqa: E402

numpy = pytest.importorskip("numpy")
Image = pytest.importorskip("PIL.Image")

LIMITS = Namespace(min_ssim=renderer_gate.MIN_SSIM, max_changed=renderer_gate.MAX_CHANGED,
                   max_brightness=renderer_gate.MAX_BRIGHTNESS, max_tile=renderer_gate.MAX_TILE)


def picture(path, pixels):
    Image.fromarray(pixels.clip(0, 255).astype(numpy.uint8)).save(path)
    return path


def scene():
    """a sky, a cliff with some texture, and the ground"""
    generator = numpy.random.default_rng(1)
    pixels = numpy.zeros((120, 160, 3))
    pixels[:40] = (90, 120, 200)
    pixels[40:100] = (150, 80, 40)
    pixels[40:100] += generator.integers(-20, 20, size=(60, 160, 1))
    pixels[100:] = (60, 110, 40)
    return pixels


def test_the_shipped_view_list_reads():
    views, ticks = renderer_gate.read_views(renderer_gate.VIEWS, None)
    assert views
    assert {view.kind for view in views} <= {"player", "spawn", "camera"}
    assert len({(view.map, view.name) for view in views}) == len(views)
    for game_map in ticks:
        assert any(view.map == game_map for view in views), game_map


def test_a_bad_view_line_is_refused(tmp_path):
    path = tmp_path / "views.txt"
    path.write_text("bloodgulch odd camera 1 2 3\n")
    with pytest.raises(ValueError):
        renderer_gate.read_views(path, None)


def test_the_same_picture_matches(tmp_path):
    first = picture(tmp_path / "a.png", scene())
    second = picture(tmp_path / "b.png", scene())
    numbers, _ = renderer_gate.compare(first, second, renderer_gate.PIXEL_DELTA)
    assert renderer_gate.verdict_of(numbers, LIMITS) == ("match", [])
    assert numbers["ssim"] > 0.999


def test_a_missing_piece_differs(tmp_path):
    changed = scene()
    changed[50:90, 40:100] = (90, 120, 200)
    first = picture(tmp_path / "a.png", scene())
    second = picture(tmp_path / "b.png", changed)
    numbers, change = renderer_gate.compare(first, second, renderer_gate.PIXEL_DELTA)
    verdict, reasons = renderer_gate.verdict_of(numbers, LIMITS)
    assert verdict == "DIFFERS"
    assert any("changed" in reason for reason in reasons)
    renderer_gate.difference_picture(first, change, tmp_path / "diff.png", renderer_gate.PIXEL_DELTA)
    assert (tmp_path / "diff.png").exists()


def test_a_darker_picture_differs_on_brightness(tmp_path):
    first = picture(tmp_path / "a.png", scene())
    second = picture(tmp_path / "b.png", scene() - 6)
    numbers, _ = renderer_gate.compare(first, second, renderer_gate.PIXEL_DELTA)
    verdict, reasons = renderer_gate.verdict_of(numbers, LIMITS)
    assert verdict == "DIFFERS"
    assert any("brightness" in reason for reason in reasons)


def test_edge_noise_alone_still_matches(tmp_path):
    # (a pixel's worth of anti-aliasing along an edge)
    changed = scene()
    changed[40, ::2] = (120, 100, 120)
    first = picture(tmp_path / "a.png", scene())
    second = picture(tmp_path / "b.png", changed)
    numbers, _ = renderer_gate.compare(first, second, renderer_gate.PIXEL_DELTA)
    assert renderer_gate.verdict_of(numbers, LIMITS)[0] == "match"


def row(mean, p99, low, fell_back=False):
    return {"mean_ms": mean, "p99_ms": p99, "low_1pct_fps": low, "cpu_ms": 1.0, "fell_back_to_gl": fell_back}


def test_faster_on_every_count_passes():
    passed, _ = renderer_gate.bench_verdict({"bloodgulch": {"gl": row(4.8, 9.5, 86), "bgfx": row(1.7, 3.8, 228)}},
                                            "gl", "bgfx", 0.05)
    assert passed


def test_a_worse_p99_or_a_fallback_fails():
    passed, reasons = renderer_gate.bench_verdict(
        {"bloodgulch": {"gl": row(4.8, 9.5, 86), "bgfx": row(3.0, 12.0, 90)}}, "gl", "bgfx", 0.05)
    assert not passed and "p99 worse" in reasons[0]
    passed, reasons = renderer_gate.bench_verdict(
        {"bloodgulch": {"gl": row(4.8, 9.5, 86), "bgfx": row(1.7, 3.8, 228, fell_back=True)}}, "gl", "bgfx", 0.05)
    assert not passed


def test_about_the_same_speed_fails():
    passed, reasons = renderer_gate.bench_verdict({"damnation": {"gl": row(4.0, 8.0, 100), "bgfx": row(3.9, 7.5, 110)}},
                                                  "gl", "bgfx", 0.05)
    assert not passed and "not 5% better" in reasons[0]


def test_a_small_part_drawn_differently_differs(tmp_path):
    # (a HUD meter's corner: too small for the whole picture's share)
    changed = scene()
    changed[2:12, 140:156] = (250, 250, 250)
    first = picture(tmp_path / "a.png", scene())
    second = picture(tmp_path / "b.png", changed)
    numbers, _ = renderer_gate.compare(first, second, renderer_gate.PIXEL_DELTA)
    verdict, reasons = renderer_gate.verdict_of(numbers, LIMITS)
    assert numbers["changed"] < renderer_gate.MAX_CHANGED
    assert verdict == "DIFFERS" and any("part" in reason for reason in reasons)


def test_ssim_of_a_large_picture_stays_at_most_one():
    """float32's summed-area table lost the low digits of a 1280x720
    picture's sums: the same picture came out past 1"""
    generator = numpy.random.default_rng(2)
    values = generator.uniform(0, 255, size=(720, 1280))
    value = renderer_gate.ssim(values, values.copy())
    assert abs(value - 1.0) < 1e-9


def test_a_busy_machine_is_not_idle():
    assert renderer_gate.idle({"before": 0.4, "after": 0.8, "cpus": 10})
    assert not renderer_gate.idle({"before": 0.4, "after": 1.2, "cpus": 10})
    assert not renderer_gate.idle({"before": 3.0, "after": 0.5, "cpus": 10})
    assert not renderer_gate.idle({"before": -1, "after": -1, "cpus": 10})
