"""tools/picture_check.py's comparison, on small made-up pictures: a hole
(sky where the reference has a cliff) is counted and marked, a picture that
matches passes, and a change elsewhere counts as changed, not as a hole."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import picture_check  # noqa: E402

SKY = (90, 120, 200)
CLIFF = (150, 80, 40)
GROUND = (60, 110, 40)


def picture(path, rows):
    width, height = len(rows[0]), len(rows)
    data = b"".join(bytes(pixel) for row in rows for pixel in row)
    path.write_bytes(b"P6\n%d %d\n255\n" % (width, height) + data)
    return path


def scene(hole=False, changed_ground=False):
    rows = []
    for y in range(20):
        row = []
        for x in range(16):
            if y < 3:
                pixel = SKY
            elif y < 16:
                pixel = SKY if hole and 5 <= x < 9 and 9 <= y < 12 else CLIFF
            else:
                pixel = (200, 200, 200) if changed_ground and x < 8 else GROUND
            row.append(pixel)
        rows.append(row)
    return rows


def test_same_picture_passes(tmp_path):
    reference = picture(tmp_path / "reference.ppm", scene())
    test = picture(tmp_path / "test.ppm", scene())
    changed, holes, mask = picture_check.compare(test, reference, (8, 0), 18, tmp_path)
    assert changed == 0 and holes == 0 and mask is None


def test_sky_through_the_cliff_is_a_hole(tmp_path):
    reference = picture(tmp_path / "reference.ppm", scene())
    test = picture(tmp_path / "test.ppm", scene(hole=True))
    changed, holes, mask = picture_check.compare(test, reference, (8, 0), 18, tmp_path)
    assert holes == 12 and abs(changed - 12 / 320) < 1e-9
    # (the mask marks the hole's pixels red)
    assert mask.count(b"\xff\x00\x00") >= 12


def test_other_change_is_not_a_hole(tmp_path):
    reference = picture(tmp_path / "reference.ppm", scene())
    test = picture(tmp_path / "test.ppm", scene(changed_ground=True))
    changed, holes, mask = picture_check.compare(test, reference, (8, 0), 18, tmp_path)
    assert holes == 0 and mask is None and abs(changed - 32 / 320) < 1e-9


def test_pose_camera_file():
    pose = picture_check.Pose("bloodgulch red 1 2 3 90 0")
    lines = pose.camera_text().splitlines()
    assert lines[0] == "1.000000 2.000000 3.000000"
    forward = [float(value) for value in lines[1].split()]
    assert abs(forward[0]) < 1e-6 and abs(forward[1] - 1) < 1e-6
    assert pose.key() == "bloodgulch_red" and pose.sky == (picture_check.WIDTH // 2, 8)


def test_the_pose_list_reads():
    poses = picture_check.read_poses(picture_check.POSES, None)
    assert {"bloodgulch", "timberland@ce"} <= {pose.map for pose in poses}
    assert len({pose.key() for pose in poses}) == len(poses)


def test_skyline_a_pixel_off_is_no_hole(tmp_path):
    reference = picture(tmp_path / "reference.ppm", scene())
    # (the cliff's top row gone: the skyline one pixel lower)
    rows = scene()
    rows[3] = [SKY] * 16
    test = picture(tmp_path / "test.ppm", rows)
    changed, holes, mask = picture_check.compare(test, reference, (8, 0), 18, tmp_path)
    assert holes == 0 and changed > 0
