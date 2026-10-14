"""The bot lobby is a feature-gated overlay on a copied multiplayer menu row."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def test_dynamic_bot_lobby_row():
    source = (ROOT / "source/interface/ui_widget.c").read_text()
    feature_gate = "#if defined(HALO_FEATURE_BOTS) && defined(HALO_GAME_BROWSER)"

    assert '#include "features/bots/bot_lobby.h"' in source
    assert feature_gate in source
    assert "BOT_MATCH_POSITION = 4" in source
    assert "ONLINE_GAMES_ADDED_ITEMS = 2" in source
    assert "children[BOT_MATCH_POSITION] = old_children[ONLINE_GAMES_SYSTEM_LINK]" in source
    assert "ui_widget_list_position(widget) == BOT_MATCH_POSITION" in source
    assert "bot_lobby_open();" in source
    assert "bot_lobby_pointer(&pointer);" in source
    assert "bot_lobby_render();" in source
    assert "bot_lobby_process();" in source
    assert "BOT MATCH" in source
    assert "Set up a private match" in source


def test_feature_off_keeps_the_single_online_games_row():
    source = (ROOT / "source/interface/ui_widget.c").read_text()
    assert "\n#else\n\tONLINE_GAMES_ADDED_ITEMS = 1,\n#endif" in source
    assert "ONLINE_GAMES_MENU_ITEMS = 4" in source
