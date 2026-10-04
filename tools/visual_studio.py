#!/usr/bin/env python3
"""Writes the Visual Studio build of the Windows port:

    python tools/visual_studio.py

makes projects/windows/halo.sln and halo.vcxproj (and its filters), which
build what ``ninja windows`` (the solution's platform x86) and ``ninja
windows64`` (its platform x64: the 64-bit game, which plays Halo PC's Custom
Edition maps and HaloMD's) build (tools/windows_build.py: the same sources,
flags and libraries) with Visual Studio alone: its ClangCL toolset
("C++ Clang tools for Windows" in the Visual Studio Installer) and a
Windows SDK. No Python, ninja or separate LLVM is needed to build with
them: the files are committed, with what the ninja build generates from the
sources (port/windows/generated), and the build's own step before it
compiles (port/windows/tools/prebuild.ps1) embeds the assets, writes the
version and downloads SDL3 with PowerShell.

It is the build with the game browser (HALO_GAME_BROWSER: the game list, the
server browser and the dedicated server), as configure.py's own is unless
told otherwise.

Run it again when a source file is added, removed or renamed, and commit
what it writes. See port/windows/README.md.
"""

import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path, PureWindowsPath
from typing import Dict, List, Tuple
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
os.chdir(ROOT)

from tools.embed_assets import UI_FONT_FILES  # noqa: E402
from tools.linux_build import (MBEDTLS_DIR, MINIUPNPC_DIR, MUSL_MATH_DIR, STB_DIR, XDK_INCLUDE, game_sources,  # noqa: E402
                               miniupnpc_sources, musl_math_sources)
from tools.lp64_build import lp64_excluded  # noqa: E402
from tools.windows_build import MONOCYPHER_DIR  # noqa: E402
from tools.windows_build import (COMMENT, EXPAT_DIR, EXPAT_SOURCES, EXPORTED_INLINE, KCP_DIR, LINUX_DIR,  # noqa: E402
                                 PORT_DIR, QRCODEGEN_DIR, SDL_SHA256, SDL_VERSION, TOML_DIR, WIN32_FLAGS,
                                 WINDOWS32, WINDOWS64, WindowsTarget)

PROJECT_DIR = Path("projects/windows")
GENERATED = PORT_DIR / "generated"
PROJECT_GUID = "{6B0B1C54-6D2A-4E0B-9F5C-2B7A4C1D9E10}"
SOLUTION_GUID = "{3F1E2D4C-5B6A-4798-8A9B-0C1D2E3F4A5B}"
# (the C++ project type)
VC_PROJECT_TYPE = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"
# the project's platforms, each with the ninja build it is (the solution
# calls Win32 x86): the 32-bit game, and the 64-bit one
PLATFORMS: List[Tuple[str, WindowsTarget]] = [("Win32", WINDOWS32), ("x64", WINDOWS64)]


def when(platform: str) -> str:
    """the condition of what only one platform has (the 32-bit build's is
    every platform's but x64: a project opened with no platform named is
    that one)"""
    return f""" Condition="'$(Platform)'{'==' if platform == 'x64' else '!='}'x64'\""""


def win(path: Path) -> str:
    """a repository path as the project names it"""
    return "$(HaloRoot)" + str(PureWindowsPath(path))


def clang(flag: str) -> str:
    """a clang flag as clang-cl takes it"""
    return f'"/clang:{flag}"' if " " in flag or "$(" in flag else f"/clang:{flag}"


def split_flags(flags: List[str]) -> Tuple[List[str], List[str]]:
    """a group's flags as (defines, the others): the defines go to the
    project's own setting, -O and -g to the configuration's"""
    defines = [flag[2:] for flag in flags if flag.startswith("-D")]
    others = [flag for flag in flags
              if not flag.startswith("-D") and not flag.startswith("-O") and flag not in ("-g", "-gcodeview")]
    return defines, others


def write_if_changed(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = text.replace("\n", "\r\n").encode("utf-8") if path.suffix in (".sln", ".vcxproj", ".filters") else \
        text.encode("utf-8")
    if not path.is_file() or path.read_bytes() != data:
        path.write_bytes(data)
        print(f"wrote {path}")


def generated_sources() -> Dict[Path, Path]:
    """What the ninja build generates from the sources, committed for this
    one: the file-scope structure tags (halo_msvc_tags.h), and a wrapper for
    each unit that defines an external __inline function
    (tools/windows_build.py, inline_export_wrapper). Returns the wrappers by
    source."""
    tags = GENERATED / "halo_msvc_tags.h"
    tags.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, "tools/linux_msvc_semantics.py", "--output", str(tags), "--tags", "source"],
                   check=True)
    wrappers: Dict[Path, Path] = {}
    linux_config = json.loads((LINUX_DIR / "port.json").read_text(encoding="utf-8"))
    kept = set()
    for source in game_sources(linux_config):
        names = EXPORTED_INLINE.findall(COMMENT.sub("", source.read_text(encoding="utf-8", errors="replace")))
        if not names:
            continue
        wrapper = GENERATED / "inline_exports" / source
        relative = os.path.relpath(source, wrapper.parent).replace(os.sep, "/")
        text = (
            "/* generated by tools/visual_studio.py: see inline_export_wrapper in tools/windows_build.py */\n"
            f'#include "{relative}"\n'
            "static void *const halo_windows_inline_exports[] __attribute__((used)) = {\n"
            + "".join(f"\t(void *){name},\n" for name in names)
            + "};\n"
        )
        write_if_changed(wrapper, text)
        wrappers[source] = wrapper
        kept.add(wrapper)
    # (a wrapper whose source no longer needs one)
    for stale in (GENERATED / "inline_exports").rglob("*.c"):
        if stale not in kept:
            stale.unlink()
            print(f"removed {stale}")
    return wrappers


# a header's function that is __inline unless a unit asks for the external
# one (#ifdef <X>_EXTERNAL_<Y>, its prototype, #else, the __inline function)
EXTERNAL_INLINE = re.compile(
    r"#ifdef\s+(\w+_EXTERNAL_\w+)\s*\n\s*[^;{}#]*?\b(\w+)\s*\([^;{}()]*\)\s*;\s*\n\s*#else", re.M)


def debug_external_inlines(sources: List[Path]) -> Dict[Path, List[str]]:
    """The Debug configuration's extra defines, by unit. A function a header
    defines __inline that one unit also defines for everyone
    (real_local_random, effects.c) is inlined in its callers by the optimised
    build, which then has the one definition; not optimised, each caller
    emits its own copy as well, and the linker stops at the duplicates. In
    Debug those callers ask for the external one, as the units that name the
    header's macro themselves do."""
    functions = {}
    for header in sorted(Path("source").rglob("*.h")):
        text = COMMENT.sub("", header.read_text(encoding="utf-8", errors="replace"))
        for match in EXTERNAL_INLINE.finditer(text):
            functions[match.group(2)] = match.group(1)
    texts = {source: COMMENT.sub("", source.read_text(encoding="utf-8", errors="replace")) for source in sources}
    defines: Dict[Path, List[str]] = {}
    for name, macro in sorted(functions.items()):
        definition = re.compile(
            r"^(?![ \t])(?![^\n]*\b(?:__inline|_inline|__forceinline|static|extern|typedef|return)\b)"
            r"[^\n;{}()#=]*\b" + name + r"\s*\([^;{}]*\)\s*\{", re.M)
        if not any(definition.search(text) for text in texts.values()):
            continue
        for source, text in texts.items():
            if (not definition.search(text) and re.search(r"\b" + name + r"\s*\(", text)
                    and not re.search(r"#\s*define\s+" + macro + r"\b", text)):
                defines.setdefault(source, []).append(macro)
    return defines


def object_name(source: Path) -> str:
    """an object file's name, its own whatever folder the source is in"""
    digest = hashlib.sha1(source.as_posix().encode("utf-8")).hexdigest()[:8]
    return f"$(IntDir){source.stem}_{digest}.obj"


# a link's flags the project has a setting for (the others go to its
# additional options)
LINK_SETTINGS = {
    "/LARGEADDRESSAWARE": ("LargeAddressAware", "true"),
    "/SUBSYSTEM:CONSOLE": ("SubSystem", "Console"),
}


def link_settings(target: WindowsTarget) -> Tuple[List[Tuple[str, str]], List[str]]:
    """a build's link flags (tools/windows_build.py, ldflags) as the
    project's settings and the options it has none for"""
    settings: List[Tuple[str, str]] = []
    options: List[str] = []
    for flag in target.ldflags:
        if not flag.startswith("-Wl,"):
            continue  # (the target, lld and -g: the toolset's and GenerateDebugInformation's)
        flag = flag[len("-Wl,"):]
        if flag in LINK_SETTINGS:
            settings.append(LINK_SETTINGS[flag])
        elif flag.startswith("/STACK:"):
            settings.append(("StackReserveSize", str(int(flag[len("/STACK:"):], 0))))
        else:
            options.append(flag)
    return settings, options


def main() -> None:
    linux_config = json.loads((LINUX_DIR / "port.json").read_text(encoding="utf-8"))
    config = json.loads((PORT_DIR / "port.json").read_text(encoding="utf-8"))
    wrappers = generated_sources()

    # (the overlay's fonts, as the prebuild step embeds them: the same files in the same order)
    prebuild = (PORT_DIR / "tools" / "prebuild.ps1").read_text(encoding="utf-8")
    listed = prebuild[prebuild.index("$uiFontFiles = @("):]
    listed = [name.strip().strip("',") for name in listed[:listed.index(")")].splitlines()[1:] if name.strip()]
    if listed != UI_FONT_FILES:
        sys.exit(f"{PORT_DIR / 'tools' / 'prebuild.ps1'}: $uiFontFiles is not tools/embed_assets.py's UI_FONT_FILES")
    game = linux_config["game"]
    linux_platform = Path(linux_config["platform_sources"])
    prefix_header = PORT_DIR / "include" / "halo_windows_prefix.h"
    crt_include = PORT_DIR / "include" / "crt"
    posix_include = PORT_DIR / "include" / "posix"
    sdl_include = "$(HaloSDL)\\include"
    generated_c = "$(HaloGenerated)hud_hires_assets.c"
    generated_fonts_c = "$(HaloGenerated)ui_fonts.c"
    version_header = "$(HaloGenerated)halo_version.h"

    # the groups of units: each one's options, defines, include directories
    # and forced includes, as tools/windows_build.py gives them to clang
    def group(flags: List[str], defines: List[str], includes: List[str], forced: List[str], warnings: bool) -> Dict[str, str]:
        return {
            "options": " ".join(["$(HaloAbiOptions)"] + [clang(flag) for flag in flags if flag != "-w"]),
            "defines": ";".join(["$(HaloAbiDefines)"] + defines),
            "includes": ";".join(includes),
            "forced": ";".join(forced),
            "warnings": "Level3" if warnings else "TurnOffAllWarnings",
        }

    def target_groups(target: WindowsTarget) -> Dict[str, Dict[str, str]]:
        """a build's groups (the 32-bit and the 64-bit builds differ in the
        game's, the platform layer's and the Windows units' flags)"""
        game_defines, game_flags = split_flags(target.game_flags)
        platform_defines, platform_flags = split_flags(target.platform_flags)
        win32_defines, win32_flags = split_flags(target.win32_flags)
        # (the third parties' units have the 32-bit build's Windows flags in
        # both: tools/windows_build.py, WIN32_FLAGS)
        other_defines, other_flags = split_flags(WIN32_FLAGS)
        platform_includes = [
            win(posix_include), win(crt_include), win(linux_platform), win(PORT_DIR / "include"), win(TOML_DIR),
            win(EXPAT_DIR), win(KCP_DIR), win(QRCODEGEN_DIR), win(MONOCYPHER_DIR),
            win(Path("source")), win(Path("source/cseries")), sdl_include, win(XDK_INCLUDE)]
        return {
            # (-w hides everything, the errors a build asks for too: a build
            # that has such errors names its warnings itself, after the
            # project's level)
            "Game": group(
                game_flags + [f"-iquote{win(LINUX_DIR / 'include')}"], game_defines + list(game.get("defines", [])),
                [win(crt_include), win(PORT_DIR / "include")] + [win(Path(d)) for d in game.get("include_dirs", [])]
                + [win(XDK_INCLUDE)],
                [win(prefix_header), win(GENERATED / "halo_msvc_tags.h")], "-w" not in target.game_flags),
            "Platform": group(
                platform_flags + [f"-iquote{win(LINUX_DIR / 'include')}"], platform_defines,
                platform_includes, [win(prefix_header)], True),
            # (updater.c, with the version the prebuild step writes: a string on
            # the command line does not survive MSBuild's quoting)
            "PlatformUpdater": group(
                platform_flags + [f"-iquote{win(LINUX_DIR / 'include')}"], platform_defines,
                platform_includes, [win(prefix_header), version_header], True),
            # (posix_ui_font.c: stb_truetype)
            "PlatformFont": group(
                platform_flags + [f"-iquote{win(LINUX_DIR / 'include')}"], platform_defines,
                platform_includes + [win(STB_DIR)], [win(prefix_header)], True),
            # (posix_browser.c: the game list's requests, on Winsock with Mbed TLS)
            "Win32Browser": group(
                win32_flags, win32_defines,
                [win(posix_include), win(linux_platform), sdl_include, win(MBEDTLS_DIR / "include")], [], True),
            "Mbedtls": group(
                [flag for flag in other_flags if flag != "-Wall"], other_defines,
                [win(MBEDTLS_DIR / "include"), win(MBEDTLS_DIR / "library")], [], False),
            "Win32": group(
                win32_flags, win32_defines, [win(posix_include), win(linux_platform), sdl_include], [], True),
            "Win32Upnp": group(
                win32_flags, win32_defines + ["MINIUPNP_STATICLIB"],
                [win(posix_include), win(linux_platform), sdl_include, win(MINIUPNPC_DIR / "include")], [], True),
            "Miniupnpc": group(
                [flag for flag in other_flags if flag != "-Wall"],
                other_defines + ["MINIUPNP_STATICLIB"],
                [win(MINIUPNPC_DIR / "include"), win(MINIUPNPC_DIR / "src")], [], False),
            "ThirdParty": group(["-std=gnu11"], [], [], [], False),
            # (the menus' XML parser: port/third_party/expat; menu_files.c)
            "Expat": group(["-std=gnu11"], [], [win(EXPAT_DIR)], [], False),
            "MuslMath": group(["-std=gnu11"], [], [win(MUSL_MATH_DIR / "include")],
                              [win(MUSL_MATH_DIR / "include" / "libm.h")], False),
        }

    groups: Dict[str, Dict[str, Dict[str, str]]] = {platform: target_groups(target) for platform, target in PLATFORMS}
    group_names = list(groups[PLATFORMS[0][0]])

    # the units: (the file compiled, the source it is, its group, more defines)
    units: List[Tuple[str, Path, str, List[str]]] = []

    def add(source: Path, name: str, defines: List[str] = []) -> None:
        compiled = wrappers.get(source, source)
        units.append((win(compiled), source, name, defines))

    debug_defines = debug_external_inlines(list(game_sources(linux_config)))
    for source in game_sources(linux_config):
        add(source, "Game", ["$(HaloDebugOnly%s)" % macro for macro in debug_defines.get(source, [])])
    for source in sorted(Path(linux_config["game_sources"]).glob("*.c")):
        add(source, "Game")
    # the dedicated server's director (server/)
    for source in sorted(Path("server/src").glob("*.c")):
        add(source, "Game")
    replaced = set(config.get("replaced_platform_sources", []))
    for source in sorted(linux_platform.glob("*.c")):
        if source.name in replaced:
            continue
        # (updater.c: the version, and no release's build, so a build of
        # one's own never looks for updates)
        add(source, {"updater.c": "PlatformUpdater", "posix_browser.c": "Win32Browser",
                     "posix_ui_font.c": "PlatformFont"}.get(source.name, "Platform"))
    # the game list's TLS (port/third_party/mbedtls; posix_browser.c)
    for source in sorted((MBEDTLS_DIR / "library").glob("*.c")):
        add(source, "Mbedtls")
    for source in sorted((PORT_DIR / "src").glob("*.c")):
        if source.name == "win32_upnp.c":
            add(source, "Win32Upnp")
        else:
            add(source, "Win32" if source.name.startswith("win32_") else "Platform")
    for source in miniupnpc_sources():
        add(source, "Miniupnpc")
    add(TOML_DIR / "tomlc17.c", "ThirdParty")
    add(KCP_DIR / "ikcp.c", "ThirdParty")
    # Link Profile's QR code (port/third_party/qrcodegen; browser.c)
    add(QRCODEGEN_DIR / "qrcodegen.c", "ThirdParty")
    # internet play's signatures, for public games' listings
    # (port/third_party/monocypher; p2p_crypto.c)
    for name in ("monocypher.c", "monocypher-ed25519.c"):
        add(MONOCYPHER_DIR / name, "ThirdParty")
    for name in EXPAT_SOURCES:
        add(EXPAT_DIR / name, "Expat")
    for source in musl_math_sources():
        add(source, "MuslMath")

    # what a build leaves out (the 64-bit one: port/linux/port.json "lp64")
    excluded: Dict[str, set] = {platform: set(lp64_excluded()) if target.lp64_exclusions else set()
                                for platform, target in PLATFORMS}

    libraries = ["$(HaloSDL)\\lib\\$(HaloSDLArch)\\SDL3.lib"] + [f"{lib}.lib" for lib in config.get("libraries", [])]

    lines: List[str] = []
    out = lines.append

    def out_property(name: str, values: Dict[str, str], indent: str = "    ") -> None:
        """a property: once when the platforms have the same, else once each"""
        if len(set(values.values())) == 1:
            out(f"{indent}<{name}>{escape(next(iter(values.values())))}</{name}>")
            return
        for platform, value in values.items():
            out(f"{indent}<{name}{when(platform)}>{escape(value)}</{name}>")

    out('<?xml version="1.0" encoding="utf-8"?>')
    out("<!-- generated by tools/visual_studio.py: do not edit -->")
    out('<Project DefaultTargets="Build" ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')
    out('  <ItemGroup Label="ProjectConfigurations">')
    for platform, _ in PLATFORMS:
        for configuration in ("Release", "Debug"):
            out(f'    <ProjectConfiguration Include="{configuration}|{platform}">')
            out(f"      <Configuration>{configuration}</Configuration>")
            out(f"      <Platform>{platform}</Platform>")
            out("    </ProjectConfiguration>")
    out("  </ItemGroup>")
    out('  <PropertyGroup Label="Globals">')
    out(f"    <ProjectGuid>{PROJECT_GUID}</ProjectGuid>")
    out("    <Keyword>Win32Proj</Keyword>")
    out("    <RootNamespace>halo</RootNamespace>")
    out("    <ProjectName>halo</ProjectName>")
    out("    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>")
    out("    <HaloRoot>$(MSBuildThisFileDirectory)..\\..\\</HaloRoot>")
    out("    <!-- SDL3's Visual C++ development package (include\\SDL3\\SDL.h, and lib\\x86 and lib\\x64 with SDL3.lib and")
    out("         SDL3.dll): the build downloads it here once and checks it against the hash below")
    out("         (port\\windows\\tools\\prebuild.ps1). Set the HALO_SDL3 environment variable to use one already on the")
    out("         computer. -->")
    out(f"    <HaloSDLVersion>{SDL_VERSION}</HaloSDLVersion>")
    out(f"    <HaloSDLSha256>{SDL_SHA256}</HaloSDLSha256>")
    out("    <HaloSDL Condition=\"'$(HaloSDL)'=='' and '$(HALO_SDL3)'!=''\">$(HALO_SDL3)</HaloSDL>")
    out("    <HaloSDL Condition=\"'$(HaloSDL)'==''\">$(HaloRoot)build\\windows\\third_party\\SDL3-$(HaloSDLVersion)</HaloSDL>")
    out_property("HaloSDLArch", {platform: target.sdl_arch for platform, target in PLATFORMS})
    out("    <!-- each platform's build has its own folder, as the ninja builds have (build\\windows, build\\windows64);")
    out("         what is generated before compiling is the same for both, and in one place -->")
    out_property("HaloBuild", {platform: f"$(HaloRoot){PureWindowsPath(target.build)}\\" for platform, target in PLATFORMS})
    out("    <HaloGenerated>$(HaloRoot)build\\windows\\generated\\</HaloGenerated>")
    out("  </PropertyGroup>")
    out('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />')
    out('  <PropertyGroup Label="Configuration">')
    out("    <ConfigurationType>Application</ConfigurationType>")
    out("    <UseDebugLibraries>false</UseDebugLibraries>")
    out("    <PlatformToolset>ClangCL</PlatformToolset>")
    out("    <CharacterSet>NotSet</CharacterSet>")
    out("    <WholeProgramOptimization>false</WholeProgramOptimization>")
    out("  </PropertyGroup>")
    out('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />')
    out("  <PropertyGroup>")
    out("    <OutDir>$(HaloBuild)$(Configuration)\\</OutDir>")
    out("    <IntDir>$(HaloBuild)obj\\$(Configuration)\\</IntDir>")
    out("    <TargetName>halo</TargetName>")
    out("    <LinkIncremental>false</LinkIncremental>")
    out("    <GenerateManifest>false</GenerateManifest>")
    out("    <!-- the debugger starts the game where it is built: the game data (maps\\) next to halo.exe, or")
    out("         paths.data in its config.toml -->")
    out("    <LocalDebuggerWorkingDirectory>$(OutDir)</LocalDebuggerWorkingDirectory>")
    out("    <DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>")
    out("    <!-- Debug: not optimised. Release: optimised as the ninja build is (-O2). Both check the game's")
    out("         assertions; add HALO_RELEASE to HaloAbiDefines for a build that does not (configure.py's release). -->")
    out("    <HaloOptimisation Condition=\"'$(Configuration)'=='Debug'\">/clang:-O0</HaloOptimisation>")
    out("    <HaloOptimisation Condition=\"'$(Configuration)'!='Debug'\">/clang:-O2</HaloOptimisation>")
    abi_options: Dict[str, str] = {}
    abi_defines: Dict[str, str] = {}
    for platform, target in PLATFORMS:
        defines, flags = split_flags(target.abi_flags)
        flags.append("-march=x86-64")
        # (the game browser, the game list and dedicated servers: tools/linux_build.py's game_browser_defines)
        defines.append("HALO_GAME_BROWSER")
        abi_options[platform] = f"{' '.join(clang(flag) for flag in flags)} $(HaloOptimisation)"
        abi_defines[platform] = ";".join(defines)
    out("    <!-- x64: the 64-bit game (HALO_64BIT), which plays Halo PC's Custom Edition maps and HaloMD's")
    out("         (HALO_CUSTOM_EDITION), as ninja windows64 builds it -->")
    out_property("HaloAbiOptions", abi_options)
    out_property("HaloAbiDefines", abi_defines)
    out("    <!-- Debug only: the callers of a header's __inline function that a unit also defines for everyone use")
    out("         that one; not inlined, their own copies would be duplicates (tools/visual_studio.py) -->")
    for macro in sorted({macro for macros in debug_defines.values() for macro in macros}):
        out(f"    <HaloDebugOnly{macro} Condition=\"'$(Configuration)'=='Debug'\">{macro}</HaloDebugOnly{macro}>")
    for name in group_names:
        for setting, suffix in (("options", "Options"), ("defines", "Defines"), ("includes", "Includes"),
                                ("forced", "Forced"), ("warnings", "Warnings")):
            out_property(f"Halo{name}{suffix}", {platform: groups[platform][name][setting] for platform, _ in PLATFORMS})
    out("  </PropertyGroup>")
    out("  <ItemDefinitionGroup>")
    out("    <ClCompile>")
    out("      <CompileAs>CompileAsC</CompileAs>")
    out("      <PrecompiledHeader>NotUsing</PrecompiledHeader>")
    out("      <!-- the static C runtime, as the ninja build links -->")
    out("      <RuntimeLibrary>MultiThreaded</RuntimeLibrary>")
    out("      <!-- (the optimisation is HaloOptimisation's, the same flag the ninja build gives clang) -->")
    out("      <Optimization>Custom</Optimization>")
    out("      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>")
    out("      <TreatWarningAsError>false</TreatWarningAsError>")
    out("      <BufferSecurityCheck>false</BufferSecurityCheck>")
    out("      <OmitFramePointers>false</OmitFramePointers>")
    out("      <FloatingPointModel>Precise</FloatingPointModel>")
    out("      <SDLCheck>false</SDLCheck>")
    out("      <ExceptionHandling>false</ExceptionHandling>")
    out("      <BasicRuntimeChecks>Default</BasicRuntimeChecks>")
    out("      <SupportJustMyCode>false</SupportJustMyCode>")
    out("      <MultiProcessorCompilation>true</MultiProcessorCompilation>")
    out("    </ClCompile>")
    out("    <Link>")
    out("      <GenerateDebugInformation>true</GenerateDebugInformation>")
    out("      <!-- as the ninja build links: nothing folded or dropped -->")
    out("      <OptimizeReferences>false</OptimizeReferences>")
    out("      <EnableCOMDATFolding>false</EnableCOMDATFolding>")
    out(f"      <AdditionalDependencies>{';'.join(libraries)};%(AdditionalDependencies)</AdditionalDependencies>")
    out("    </Link>")
    out("  </ItemDefinitionGroup>")
    out("  <!-- each platform's link, as its ninja build's (tools/windows_build.py, ldflags). x86: the Xbox memory window")
    out("       is at 0x80000000, in the upper half of the 32-bit address space. x64: the Xbox address space is 4 GB at")
    out("       1 TB, and the first allocations of the process stay low, clear of it. -->")
    for platform, target in PLATFORMS:
        settings, options = link_settings(target)
        out(f"  <ItemDefinitionGroup{when(platform)}>")
        out("    <Link>")
        out(f"      <TargetMachine>{'MachineX64' if platform == 'x64' else 'MachineX86'}</TargetMachine>")
        for name, value in settings:
            out(f"      <{name}>{value}</{name}>")
        if platform != "x64":
            # (x86 only: the linker refuses the setting for another machine)
            out("      <ImageHasSafeExceptionHandlers>false</ImageHasSafeExceptionHandlers>")
        if options:
            out(f"      <AdditionalOptions>{' '.join(options)} %(AdditionalOptions)</AdditionalOptions>")
        out("    </Link>")
        out("  </ItemDefinitionGroup>")

    def unit_lines(include: str, source: Path, name: str, defines: List[str]) -> None:
        all_defines = ";".join(defines + [f"$(Halo{name}Defines)", "%(PreprocessorDefinitions)"])
        out(f'    <ClCompile Include="{escape(include)}">')
        for platform, _ in PLATFORMS:
            if source.as_posix() in excluded[platform]:
                out(f"      <ExcludedFromBuild{when(platform)}>true</ExcludedFromBuild>")
        out(f"      <ObjectFileName>{object_name(source)}</ObjectFileName>")
        out(f"      <WarningLevel>$(Halo{name}Warnings)</WarningLevel>")
        out(f"      <PreprocessorDefinitions>{escape(all_defines)}</PreprocessorDefinitions>")
        if any(groups[platform][name]["includes"] for platform, _ in PLATFORMS):
            out(f"      <AdditionalIncludeDirectories>$(Halo{name}Includes)</AdditionalIncludeDirectories>")
        if any(groups[platform][name]["forced"] for platform, _ in PLATFORMS):
            out(f"      <ForcedIncludeFiles>$(Halo{name}Forced)</ForcedIncludeFiles>")
        out(f"      <AdditionalOptions>$(Halo{name}Options) %(AdditionalOptions)</AdditionalOptions>")
        out("    </ClCompile>")

    out("  <ItemGroup>")
    for include, source, name, defines in units:
        unit_lines(include, source, name, defines)
    # the high-res HUD's textures, the titles and the fonts (prebuild.ps1 writes it)
    unit_lines(generated_c, Path("build/windows/generated/hud_hires_assets.c"), "Platform", [])
    # the overlay's fonts (the game browser's; prebuild.ps1 writes it)
    unit_lines(generated_fonts_c, Path("build/windows/generated/ui_fonts.c"), "Platform", [])
    out("  </ItemGroup>")
    out("  <ItemGroup>")
    out(f'    <None Include="{win(PORT_DIR / "tools" / "prebuild.ps1")}" />')
    out(f'    <None Include="{win(PORT_DIR / "tools" / "bake-navigation.cmd")}" />')
    out("  </ItemGroup>")
    out('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />')
    out("  <!-- before compiling: the embedded assets, the version and SDL3 (PowerShell, which Windows has) -->")
    out('  <Target Name="HaloPrebuild" BeforeTargets="ClCompile">')
    out('    <Exec Command="powershell.exe -NoProfile -ExecutionPolicy Bypass -File &quot;$(HaloRoot)port\\windows\\tools\\prebuild.ps1&quot; '
        '-Root &quot;$(HaloRoot).&quot; -Output &quot;$(HaloGenerated)hud_hires_assets.c&quot; '
        '-FontsOutput &quot;$(HaloGenerated)ui_fonts.c&quot; -VersionOutput &quot;$(HaloGenerated)halo_version.h&quot; '
        '-SdlDir &quot;$(HaloSDL)&quot; -SdlVersion $(HaloSDLVersion) -SdlSha256 $(HaloSDLSha256) -SdlArch $(HaloSDLArch)" />')
    out("  </Target>")
    out("  <!-- SDL3.dll next to the game, and the bots' navigation's bake (bake-navigation.cmd: port/linux/game/bot_bake.c) -->")
    out('  <Target Name="HaloCopySDL" AfterTargets="Build">')
    out('    <Copy SourceFiles="$(HaloSDL)\\lib\\$(HaloSDLArch)\\SDL3.dll;$(HaloRoot)port\\windows\\tools\\bake-navigation.cmd" '
        'DestinationFolder="$(OutDir)" SkipUnchangedFiles="true" />')
    out("  </Target>")
    out("</Project>")
    write_if_changed(PROJECT_DIR / "halo.vcxproj", "\n".join(lines) + "\n")

    # the Solution Explorer's folders: the sources' own
    filters: List[str] = []
    seen: List[str] = []
    entries: List[Tuple[str, str]] = []
    for include, source, _, _ in units:
        folder = str(PureWindowsPath(source.parent))
        entries.append((include, folder))
        parts = PureWindowsPath(folder).parts
        for depth in range(1, len(parts) + 1):
            name = str(PureWindowsPath(*parts[:depth]))
            if name not in seen:
                seen.append(name)
    entries.append((generated_c, "build"))
    entries.append((generated_fonts_c, "build"))
    if "build" not in seen:
        seen.append("build")
    filters.append('<?xml version="1.0" encoding="utf-8"?>')
    filters.append('<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')
    filters.append("  <ItemGroup>")
    for name in sorted(seen):
        digest = hashlib.sha1(name.encode("utf-8")).hexdigest()
        guid = f"{{{digest[:8]}-{digest[8:12]}-{digest[12:16]}-{digest[16:20]}-{digest[20:32]}}}"
        filters.append(f'    <Filter Include="{escape(name)}">')
        filters.append(f"      <UniqueIdentifier>{guid}</UniqueIdentifier>")
        filters.append("    </Filter>")
    filters.append("  </ItemGroup>")
    filters.append("  <ItemGroup>")
    for include, folder in entries:
        filters.append(f'    <ClCompile Include="{escape(include)}">')
        filters.append(f"      <Filter>{escape(folder)}</Filter>")
        filters.append("    </ClCompile>")
    filters.append("  </ItemGroup>")
    filters.append("</Project>")
    write_if_changed(PROJECT_DIR / "halo.vcxproj.filters", "\n".join(filters) + "\n")

    # (the solution's names for the platforms: x86 for the project's Win32)
    names = [("x86" if platform == "Win32" else platform, platform) for platform, _ in PLATFORMS]
    solution_platforms = "".join(f"\t\t{configuration}|{name} = {configuration}|{name}\n"
                                 for name, _ in names for configuration in ("Release", "Debug"))
    project_platforms = "".join(
        f"\t\t{PROJECT_GUID}.{configuration}|{name}.ActiveCfg = {configuration}|{platform}\n"
        f"\t\t{PROJECT_GUID}.{configuration}|{name}.Build.0 = {configuration}|{platform}\n"
        for configuration in ("Debug", "Release") for name, platform in names)
    solution = f"""
Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
VisualStudioVersion = 17.0.31903.59
MinimumVisualStudioVersion = 10.0.40219.1
Project("{VC_PROJECT_TYPE}") = "halo", "halo.vcxproj", "{PROJECT_GUID}"
EndProject
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
{solution_platforms}	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
{project_platforms}	EndGlobalSection
	GlobalSection(SolutionProperties) = preSolution
		HideSolutionNode = FALSE
	EndGlobalSection
	GlobalSection(ExtensibilityGlobals) = postSolution
		SolutionGuid = {SOLUTION_GUID}
	EndGlobalSection
EndGlobal
"""
    write_if_changed(PROJECT_DIR / "halo.sln", solution)
    kept = {platform: len(units) + 2 - sum(1 for _, source, _, _ in units if source.as_posix() in excluded[platform])
            for platform, _ in PLATFORMS}
    print(", ".join(f"{count} units for {platform}" for platform, count in kept.items()) + f" in {PROJECT_DIR / 'halo.vcxproj'}")


if __name__ == "__main__":
    main()
