"""Exercise the Android MCC host allocator and guest bridge with failing OS calls."""
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

from test_mcc_identity import compile_tool

ROOT = Path(__file__).resolve().parents[1]


def implementation(path):
    return "\n".join(line for line in (ROOT / path).read_text().splitlines()
                     if not line.startswith("#include"))


@pytest.fixture(scope="module")
def allocator(tmp_path_factory):
    work = tmp_path_factory.mktemp("android-mcc-memory")
    source = work / "test.c"
    source.write_text(r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define HALO_ANDROID 1
#define HOST_LOG_WARN 5
static void host_logf(int priority, const char *format, ...) {(void)priority; (void)format;}
#define platform_log(...) ((void)0)
#define MAP_FAILED ((void *)-1)
#define MAP_PRIVATE 1
#define MAP_ANONYMOUS 2
#define MAP_NORESERVE 4
#define MAP_FIXED_NOREPLACE 8
#define MAP_FIXED 16
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
typedef int pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER 0
#define CHECK(c) do { if (!(c)) {fprintf(stderr, "%d: %s\n", __LINE__, #c); exit(1);} } while (0)
static int depth, maps, unmaps, blocked, wrong_address, fail_commit, fail_release, fail_unmap;
static int pages, contents;
static void *owned;
static int pthread_mutex_lock(pthread_mutex_t *p) {(void)p; CHECK(depth++ == 0); return 0;}
static int pthread_mutex_unlock(pthread_mutex_t *p) {(void)p; CHECK(--depth == 0); return 0;}
static void *mmap(void *p, size_t n, int prot, int flags, int fd, long offset) {
    CHECK(depth == 1 && n == 0x4000000u && fd == -1 && offset == 0);
    maps++;
    if (flags & MAP_FIXED) {
        CHECK(owned && p == owned); /* Never replace a foreign reservation. */
        if ((prot && fail_commit) || (!prot && fail_release)) {errno=ENOMEM; return MAP_FAILED;}
        pages=!!prot; contents=0;
        return p;
    }
    CHECK(flags & MAP_FIXED_NOREPLACE);
    if (owned || blocked) {errno=EEXIST; return MAP_FAILED;}
    CHECK(prot == PROT_NONE);
    owned=wrong_address ? (void *)(uintptr_t)0x60000000u : p;
    return owned;
}
static int munmap(void *p, size_t n) {
    CHECK(depth == 1 && p == owned && n == 0x4000000u);
    if(fail_unmap) {errno=ENOMEM; return -1;}
    unmaps++; owned=NULL; pages=0; contents=0; return 0;
}
''' + implementation("port/android/host/host_mcc_memory.c") + "\n" +
                      implementation("port/linux/src/mcc_memory.c") + r'''
int main(int argc, char **argv) {
    void *p; int before, i;
    CHECK(argc == 2);
    if (!strcmp(argv[1], "collision")) {
        blocked=1;
        host_mcc_memory_initialize();
        CHECK(!owned && !mcc_window && !pages);
        CHECK(!mcc_memory_reserve(0x50000000u,0x4000000u) && errno == EEXIST);
        CHECK(!unmaps && !owned);
        blocked=0; /* Retry if the foreign allocation has since gone away. */
    } else if (!strcmp(argv[1], "old-kernel")) {
        wrong_address=1;
        host_mcc_memory_initialize();
        CHECK(!owned && !mcc_window && unmaps == 1);
        CHECK(!mcc_memory_reserve(0x50000000u,0x4000000u) && errno == EEXIST);
        CHECK(!owned && unmaps == 2);
        wrong_address=0;
    } else if (!strcmp(argv[1], "invalid")) {
        CHECK(!mcc_memory_reserve(0x40440000u,0x4000000u) && errno == EINVAL);
        CHECK(!mcc_memory_reserve(0x80000000u,0x4000000u));
        CHECK(!mcc_memory_reserve(0x50000001u,0x4000000u));
        CHECK(!mcc_memory_reserve(0x50000000u,0x4000001u));
        CHECK(!mcc_memory_reserve(0xffffffffu,0xffffffffu));
        CHECK(!maps);
    }
    host_mcc_memory_initialize();
    CHECK(owned == (void *)(uintptr_t)0x50000000u && !pages && !mcc_active);
    before=maps; host_mcc_memory_initialize(); CHECK(maps == before);
    if (!strcmp(argv[1], "commit-failure")) {
        fail_commit=1;
        CHECK(!mcc_memory_reserve(0x50000000u,0x4000000u) && errno == ENOMEM);
        CHECK(owned && !mcc_active && !pages);
        fail_commit=0;
    }
    for(i=0;i<3;i++) {
        p=mcc_memory_reserve(0x50000000u,0x4000000u);
        CHECK(p == owned && pages && mcc_active && !contents);
        contents=42;
        before=maps;
        CHECK(!mcc_memory_reserve(0x50000000u,0x4000000u) && errno == EBUSY);
        CHECK(!mcc_memory_reserve(0x51000000u,0x4000000u) && errno == EBUSY);
        CHECK(host_mcc_memory_release(0x51000000u,0x4000000u) == -EINVAL);
        CHECK(host_mcc_memory_release(0x50000000u,1) == -EINVAL);
        mcc_memory_release(NULL,0);
        CHECK(maps == before && contents == 42);
        if (!strcmp(argv[1], "release-failure")) {
            fail_release=1;
            CHECK(host_mcc_memory_release(0x50000000u,0x4000000u) == -ENOMEM);
            CHECK(mcc_active && contents == 42);
            CHECK(!mcc_memory_reserve(0x50000000u,0x4000000u) && errno == EBUSY);
            fail_release=0;
        }
        mcc_memory_release(p,0x4000000u);
        CHECK(owned == p && !mcc_active && !pages && !contents);
        CHECK(host_mcc_memory_release(0x50000000u,0x4000000u) == -EINVAL);
    }
    if (!strcmp(argv[1], "alternate")) {
        fail_unmap=1;
        CHECK(!mcc_memory_reserve(0x51000000u,0x4000000u) && errno == ENOMEM);
        CHECK(owned == (void *)(uintptr_t)0x50000000u);
        fail_unmap=0;
        blocked=1;
        CHECK(!mcc_memory_reserve(0x51000000u,0x4000000u) && errno == EEXIST);
        CHECK(!owned);
        blocked=0;
        p=mcc_memory_reserve(0x51000000u,0x4000000u);
        CHECK(p == (void *)(uintptr_t)0x51000000u);
        mcc_memory_release(p,0x4000000u);
        CHECK(mcc_memory_reserve(0x50000000u,0x4000000u) == (void *)(uintptr_t)0x50000000u);
        mcc_memory_release(owned,0x4000000u);
    }
    CHECK(depth == 0);
    return 0;
}
''')
    return compile_tool(work, [source])


@pytest.mark.parametrize("case", ["reload", "collision", "old-kernel", "invalid",
                                  "commit-failure", "release-failure", "alternate"])
def test_android_mcc_reservation(allocator, case):
    subprocess.run([str(allocator), case], check=True, capture_output=True, text=True, timeout=10)


def test_android_imports_and_startup(tmp_path):
    main = (ROOT / "port/android/host/host_main.c").read_text()
    startup = main[main.index("JNIEXPORT jint JNICALL JNI_OnLoad"):main.index("int main(")]
    assert startup.index("host_memory_reserve_early();") < startup.index("host_mcc_memory_initialize();")
    table = tmp_path / "imports.c"
    subprocess.run([sys.executable, str(ROOT / "tools/android_imports.py"),
                    "--host-table", str(table), str(tmp_path / "imports.s"),
                    str(ROOT / "port/android/host_imports.list")], check=True)
    for name in ("host_mcc_memory_reserve", "host_mcc_memory_release"):
        assert name in table.read_text()
        assert name in (ROOT / "port/android/guest/runtime/guest_host.h").read_text()


@pytest.mark.skipif(sys.platform != "linux", reason="real Linux mmap semantics")
def test_real_reserved_pages_and_foreign_mapping(tmp_path):
    compiler = shutil.which("clang")
    if not compiler:
        pytest.skip("clang required")
    source = tmp_path / "real.c"
    source.write_text(r'''
#include "host_mcc_memory.h"
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"%d: %s\n",__LINE__,#c);exit(1);}}while(0)
void host_logf(int priority, const char *format, ...) {(void)priority;(void)format;}
static int successes;
static void *acquire(void *unused) {
    long long result=host_mcc_memory_reserve(0x50000000u,0x4000000u);
    (void)unused;
    CHECK(result==0x50000000u || result==-EBUSY);
    if(result>0) __sync_add_and_fetch(&successes,1);
    return NULL;
}
int main(void) {
    unsigned char *p=(unsigned char *)(uintptr_t)0x50000000u;
    void *other;
    pthread_t a,b;
    /* A real occupied page: even a partial overlap must survive unchanged. */
    CHECK(mmap(p+4096,4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==p+4096);
    p[4096]=123;
    host_mcc_memory_initialize();
    CHECK(host_mcc_memory_reserve(0x50000000u,0x4000000u)==-EEXIST);
    CHECK(p[4096]==123);
    CHECK(!munmap(p+4096,4096));
    host_mcc_memory_initialize();
    other=mmap(p,4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(other==MAP_FAILED && errno==EEXIST);
    CHECK(!pthread_create(&a,NULL,acquire,NULL));
    CHECK(!pthread_create(&b,NULL,acquire,NULL));
    CHECK(!pthread_join(a,NULL) && !pthread_join(b,NULL) && successes==1);
    p[0]=42;p[0x3ffffff]=43;
    CHECK(!host_mcc_memory_release(0x50000000u,0x4000000u));
    CHECK(mmap(p,4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==MAP_FAILED);
    CHECK(errno==EEXIST);
    CHECK(host_mcc_memory_reserve(0x50000000u,0x4000000u)==0x50000000u);
    CHECK(!p[0] && !p[0x3ffffff]);
    CHECK(!host_mcc_memory_release(0x50000000u,0x4000000u));
    return 0;
}
''')
    binary = tmp_path / "real"
    result = subprocess.run([compiler, "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-pthread",
                             "-I", str(ROOT / "port/android/include"),
                             "-I", str(ROOT / "port/android/host"), str(source),
                             str(ROOT / "port/android/host/host_mcc_memory.c"), "-o", str(binary)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=10)
