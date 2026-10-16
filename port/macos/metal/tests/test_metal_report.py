"""Compile the real reporter with a log sink: dedup, bounds, threads, strict mode."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from metal_paths import source

DRIVER = r'''
#include "metal_report.h"
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static unsigned logs;
void platform_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
    __atomic_fetch_add(&logs, 1, __ATOMIC_RELAXED);
}
static void *repeat(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 1000; i++)
        METAL_REPORT(METAL_REPORT_COMMAND, "completion", 0, 7, "frame lost", "driver error");
    return NULL;
}
int main(int argc, char **argv)
{
    metal_report_reset();
    if (argc > 1 && !strcmp(argv[1], "strict")) {
        metal_report_configure(1);
        METAL_REPORT(METAL_REPORT_UNSUPPORTED_STATE, "state", 1, 99, "fallback", NULL);
        return 9;
    }
    if (argc > 1 && !strcmp(argv[1], "strict-duplicate")) {
        METAL_REPORT(METAL_REPORT_UNSUPPORTED_STATE, "state", 1, 99, "fallback", NULL);
        metal_report_configure(1);
        METAL_REPORT(METAL_REPORT_UNSUPPORTED_STATE, "state", 1, 99, "fallback", NULL);
        return 9;
    }
    if (argc > 1 && !strcmp(argv[1], "strict-full")) {
        for (unsigned i = 0; i < 128; i++)
            METAL_REPORT(METAL_REPORT_UNSUPPORTED_FORMAT, "format", 0, i, "skipped", NULL);
        metal_report_configure(1);
        METAL_REPORT(METAL_REPORT_COMMAND, "completion", 0, 7, "frame lost", NULL);
        return 9;
    }
    metal_report_frame = 42;
    metal_report_draw = 3;
    assert(METAL_REPORT(METAL_REPORT_UNSUPPORTED_STATE, "state", 1, 99, "fallback", NULL) == 1);
    assert(METAL_REPORT(METAL_REPORT_UNSUPPORTED_STATE, "state", 1, 99, "fallback", NULL) == 0);
    assert(logs == 1);
    assert(metal_report_count(METAL_REPORT_UNSUPPORTED_STATE) == 2);
    assert(metal_report_distinct() == 1);
    pthread_t threads[4];
    for (unsigned i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, repeat, NULL));
    for (unsigned i = 0; i < 4; i++) assert(!pthread_join(threads[i], NULL));
    assert(metal_report_count(METAL_REPORT_COMMAND) == 4000);
    assert(metal_report_distinct() == 2);
    assert(logs == 2);
    metal_report_summary();
    for (unsigned i = 0; i < 160; i++)
        METAL_REPORT(METAL_REPORT_UNSUPPORTED_FORMAT, "format", 0, i, "skipped", NULL);
    assert(metal_report_count(METAL_REPORT_UNSUPPORTED_FORMAT) == 160);
    assert(metal_report_distinct() == 128);
    for (unsigned i = 0; i < 10; i++)
        METAL_REPORT(METAL_REPORT_UNSUPPORTED_FORMAT, "format", 0, 159, "skipped", NULL);
    assert(metal_report_distinct() == 128);
    assert(metal_report_count(METAL_REPORT_UNSUPPORTED_FORMAT) == 170);
    metal_report_summary();
    assert(metal_report_issue(NULL) == 0);
    assert(metal_report_count(METAL_REPORT_KINDS) == 0);
    metal_report_reset();
    assert(metal_report_count(METAL_REPORT_COMMAND) == 0);
    assert(metal_report_distinct() == 0);
    return 0;
}
'''


class ReportTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("clang"), "clang required")
    def test_reporter(self):
        with tempfile.TemporaryDirectory(prefix="metal-report-") as folder:
            root = Path(folder)
            (root / "driver.c").write_text(DRIVER)
            binary = root / "report"
            subprocess.run(["clang", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread",
                            "-fsanitize=address,undefined", "-I", str(root),
                            "-I", str(source("metal_report.h").parent),
                            str(source("metal_report.c")), str(root / "driver.c"), "-o", str(binary)], check=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("Metal compatibility:", run.stderr)
            self.assertIn("Metal failure:", run.stderr)
            self.assertIn("frame 42, draw 3", run.stderr)
            for mode in ("strict", "strict-duplicate", "strict-full"):
                with self.subTest(mode=mode):
                    strict = subprocess.run([str(binary), mode], capture_output=True, text=True)
                    self.assertLess(strict.returncode, 0, strict.stderr)
                    self.assertIn("stopping at the first report", strict.stderr)
            self.assertIn("44 additional occurrence(s) not itemized", run.stderr)
