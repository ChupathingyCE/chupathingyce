"""Runs the C tests of which addresses the game's HTTP requests may send a
secret to, and which redirects a download follows
(port/linux/src/http_url_rules.h), with the build machine's C compiler. The unit is header only, so it compiles anywhere."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_http_url_rules(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "http_url_rules_test"
    build = subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "http_url_rules_test.c"), "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("path,function", [("event_upload.c", "safe_for_token"), ("browser.c", "safe_for_key")])
def test_secrets_use_the_exact_check(path, function):
    """The events token and the claim key both go through the exact check."""
    text = (SOURCE / path).read_text()
    body = re.search(r"^static int " + function + r"\(const char \*url\)\n\{.*?^\}", text, re.M | re.S).group(0)
    assert "return http_url_safe_for_secret(url);" in body
    assert "strncmp" not in body


def test_download_checks_each_redirect():
    """posix_browser.c's download loop checks a redirect before following
    it, and still stops after MAXIMUM_REDIRECTS."""
    text = (SOURCE / "posix_browser.c").read_text()
    body = re.search(r"^int posix_browser_download_open\(.*?^\}", text, re.M | re.S).group(0)
    assert "redirect <= MAXIMUM_REDIRECTS" in body
    check = body.index("http_url_redirect_allowed(current, location)")
    assert check < body.index('snprintf(current, sizeof(current), "%s", location);')
    assert check < body.index("if (location[0] == '/')")


def test_updater_is_https_only():
    """The updater's downloads (posix_update.c) take only https:// addresses,
    redirects included, so they never leave https:// either."""
    text = (SOURCE / "posix_update.c").read_text()
    parse = re.search(r"^static int parse_url\(.*?^\}", text, re.M | re.S).group(0)
    assert 'strncasecmp(url, "https://", 8) != 0' in parse
    get = re.search(r"^static int https_get\(.*?^\}", text, re.M | re.S).group(0)
    assert get.index("parse_url(url,") < get.index("connection_open(")
