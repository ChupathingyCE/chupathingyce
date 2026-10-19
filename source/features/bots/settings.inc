/* Bots' config.toml settings: rows of port_config.c's config_settings
table, included there when the feature is built (tools/features.py,
HALO_FEATURE_SETTINGS_<n>). A setting may be in any section; the file is
written grouped by section. */

	/* computer-controlled players (source/features/bots/bots.c; configure.py --bots) */
	{ "bots.count", _config_integer, "0", "HALO_BOTS", _environment_value, _platform_all,
		"Computer-controlled players (0 to 31) in an offline FFA, Team Slayer or\n"
		"opted-in standard CTF game. Bots are local-only and leave if a remote\n"
		"machine joins. They use the normal player actions; experimental." },
	{ "bots.ctf", _config_boolean, "false", "HALO_BOT_CTF", _environment_value, _platform_all,
		"Opt-in experimental standard two-flag CTF objectives; off by default.\n"
		"Assault, single-flag and manual-reset variants are refused. See\n"
		"docs/bot-objective-modes.md for the implementation and test scope." },
	{ "bots.team_spawns", _config_boolean, "true", "HALO_BOT_TEAM_SPAWNS", _environment_value, _platform_all,
		"In a team game with bots, each team spawns in its own half of the map,\n"
		"nearer its own base. False: Halo's own spawns, where both teams can\n"
		"start in the same base." },
	{ "bots.skill", _config_string, "\"marine\"", "HALO_BOT_SKILL", _environment_value, _platform_all,
		"How well the bots play: \"recruit\", \"marine\", \"odst\" or \"spartan\".\n"
		"Skill changes their reactions, aim and sight, never their health or damage." },
	{ "bots.playfighting", _config_boolean, "false", "HALO_BOT_PLAYFIGHTING", _environment_value, _platform_all,
		"Combat practice mode: excludes melee, grenade and disengage evaluators.\n"
		"Uses ordinary player weapons/damage; it is not an invulnerability mode." },
	{ "bots.combat_fight", _config_boolean, "true", "HALO_BOT_COMBAT_FIGHT", _environment_value, _platform_all,
		"Enable the native-style firing evaluator; movement and goals remain independent." },
	{ "bots.combat_melee", _config_boolean, "true", "HALO_BOT_COMBAT_MELEE", _environment_value, _platform_all,
		"Enable the profile-driven melee evaluator and normal player melee controls." },
	{ "bots.combat_grenade", _config_boolean, "true", "HALO_BOT_COMBAT_GRENADE", _environment_value, _platform_all,
		"Enable the profile-driven grenade evaluator, subject to live safety checks." },
	{ "bots.combat_avoid", _config_boolean, "true", "HALO_BOT_COMBAT_AVOID", _environment_value, _platform_all,
		"Enable bounded observed-grenade/approaching-vehicle avoidance on foot." },
	{ "bots.combat_disengage", _config_boolean, "true", "HALO_BOT_COMBAT_DISENGAGE", _environment_value, _platform_all,
		"Enable confidence-threshold disengaging for profiles that retreat." },
	{ "bots.human_callouts", _config_boolean, "true", "HALO_BOT_HUMAN_CALLOUTS", _environment_value, _platform_all,
		"Share local human POV contacts with teammate bots after one second of\n"
		"continuous sight. Nearby patrol bots also turn toward incoming-fire cues;\n"
		"Team Slayer only, no speech or hidden enemy tracking (docs/bots.md)." },
	{ "debug.bot_decisions", _config_boolean, "false", "HALO_BOT_DECISIONS", _environment_set_is_true, _platform_all,
		"Log each bot's change of behavior with every behavior's utility score\n"
		"and its confidence; off in normal play (docs/bots.md)." },
	{ "debug.nav_probe", _config_boolean, "false", "HALO_NAV_PROBE", _environment_set_is_true, _platform_all,
		"Opt-in BSP navigation walking probe controlling Bot 1 in a solo Slayer game.\n"
		"No teleport or physics bypass; off in normal play." },
	{ "debug.nav_dump", _config_string, "\"\"", "HALO_NAV_DUMP", _environment_value, _platform_all,
		"Diagnostic walkable BSP polygon dump path (derived local data, not a map edit)." },
	{ "debug.nav_goal", _config_string, "\"\"", "HALO_NAV_GOAL", _environment_value, _platform_all,
		"Optional navigation probe goal as x,y,z in CE world units; empty picks a nearby region." },
	{ "debug.map_anchors_dump", _config_string, "\"\"", "HALO_MAP_ANCHORS_DUMP", _environment_value, _platform_all,
		"Diagnostic JSON of the map's anchors (starting locations, netgame flags,\n"
		"weapon and powerup spawns) in world coordinates, each placed on the walkable\n"
		"BSP polygons; the path must end in .json (docs/bots.md)." },
	{ "debug.bot_navigation_only", _config_boolean, "false", "HALO_BOT_NAVIGATION_ONLY", _environment_set_is_true, _platform_all,
		"Navigation test fixture: bots only patrol their team lanes and hold fire\n"
		"(no fighting, retreating or pickups); off in normal play (docs/bots.md)." },
	{ "debug.bot_sandbox", _config_boolean, "false", "HALO_BOT_SANDBOX", _environment_set_is_true, _platform_all,
		"Solo Team Slayer bot test fixture (requires three bots): seat the local\n"
		"player in a parked Warthog and place teammate Bot 2 near the gunner entrance\n"
		"once per map. The bot must approach and enter normally; off in normal play." },
	{ "debug.bot_scenario", _config_string, "\"\"", "HALO_BOT_SCENARIO", _environment_value, _platform_all,
		"Controlled bot behavior tests (docs/bots.md): \"S01,S02\" or \"all\".\n"
		"A walled arena is built above the map (64-bit desktop builds), and each trial\n"
		"places a bot and its scripted targets; a camera follows it. Empty: off." },
	{ "debug.bot_scenario_skills", _config_string, "\"spartan\"", "HALO_BOT_SCENARIO_SKILLS", _environment_value, _platform_all,
		"The skills each scenario runs with, in order: \"recruit,marine,odst,spartan\" or \"all\"." },
	{ "debug.bot_scenario_trials", _config_integer, "1", "HALO_BOT_SCENARIO_TRIALS", _environment_value, _platform_all,
		"Trials of each scenario at each skill (1 to 100); variants take turns." },
	{ "debug.bot_scenario_seed", _config_integer, "1", "HALO_BOT_SCENARIO_SEED", _environment_value, _platform_all,
		"The scenarios' random seed: the same seed gives the bots the same numbers." },
	{ "debug.bot_scenario_log", _config_string, "\"\"", "HALO_BOT_SCENARIO_LOG", _environment_value, _platform_all,
		"The JSON-lines telemetry (every tick of every subject): a .jsonl file in\n"
		"paths.data (absolute paths are taken inside it too); empty for none." },
	{ "debug.bot_scenario_camera", _config_string, "\"follow\"", "HALO_BOT_SCENARIO_CAMERA", _environment_value, _platform_all,
		"How a scenario is watched: \"follow\" (behind the bot), \"overview\" (the whole\n"
		"arena), \"first_person\" (the bot's eyes) or \"off\" (the host's own view)." },
	{ "debug.bot_scenario_origin", _config_string, "\"\"", "HALO_BOT_SCENARIO_ORIGIN", _environment_value, _platform_all,
		"The arena's center \"x,y\" (and the height \"x,y,z\" its ground probes start at);\n"
		"empty uses the map's own (Blood Gulch only)." },
	{ "debug.bot_scenario_crowd", _config_integer, "8", "HALO_BOT_SCENARIO_CROWD", _environment_value, _platform_all,
		"Bots in the crowd scenario, S10 (2 to 24)." },
	{ "debug.bot_scenario_exit", _config_boolean, "false", "HALO_BOT_SCENARIO_EXIT", _environment_set_is_true, _platform_all,
		"Quit the game once every scenario trial has run." },
