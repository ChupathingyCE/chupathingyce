# CMake toolchain of the portable 64-bit Linux build's SDL3
# (tools/linux64_build.py): clang for x86-64, against the amd64 system root
# of tools/linux_sysroot.py (--arch amd64), given as -DHALO_SYSROOT=<folder>.
# As tools/linux_sysroot.cmake, the 32-bit build's.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER_TARGET x86_64-linux-gnu)
set(CMAKE_SYSROOT "${HALO_SYSROOT}")
set(CMAKE_LIBRARY_ARCHITECTURE x86_64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${HALO_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${HALO_SYSROOT}/usr/lib/x86_64-linux-gnu/pkgconfig:${HALO_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
# (a program that a check links may use a library whose own libraries the
# system root has not got, as it has only what SDL's headers and dlopen
# names need: libX11's libxcb, in the check for XInput 2.1's scrolling)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--allow-shlib-undefined")
# (and in the projects CMake tries compiling in)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES HALO_SYSROOT)
