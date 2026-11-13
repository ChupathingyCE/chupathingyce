"""tools/console_gl_check.py: it reads the renderer's desktop GL list from
gl.h and fails a console table that lacks one (the Xbox 360 and Wii U run it
in their CI against their own GL layers)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import console_gl_check as gl_check  # noqa: E402


def lists():
    required = gl_check.desktop_functions(gl_check.GL_H.read_text())
    optional = gl_check.optional_functions(gl_check.GL_FUNCTIONS_C.read_text())
    return required, optional


def table(names):
    return "#define ENTRY(name) { #name, (void *)x360_##name },\n" + "\n".join(f"\tENTRY({name})" for name in names)


def test_reads_the_desktop_list():
    required, optional = lists()
    assert len(required) > 80
    assert "glDeleteRenderbuffers" in required and "glDrawElementsBaseVertex" in required
    # (the ES list's own extension names belong to Android, not the consoles)
    assert "glDrawElementsBaseVertexOES" not in required
    assert {"glClipControl", "glCopyImageSubData", "glBufferStorage"} <= optional


def test_full_table_passes():
    required, optional = lists()
    assert gl_check.check(table(required), required, optional) == ([], [])


def test_missing_function_fails():
    required, optional = lists()
    missing, _ = gl_check.check(table(name for name in required if name != "glDeleteRenderbuffers"), required, optional)
    assert missing == ["glDeleteRenderbuffers"]


def test_missing_optional_is_only_listed():
    required, optional = lists()
    missing, skipped = gl_check.check(table(name for name in required if name != "glClipControl"), required, optional)
    assert missing == [] and skipped == ["glClipControl"]


def test_main_exit_codes(tmp_path):
    required, optional = lists()
    good, bad = tmp_path / "good_gl.c", tmp_path / "bad_gl.c"
    good.write_text(table(required))
    bad.write_text(table(required[1:]))
    assert gl_check.main([str(good)]) == 0
    assert gl_check.main([str(good), str(bad)]) == 1
