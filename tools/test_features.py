"""Manifest selection, disabled-source removal, settings and dispatcher contracts."""
import json
import shutil
import subprocess
from pathlib import Path
from types import SimpleNamespace
from unittest import SkipTest, TestCase

import tools.features as features
from tools import linux_build

ROOT = Path(__file__).resolve().parents[1]


def compiler():
    cc = shutil.which("clang")
    if not cc:
        raise SkipTest("clang required")
    assert cc is not None
    return cc


def test_selection(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    feature_root = tmp_path / "source" / "features"
    alpha = feature_root / "alpha"
    alpha.mkdir(parents=True)
    (alpha / "feature.json").write_text(json.dumps({
        "name": "alpha", "order": 20, "settings": "settings.inc"
    }))
    (alpha / "settings.inc").write_text("")
    (alpha / "alpha.c").write_text("")
    beta = feature_root / "beta"
    beta.mkdir()
    (beta / "feature.json").write_text(json.dumps({
        "name": "beta", "order": 10, "extension": False
    }))
    (beta / "beta.c").write_text("")
    monkeypatch.setattr(features, "FEATURES_DIR", Path("source/features"))

    selected = features.defines(SimpleNamespace(feature_alpha=True, feature_beta=True))
    assert selected == [
        "-DHALO_FEATURE_BETA", "-DHALO_FEATURE_ALPHA",
        "-DHALO_EXTENSION_1=alpha", "-DHALO_FEATURE_SETTINGS_1=alpha",
    ]
    assert features.defines(SimpleNamespace(feature_alpha=False, feature_beta=False)) == []
    assert features.excluded_sources(SimpleNamespace()) == {
        "source/features/alpha/alpha.c", "source/features/beta/beta.c"
    }
    config = {"game": {"root": "source"}}
    assert not any("source/features/" in str(path) for path in linux_build.game_sources(config))
    on = linux_build.game_sources(config, SimpleNamespace(feature_alpha=True, feature_beta=True))
    assert Path("source/features/alpha/alpha.c") in on
    assert Path("source/features/beta/beta.c") in on


def test_bot_feature_selection():
    selected = features.defines(SimpleNamespace(feature_bots=True))
    assert selected == [
        "-DHALO_FEATURE_BOTS", "-DHALO_EXTENSION_1=bots",
        "-DHALO_FEATURE_SETTINGS_1=bots",
    ]
    assert features.defines(SimpleNamespace(feature_bots=False)) == []
    config = {"game": {"root": "source"}}
    enabled = linux_build.game_sources(config, SimpleNamespace(feature_bots=True))
    disabled = linux_build.game_sources(config, SimpleNamespace(feature_bots=False))
    assert Path("source/features/bots/bots.c") in enabled
    assert not any("source/features/bots/" in str(path) for path in disabled)


def test_drop_in(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    module = Path("source/features/experiment")
    module.mkdir(parents=True)
    (module / "feature.json").write_text(json.dumps({"name": "experiment", "settings": "settings.inc"}))
    (module / "settings.inc").write_text("")
    (module / "experiment.c").write_text("")
    assert features.defines(SimpleNamespace(feature_experiment=True)) == [
        "-DHALO_FEATURE_EXPERIMENT", "-DHALO_EXTENSION_1=experiment",
        "-DHALO_FEATURE_SETTINGS_1=experiment",
    ]
    assert features.excluded_sources(SimpleNamespace()) == {str(module / "experiment.c")}
    assert features.excluded_sources(SimpleNamespace(feature_experiment=True)) == set()
    (module / "feature.json").write_text('{"name": "wrong"}')
    with TestCase().assertRaisesRegex(ValueError, "directory"):
        features.discover()


def test_bad_manifest(tmp_path):
    module = tmp_path / "bad"
    module.mkdir()
    (module / "feature.json").write_text("not json")
    with TestCase().assertRaisesRegex(ValueError, "cannot read"):
        features.discover(tmp_path)
    (module / "feature.json").write_text('{"name": "bad", "default": "false"}')
    with TestCase().assertRaisesRegex(ValueError, "boolean"):
        features.discover(tmp_path)


def test_dispatcher(tmp_path):
    # Stand-ins for cseries types, not the game's Xbox ABI: only the dispatcher
    # is under test; platform builds compile it with the real game headers.
    (tmp_path / "cseries.h").write_text(
        "#pragma once\n#include <stddef.h>\n#include <wchar.h>\n"
        "typedef int boolean; typedef float real;\n"
        "#define TRUE 1\n#define FALSE 0\n#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))\n")
    executable = tmp_path / "dispatch_test"
    subprocess.run([compiler(), "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-I", str(tmp_path),
                    "-I", str(ROOT / "source"), "-DHALO_EXTENSION_1=first", "-DHALO_EXTENSION_2=second",
                    str(ROOT / "source/extensions/extension_dispatch.c"),
                    str(ROOT / "tools/tests/extension_dispatch_test.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
    empty = tmp_path / "empty.c"
    empty.write_text('''
#include "extensions/extension_api.h"
#include <assert.h>
int main(void) {
    boolean result = TRUE, reset = TRUE;
    void *proc = (void *)1;
    assert(halo_extensions_count() == 0);
    assert(!halo_extensions_active_ruleset());
    assert(!halo_extensions_should_end_game(&result) && result);
    assert(!halo_extensions_suppress_network_state());
    assert(!halo_extensions_suppress_game_report());
    assert(halo_extensions_can_collect_items(0));
    assert(halo_extensions_damage_multiplier(0, 0) == 1.f);
    assert(!halo_extensions_input_captured());
    assert(!halo_extensions_editor_director_camera(0, &proc, &reset));
    assert(!proc && !reset);
    halo_extensions_objects_placed();
    halo_extensions_update_player_controllers();
    halo_extensions_end_tick();
    return 0;
}
''')
    subprocess.run([compiler(), "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-I", str(tmp_path),
                    "-I", str(ROOT / "source"), str(ROOT / "source/extensions/extension_dispatch.c"),
                    str(empty), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)


def test_world_geometry_dispatcher(tmp_path):
    (tmp_path / "cseries.h").write_text(
        "#pragma once\n#include <stddef.h>\n#include <wchar.h>\n"
        "typedef int boolean; typedef float real;\n"
        "#define TRUE 1\n#define FALSE 0\n#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))\n")
    executable = tmp_path / "geometry_dispatch_test"
    subprocess.run([compiler(), "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-I", str(tmp_path),
                    "-I", str(ROOT / "source"), "-DHALO_EXTENSION_1=first", "-DHALO_EXTENSION_2=second",
                    str(ROOT / "source/extensions/extension_dispatch.c"),
                    str(ROOT / "tools/tests/world_geometry_dispatch_test.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)


def test_settings_sections(tmp_path):
    source = (ROOT / "port/linux/src/port_config.c").read_text()
    declarations = source[source.index("enum config_type"):source.index("/* macOS starts")]
    table_start = source.index("static const struct config_setting config_settings[]")
    table_end = source.index("#ifdef HALO_ANDROID", source.index("#define NUMBER_OF_CONFIG_SETTINGS"))
    table = source[table_start:table_end]
    writer_start = source.index("static char *config_default_text(void)")
    writer = source[writer_start:source.index("/* whether text has", writer_start)]
    feature_settings = tmp_path / "source/features/experiment/settings.inc"
    feature_settings.parent.mkdir(parents=True)
    feature_settings.write_text(
        '{ "experiment.enabled", _config_boolean, "false", NULL, _environment_value, _platform_desktop, "test" },\n')
    # Use the production standard-C headers: injecting assert.h here used to
    # hide its absence in port_config.c, breaking several real platform builds.
    header_block = source[:source.index("#define HALO_SETTINGS_PATH_")]
    standard_headers = "\n".join(line for line in header_block.splitlines()
                                 if line.startswith("#include <") and
                                 not line.startswith(("#include <SDL3/", "#include <pthread.h>")))
    harness = standard_headers + '''
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define DEFAULT_FULLSCREEN "false"
#define DEFAULT_AUDIO_BUFFER_FRAMES "2048"
#define CONFIG_VERSION_TEXT ""
#define CONFIG_PLATFORM _platform_desktop
#define HALO_FEATURE_SETTINGS_1 experiment
#define HALO_SETTINGS_PATH_(name) features/name/settings.inc
#define HALO_SETTINGS_STRING_(path) #path
#define HALO_SETTINGS_STRING(path) HALO_SETTINGS_STRING_(path)
#define HALO_SETTINGS_FILE(name) HALO_SETTINGS_STRING(HALO_SETTINGS_PATH_(name))
'''+declarations+table+'''
struct config_text { char *buffer; size_t size, capacity; };
static char output[131072];
static void config_append(struct config_text *text, const char *value) {
    (void)text; strcat(output, value);
}
static void config_append_setting(struct config_text *text, const struct config_setting *setting) {
    config_append(text, strchr(setting->name, '.') + 1);
    config_append(text, " = "); config_append(text, setting->default_value);
    config_append(text, "\\n");
}
'''+writer+'''
int main(void) { config_default_text(); puts(output); return 0; }
'''
    path = tmp_path / "settings_test.c"
    path.write_text(harness)
    executable = tmp_path / "settings_test"
    subprocess.run([compiler(), "-std=c11", "-Werror=implicit-function-declaration",
                    "-I", str(tmp_path / "source"), str(path), "-o", str(executable)], check=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
    import tomllib
    config = tomllib.loads(result.stdout)
    assert config["experiment"]["enabled"] is False
    assert config["display"]["fullscreen"] is False
