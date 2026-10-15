"""Exercise Linux math compatibility using the game's strict-C89 header mode."""
import shutil
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase, skipUnless

ROOT = Path(__file__).resolve().parents[1]


class LinuxMathTests(TestCase):
    @skipUnless(shutil.which("clang"), "clang required")
    def test_finite_classification_in_strict_c89(self):
        compiler = shutil.which("clang")
        assert compiler is not None
        with tempfile.TemporaryDirectory(prefix="linux-math-test-") as directory:
            source, executable = Path(directory) / "finite.c", Path(directory) / "finite"
            source.write_text(r'''
#include <assert.h>
#include <float.h>
#include <math.h>
static int evaluations;
static double next_value(void) { evaluations++; return 1.0; }
int main(void) {
    volatile float finite_f = FLT_MAX;
    volatile double finite_d = DBL_MAX;
    volatile long double finite_l = LDBL_MAX;
    volatile float infinity_f = __builtin_inff();
    volatile double infinity_d = __builtin_inf();
    volatile long double infinity_l = __builtin_infl();
    volatile float nan_f = __builtin_nanf("");
    volatile double nan_d = __builtin_nan("");
    volatile long double nan_l = __builtin_nanl("");
    assert(isfinite(0.0) && isfinite(-0.0));
    assert(isfinite(finite_f) && isfinite(-finite_f));
    assert(isfinite(finite_d) && isfinite(-finite_d));
    assert(isfinite(finite_l) && isfinite(-finite_l));
    assert(!isfinite(infinity_f) && !isfinite(-infinity_f));
    assert(!isfinite(infinity_d) && !isfinite(-infinity_d));
    assert(!isfinite(infinity_l) && !isfinite(-infinity_l));
    assert(!isfinite(nan_f) && !isfinite(nan_d) && !isfinite(nan_l));
    assert(isfinite(next_value()));
    assert(evaluations == 1);
    return 0;
}
''')
            subprocess.run([compiler, "-std=gnu89", "-D__STRICT_ANSI__", "-O2", "-Wall", "-Wextra",
                            "-Werror=implicit-function-declaration", "-I", str(ROOT / "port/linux/include"),
                            str(source), "-lm", "-o", str(executable)], check=True, timeout=30)
            subprocess.run([str(executable)], check=True, timeout=30)
