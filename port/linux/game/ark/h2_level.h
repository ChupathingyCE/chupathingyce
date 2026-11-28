/*
H2_LEVEL.H

A Halo 2 level played by this engine (h2_level.c): the player's own retail
Xbox Halo 2 map (01a_tutorial.map, with shared.map and
single_player_shared.map beside it) made, on the player's machine, into a
cache file of this engine's own kind - its structure BSP's render geometry
and collision (the instanced geometry's too), its lightmaps, its bitmaps,
its shaders as environment shaders, a sky - with the player, his weapons
and the game's globals taken from the player's own Halo campaign map
(a10.map) by a scene pack (scene_pack.h). The engine then loads that file
as any map. Nothing of either game is shipped: the file is made from the
player's files, in t:\h2, and made again when they change.
*/

#ifndef __H2_LEVEL_H
#define __H2_LEVEL_H

#include "cseries.h"
#include <stdio.h>

/* ---------- constants */

enum
{
	H2_LEVEL_WHY_SIZE = 192,
	/* the level's parts (Nov 3: run Setup once): each part has a version of
	its own, and only a part whose version is not this game's is made again -
	  the world: the level's .map (tags, BSP, bitmaps, lightmaps) and its HUD
	    file (.h2hud, whose sheets are bitmaps in the .map):
	    H2_LEVEL_BUILDER_VERSION, with H2_WEAPONS_/OBJANIM_/MATERIALS_BUILDER_VERSION,
	    the effects' and lighting's keys; H2_HUD_FILE_VERSION. A level whose
	    world version is from H2_LEVEL_WORLD_OLDEST to this one is current.
	    Stale: made again (a multiplayer map on the device from MCC's
	    textures.dat; a mission by Setup on the PC);
	  the menu: .h2menu (and the main menu's .h2art) - H2_MENU_FILE_VERSION
	    (h2_menu.h); stale alone, made again alone in seconds, on any machine;
	  the kit (h2kit.pack), the emblems (emblems.h2emblem), the texture
	    sidecar (.texcache) and the sound cache: each checked on its own, as
	    before.
	RULE: bump a part's version only when its built bytes must change. A fix
	the game can make when it loads the level (from the made data, or MCC's
	map beside it) is made there and bumps nothing. When the world's bytes
	change, set H2_LEVEL_WORLD_OLDEST to the new version too; when a bump
	changed another part's bytes only, bump that part and leave the world's
	oldest where it was */
	/* the builder's version: a file made by another is made again */
	H2_LEVEL_BUILDER_VERSION = 308 + 5000 + 100 * 1,   /* (308 (h2/render-accuracy, on the desktop: the opaque templates' vertex constants read in the order their passes name them - tex_bump_env's bump scale, tex_bump_alpha_test's detail, bump and base, every row's base scale - and the detail maps their passes draw)) (307 (h2/render-accuracy, on the desktop: tex_bump_env_illum_combined's constants read as its own passes order them - detail, base, bump; tints 1 and 2, brightnesses 3 and 4, specular 5 and 6)) (306 (h2/render-accuracy, on the desktop: tex_illum_bloom's glow in its first pixel constant, its self_illum_color; a glow Halo 2 adds flagged in the material extension, 0x0200; sprite decals sized by the decal's maximum sprite extent, Halo 2's pixels per unit)) (305 (h2/render-accuracy: a level shader with no bump map of its own given a flat one, MCC's monochrome cube maps made 32-bit - on the desktop)) (304 (h2/render-accuracy: a Halo 2 environment shader's texture animation function zero - no offset of 1 under its detail and bump maps - on the desktop)) (303 (h2/render-accuracy: the sky's BLOOM OVERRIDE gamma power in the built sky's padding, 0x30 - h2_bloom.c's frame gamma)) (302 (h2/render-accuracy: a Halo 2 shader's own lightmap specular brightness in its extension's specular colour, flag 0x0020; its template's specular mask, flags 0x0040 0x0080 0x0100)) (300 (h2/render-accuracy: water_pool and water_pool_cube on Halo 2's water_pool pass - the shader's own reflection and fresnel maps, no sun glitter)) (299 (h2/render-accuracy: the desktop's lightmap pages whole, not halved - the lighting build key keys it too)) (298 (h2/render-accuracy: the world from the map lights an instance with no lightmap page by its bucket's baked colours - their mean a patch)) (297 (h2/render-accuracy: tex_bump_three_detail_blend's details b and c as the secondary and micro detail maps, the extension's three detail flag)) (296 (h2/render-accuracy: two Halo 2 objects of one short name made two names - the second its folder after it; lights' brightness animations)) (295 (h2/render-accuracy: an environment shader's reflection constants Halo 2's own env tints and brightnesses, the pass draws them as Halo 2's lightmap_tex_env)) (294 (h2/render-accuracy: the desktop's world from the map gets Halo 2's every decorator quad, no thinning)) (293 (h2/render-accuracy: a decorator's light Halo 2's - its permutation's tint times the ground's colour and the lightmap's by their shares)) (292 (h2/render-accuracy: a placed scenery's Halo 2 unique id at its permutation's 0x44, its model's parts' Halo 2 sections and vertex places - 'h2bk' at 0x44 - for Halo 2's baked scenery light)) (291 (h2/render-accuracy: tex_bump_detail_blend_detail's overlay detail as the micro detail map, twice it)) (290 (h2/render-accuracy: a sky_one_add_illum_detail sky layer the detail times its mask's colour AND alpha, Halo 2's pass)) (289 (fix/h2-ai-audit-12: a machine carries its Halo 2 bounding radius and offset - a door looks for units in that sphere; one-sided for player placement bit; a machine or control placement its Halo 2 scale at 0x38)) (288 (fix/h2-ai-audit-11: every level object with a Halo 2 shield carries it - 06a's absorbers too, not only the characters' child objects; its shield damaged shader the modifier shader, by its current_shield_damage function)) (287 (fix/h2-ai-audit-10: shielded level objects - the Enforcer's frontal shields - their Halo 2 shield, stun, recharge and shield effects)) (286 (fix/h2-ai-audit-9: the characters' model variants' child sceneries made - the Enforcer's frontal shields)) (285 (fix/h2-object-lighting: the PRT vertices' baked lightmap colour R G B A, its palette index over 256) 284 (fix/h2-object-lighting: PRT models carry Halo 2's CPCA - each vertex its cluster and PCA weights, the model its basis at 0x38 - lit by the lighting environment at the object, 9 coefficients)) (283 (fix/h2-audit-batch: every vehicle seat's Halo 2 camera track a tag of its own, the bipeds' follow camera, the Banshee's canopy overlay from combat:closing)) (282 (perf/h2-vita-textures-full: a level too big for the tag cache with its world's whole room keeps its world's geometry apart)) (281 (perf/h2-vita-textures-full: the Vita tier's bitmaps Halo 2's full size, its multiplayer bump maps real, weapons' maps as they are, no half-size masks)) (280 (perf/h2-vita-textures-full: the Vita tier's models Halo 2's full detail - scenery L6, the bipeds' level cutoffs from their own models)) (279 (fix/h2-cutscene-visuals-4: the Vita keeps 64 character nodes, face pairs kept together, hologram twins of the vehicles in a level with a hologram)) (278 (fix/h2-cutscene-visuals: images picked by the random constant)) (277 (fix/h2-cutscene-visuals: texture camera screens, unseen marker-only objects)) (276 (fix/h2-cutscene-visuals: the campaign Chief carries no stand-in rifle)) (275 (fix/h2-pause-fonts: the pause menu built with all of Halo 2's fonts)) (274 (fix/h2-armory: placement variants, e.g. Cairo's destroyed door; campaign player N at starting location N)) (273 (fix/h2-elite-flight): only a biped that flies stands in its flight mode - Elites walked as jetpack Elites; 272 (fix/h2-sword-spawn; 271 was taken by integration/1.2.0-insider.3: animation_make, texture cap, District): a netgame item placed with Halo 2's three angles, the energy sword tip up) (270 (fix/h2-vita-hud-batch): PC-built levels for the Vita take the Vita tier content; 269 (fix/h2-map-parity): the classic tier lightmap times 2.4; 268 (fix/h2-map-parity): the sea sky map bakes cloud layers alone; 267 (fix/h2-map-parity): three_detail_blend its detail a alone; 266 (fix/h2-map-parity): detail maps as they are on every tier; 264 (fix/h2-vehicle-fx r5); 263 (h2cine r6); 262 (fix/h2-map-parity r4); 259 (feat/h2-campaign-ai r4); 258 (fix/h2-map-parity r3); 257 (h2cine r5); 256 (fix/h2-vehicle-fx r3); 255 (fix/h2-collision-align): machine frame ratio, touch kill by scale; 252: a weapon's two accuracy patterns; 251: Jackal and Brute gun markers; 249 (merge: per-character damage tables); 248: rifle classes hold csr and cb; 247: a sniper does not hide; 246: a weapon with no character entry borrows none; 245: fliers' 3D encounters, Drones' flight mode; 244 (merge), 243: a biped's plasma_mask_offset shaders the level's pass; 242: patrol mode as the alert seat; 241: damage sections that ignore shields, a Jackal's; 240 (fix/h2-collision-align): instance collision turned as drawn, every instance placed, force model string v12; 239 (fix/h2-vehicle-fx r2): material families, water coverage, one splash a shot; 238 (feat/h2-campaign-ai): AI rank upgrades, legendary characters; 237 (h2cine): a scenery's skinned sections read, nodes kept for scenery; 236 (feat/h2-campaign-11): skulls built, head_sp, weapons 118; 235 (fix/h2-map-parity): decorator quads, water sheets by area, level objects, 231-234 of that lane; 230 (fix/h2-vehicle-fx): runtime collision materials, water fog planes, breakable panes, the Banshee's change colours; 229 (h2saves): the menu file v18, CAMPAIGN OPTIONS' Resume; 228 (h2physics r5): the vehicles' force model string v11 - their rigid hull from the physics model, h2_jolt_hull.c; 227 (fix/h2-sky-weather): fog planes only in designated clusters; 226: postgame words in the HUD file; 225: change-colour masks for models with no reflection; 224: decals merged; 223 (fix/h2-sky-weather): the skies' suns - Halo 2's sky lights with a lens flare as this engine's sky lights; 222: mapfx r9 - the fusion coils' plasma self-illumination; 221: mapfx r9 - one_alpha_env on Halo 2's pass, on by default; 220: pools back off the water pass (9dc7f5da left behind: too brown); 219: mapfx r9 - the sky's bloom override in the built sky's padding; 218: 1.2.0 merged into mapfx; 217 (h2small): the gate hold prompt; 217 (mapfx r8): pools on the water pass; 216: mapfx r7 - Halo 2's plasma_shield pass in the shield and powerup plasma shaders' padding; 215: the sea, sky reflection map, fog floor; 213: h2bugs - the damage effects' table on Halo 2's crates; 212: plasma_mask_offset and active-camo plasma passes; 211: emblems - 207: h2emblem merged on 206 - 204 (h2emblem): multiplayer armours' secondary colour pass marked; 210: mapfx - 207 (h2mapfx r4): plasma_alpha shaders a three-map chicago for the PC's plasma pass (extra flag 14), the Vita's glow as before; 209: hud r4 - 207 (fix/h2-hud-11 r4): the scoreboard's bomb_defusal meter and territory meter widgets carried, their bitmaps made; 208: 207 (h2physics r3): the vehicles' force model string v10 - their collision damage limits (obje 0x6C..0x8C), h2_collision_damage.c; 207 (h2glow): a vehicle shader's fade source from its parameters' inputs through the vehicles' functions, kept by two-map shaders - an empty Ghost's plasma vents out; 206: h2mapfx merged - the water definitions' shaders' BSP surfaces left out; 205: h2hud r2 merged - 203 (fix/h2-hud-11; 204: h2sound merged - 202 (h2sound): the structure BSPs' sound environment palettes and the clusters' entries, H2_SOUND_ENVIRONMENT_TAGS empty sound environment tags - the clusters' background sounds (the ambience bed) play, with Halo 2's reverbs; 203: h2player merged - 202 (h2player): the kit globals' player information Halo 2's - run and sneak speeds and accelerations, airborne acceleration, stun penalties; the Spartan's and Elite's turning speed, turning threshold and crouch transition time Halo 2's; 202 (h2maplogic11): a trigger volume of the scenario's kill triggers marked (0x2C), h2_scripts.c enables them as Halo 2 does; 201 (h2dmgshd): two_alpha_env_multichannel glass made Halo 2's multichannel pass, alpha blended - the Warthog's broken windshield no longer magenta; 200 (h2hud): the HUD file v8, the ammunition meters' rounds; 199 (h2terr): a placement's Halo 2 game type Territories (8) kept, h2_rules.c reads it for a Territories game; 198 (h2gt): a king hill's points the corners of their hull, at most the engine's 12 - not the first 12 of a longer outline; 197 (h2crane): a node turned in its bind pose keeps its rigid section's vertices the model's - the main crane's arm and link drawn where their collision is; 196: play 195 + h2turrets 195 merged - h2turrets: the Wraith's mortar and the Spectre's plasma gun their collision BSPs on their nodes, their child nodes; 195 (h2scripts): a multiplayer level's scripts, its trigger volumes and object names, its placements' names and "not automatically"; 194 (h2core): the vehicles' turrets their Halo 2 child objects' damage seats and nodes; 193 (h2core): Halo 2 vehicles their own acceleration scale, the death push its cone angles; 192: T6 round 10 merged on h2engine/play - the menu file v14, the virtual keyboard / error dialog / live game browser screens; 191: r03a 8f8464f2 81958031 cbf74a3c cbc08753 - sky/decal tints, per-vertex PRT, Cortana, merged on h2engine/play; 190: play 185 (h2ai throne seats) + s9 189 merged; 189 (s9): the level select's description slot; 188 (s9): the lobby's player list skin; 187 (s9): the menu file v13, the panes' buttons (the lobby's); 186 (s9): the menu fonts' glyphs their left bearing right, the levels' map shots; 185 (s9): the menu file v12, the attract screen (start_screen); 183 (r03a r16): two_add_clouds cloud x mask x2 added; 182 (r03a r16): the Spectre's power lights too; 181 (r03a r15): driver_seat_power dashes faded by A out; 180 (r03a r15): two_alpha_env_detail alpha blended, opacity 0 hidden; 179 (r03a r15): bipeds own an overshield plasma shader; 178 (r03a r14): one_add_illum tinted by its colour; 177 (r03a r14): two_add_env_illum tinted by its self-illumination colour; 176 (r03a r14): overlays a double multiply, tinted by their colour; 175 (r03a r10): two_add_detail_masked the base times twice its detail; 174 (r03a r10): opaque\\overlay drawn - its base map, alpha tested; 173: h2/characters 874505fc 59a8df3d - damage regions from damage sections, Sentinels' built-in guns, merged on h2engine/play; 172 (r03a r7): PRT models flagged, the BSPs' default lighting their lighting environment; 171: h2ai df478cef - Sentinel/Enforcer weapon stand-ins in the actor variants, merged on h2engine/play; 170 (r03a r5): the meters' smet, one_alpha_env glass sgla opt-in; 169: play 168 + r03a 168 merged - r03a 167-168: glow maps over 256 averaged down, bipeds' illum templates glowing, the Elite glow by the shields; 168: fix/h2-vehicle-damage a9c1706f - one vehicle for an empty-vehicle location, merged on h2engine/play; 167: play 166 + h2chars 161 merged - 161 (h2chars): the PC keeps every node of a character up to 64, drawn by part palettes; 160 (h2chars): node scale, holograms, face nodes, the Enforcer vehicle placed as a biped; 166: play 165 + r03a 165 merged - r03a 165: play 164 + r03a 160 (the bipeds' self-illumination, change colour round 2, space skies round 2); 165: fix/h2-vehicle-damage bfaa28fc - empty vehicle locations placed, t6 cb3e02c7 level names, merged on h2engine/play; 160 (h2chars): node scale, holograms, face nodes, the Enforcer vehicle placed as a biped; 159: play 158 + h2chars 151 merged - 151 (h2chars): every campaign character a biped of its own - the cutscenes' people, the Flood, Sentinels, Heretics, the Monitor -, the campaign player the Master Chief / Arbiter; 158 (s9): the menu images named by their folder too; 157 (s9): the list skins' scroll arrows, the UI's error sound; 156 (s9): the menu file v9, the screens' element animations; 155: play 154 + s9 152 merged - 152 (s9): the multiplayer maps in their files' sort order; 151 (s9): each level's menu file the titles' fonts; 153: h2ai 2b662ad2 Jackal/Hunter shield collision material + fix/h2-vehicle-damage 8fc03ccd e47aa1d2, merged on h2engine/play; 152 (t5): 1024 script nodes kept free for the console; 151: t5 round 2 merged (attached trigger volumes kept) - 150: play 148 + s9 149 merged - 149 (s9): the lobby versions of the game and map select, the maps' pictures; 147 (s9): the menu file v8, CAMPAIGN OPTIONS; 148: h2ai/squad-vehicles - dropships, placement scripts, merged on h2engine/play; 147: T6 3098c176 - effect location names written whole (deterministic builds for the H2 build hash), merged on h2engine/play; 144: fix/h2-vehicle-damage b8359040 - campaign turrets placed, merged on h2engine/play; 143: r03a merged - 142 (r03a): each BSP its own fog planes and planar fogs; 141 (r03a): fog planes only in designated clusters, skies their own atmospheric fog; 138: fix/h2-vehicle-damage - netgame placements by classification, merged on h2engine/play; 137: play 134 + s9 136 merged - 136 (s9): the main menu's haze its sky's colour; 135 (s9): the menu file v6, each level's file its pause (shared.map's), the multiplayer maps by name, the language pack's top bit; 134 (s9): the menu file v5 - the game shell background, button keys, the fonts' button icons; 134: the Halo 2 levels' own decals, fix/h2-zanzibar-generator 04851c52, merged on h2engine/play; 133: play 132 + s9 129 merged - 129 (s9): camera points of the second layout read at 0x28; 128 (s9): the menu file v4, the scene's grade; 126: play 125 + s9 121 (the menu file v3) merged; 122: T6 - the header records the tag cache room the level needs at 0x730, merged on h2engine/play; 121: S6 no longer writes the object names and trigger volumes h2_level.c writes; +5000: h2engine/preview, the five branches merged; +100 a preview bump; 73: detail mips faded; 74: neutralized DXT1 blocks kept four-colour; 75: rigid parts placed by their node's transform, not its inverse; 76: plasma shaders their mask's glow; 90: player physics - jump, landings, slopes, look, falls; 91: both merged; 92: Halo 2's damage model, h2_damage.c; 93: Halo 2's netgame flags, game types, starting equipment, respawn rules and powerup times; 94: Halo 2's weapon type overlays, the seats' grip, ghost_b_l; 95: Halo 2's overshield placed, powerup_super_shield; 96: an empty cluster's empty subcluster, District's BSP drawn; 95 (s6): the campaign levels' scripts, h2_scripts.c; 97: s4 + s6 merged; 97 (s1): skies by their templates, the sky model grown about the camera; 98: s1 merged; 99: s2 + s3 merged - the vessel sounds, phantoms, vehicles; 95 (s5): a campaign level's every structure BSP, its switch volumes, its objects placed in their BSPs; 100: s5 merged; 98: tex_alpha_test cut-outs from the base map (s1); 101: s1 merged; 102: s8 merged - the campaign characters' bipeds; 103: s6 round 2 merged - scripts' effects, sounds, object_hide, kill volumes; 100 (s1): teleporters, the shore's waves; 104: s1 merged; 102: sky_two_add_clouds doubled (s1); 105: s1 merged; 106: s6 round 3 merged - script text, cinematics; 106: s1 round 1 merged - pools their own cube and tint (s1); 107: s1 merged; 108: s8 merged - the campaign AI's tags; 109 (s9): the main menu file, env cut-outs, cloud masks, the menu's plasma screens; 110: s9 merged; 107: rigid sections already in the model's space, the teleporters' three-band energy, pools' fresnel tints (s1); 111: s1 merged; 108: transparent maps' rotation scale a whole turn (s1); 112: s1 merged; 113: s8 c12dcd1d - squads written guarding; 110: the teleporters' column its mask's red (s1); 114: s1 merged; 115: s1 deaf2bd6 - the teleporter column's glow; 116: pools a quarter reflection along; 117: the sea's cluster marked 'h2wa', drawn always; 118: soft plasma beams, illum_3_channel glow, 8-bit maps decoded; 119: water writes no depth; 118 (s8): the characters' placement variants, Brutes, Tartarus and the Prophets (s8); 120: s8 merged; 123: objects' Halo 2 functions named, script-driven (s1); 124: s1 merged; 119 (s8): skinned vertices of type 8 (the Jackal's body), the Hunter's cannon (s8); 125: s8 merged; 122 (t4): its starting profiles, its player Halo 2's Spartan or Elite, the HUD's waypoint arrows (t4); 127: t4 merged; 124: skies' map animations from their tags (s1); 128: s1 merged; 124 (t4): profiles' rounds total less the loaded (t4); 129: t4 merged; 121 (t3): the music's sound tags and the looping sounds, h2_sound.c (t3); 130: t3 merged; 125: turned rigid sections by the bounds rule alone (s1); 131: s1 merged; 125: s8 merged (s9); 132: s9 merged; 132: s9 merged (anims); 139: anims merged; 132: s9 merged (s9); 140: s9 merged; 132: s9 merged (s9); 141: s9 merged; 141: s9 merged (render03a); 146: render03a merged; 146: render03a merged (s9); 154: s9 merged; 154: s9 merged (s9); 164: s9 merged; 164: s9 merged (chars); 184: chars merged) */
	/* the oldest world version this game takes as its own (above): 275 was the
	pause menu's fonts, the menu's bytes alone (H2_MENU_FILE_VERSION 19), so a
	level made at 274 is current but for its menu, made again alone */
	H2_LEVEL_WORLD_OLDEST = 289 + 5000 + 100 * 1,
	/* the parts a level made now leaves to load time (feat/h2-no-rebuild-switch,
	Nov 4 2026): each read from the player's MCC map as the level loads, nothing
	of it in the made level, on the way to no made level at all (the builder
	goes once every part is made at load: feat/h2-level-at-load). Each has a
	switch (HALO_H2_<PART>: "1" on, "0" off, h2_level_switch) and its default
	here, one line a part: flip one when its lane has proven it (the level says
	for itself which parts it carries - the header's 'h2lt' ranges, 'h2kt'
	record and BSP room -, so a level made before a flip plays as it was). A
	part off by default is not yet proven on 1.2.0 (PLAN.md) */
	H2_LEVEL_WORLD_FROM_MAP_DEFAULT = 1,    /* HALO_H2_WORLD_FROM_MAP: the world (feat/h2-world-direct, merged) */
	/* HALO_H2_MODELS_AT_LOAD: bipeds', weapons', vehicles', objects' models (steps
	5-7, merged). OFF until proven: on 1.2.0 e48854ad4 the tick-hash A/B of a level
	with its models at load against one with them built in is IDENTICAL on Lockout
	but DIFFERENT on Zanzibar (first person weapons from tick 19, effects, objects
	from 383; the world at load alone is IDENTICAL there), and on the Vita the
	first-person weapon does not draw on the at-load Lockout, and Cairo's makers
	run out of the 44 MB tag cache (the lead / PLAN.md, Nov 4). The models lane's;
	flip this 0 to 1 when the first-person weapon fix lands and the A/B holds */
	H2_LEVEL_MODELS_AT_LOAD_DEFAULT = 0,
	H2_LEVEL_KIT_AT_LOAD_DEFAULT = 0,       /* HALO_H2_KIT_AT_LOAD: the kit's tags filled from h2kit.pack (A/B of this lane pending) */
	H2_LEVEL_HUD_AT_LOAD_DEFAULT = 1,       /* HALO_H2_HUD_AT_LOAD: the HUD's widgets and sheets from the map (piece C, no .h2hud) */
	/* HALO_H2_MENU_AT_LOAD: the pause menu, the main menu's screens and the
	lobby's art made from the map as they are wanted, their bitmaps textures
	(feat/h2-direct-hud-menu: no .h2menu/.h2art) */
	H2_LEVEL_MENU_AT_LOAD_DEFAULT = 1,
	/* HALO_H2_DIRECT: the level made as it loads with NO level file (DESIGN.md,
	feat/h2-level-at-load piece B): the tags written into the tag cache by
	the builder's own writers, the kit's data read from h2kit.pack, the BSP
	stubs from memory; its bitmaps' pixels from textures.dat and made again
	on demand (piece J). ON: the level builder is gone (piece I) */
	H2_LEVEL_DIRECT_DEFAULT = 1,
	/* the made level's header (CACHE_HEADER_SIZE, 0x800): this builder's
	fields 0x700-0x734 (0x704 the world's stamp), 0x738 the shared library's
	identity (feat/h2-globals-direct), and the part table at 0x740: 'h2pt',
	a count, then up to H2_LEVEL_PART_SLOTS (part, version) pairs - what each
	part was made at. Levels made before it have none (zeros): their world's
	stamp at 0x704 and each part file's own version say */
	/* (piece G, DESIGN.md) the engine's Halo 2 make version: what the engine
	makes of a Halo 2 map - at load, or in a level file until the builder goes -
	as the network identity and compatibility name it (h2_level_network_identity,
	h2_level_network_compat) in place of the builders' versions, which go with
	the builder. Bump it when what the make gives a game that plays changes
	(what two machines in one game must share) */
	H2_MAKE_VERSION = 1,
	H2_LEVEL_HEADER_LIBRARY_IDENTITY = 0x738,
	H2_LEVEL_HEADER_PART_TABLE = 0x740,
	H2_LEVEL_PART_TABLE_SIGNATURE = 0x74703268,   /* 'h2pt' */
	H2_LEVEL_PART_SLOTS = 7,   /* (0x740 + 8 + 7 * 8 = 0x780) */
};

/* the level's parts (h2_level.h above): their ids in the part table */
enum h2_level_part
{
	_h2_level_part_world = 1,      /* the .map: H2_LEVEL_BUILDER_VERSION */
	_h2_level_part_menu,           /* .h2menu/.h2art: H2_MENU_FILE_VERSION */
	_h2_level_part_hud,            /* .h2hud: H2_HUD_FILE_VERSION (made with the world) */
	_h2_level_part_library,        /* the shared library (feat/h2-globals-direct): its version */
	NUMBER_OF_H2_LEVEL_PARTS
};

/* a made level's header: what `part` was made at (its table's), 0 if not said */
unsigned long h2_level_header_part_version(
	unsigned char const *header,
	short part);

/* Halo 2's own folder, beside Halo CE's (d:\\ is the data root:
ux0:data/haloce-vita on the Vita): its map files and sound banks in
halo2\\maps, the levels this machine (or the install helper) made in
halo2\\levels. Written only there; read from the old places too, as cards
set up before (Nov 2026) have them: the levels in t:\\h2 (saves/t/h2), the
maps in the Halo CE maps folder. Halo 3 gets halo3\\ the same way */
#define H2_LEVEL_FOLDER "d:\\halo2\\levels"
#define H2_LEVEL_FOLDER_OLD "t:\\h2"
/* (a campaign level the Vita does not make, h2_level_cache_path: the
reason, after the level's title in the message the player sees,
cache_files.c) */
#define H2_LEVEL_NOT_SET_UP "isn't set up on this Vita: run Setup on your PC to add it"
#define H2_LEVEL_NOT_SET_UP_FOR_THIS_VERSION "isn't set up on this Vita for this version of the game: run Setup on " \
	"your PC again to update it"
#define H2_MAPS_FOLDER "d:\\halo2\\maps\\"
/* (a scope's screen, h2_weapons.c's h2w_scope_camera_shader) the stand-in
bitmap the renderer draws its copy of the screen in place of (its name
also in rasterizer_xbox_active_camouflage.c: rasterizer_scope_camera_map) */
#define H2_SCOPE_CAMERA_BITMAP "h2\\bitmaps\\scope camera"
/* (a texture camera's screen, h2_level.c's texture_camera_shader_fill) the
stand-in bitmap the renderer draws the texture camera's target in place of
(its name also in rasterizer_xbox_active_camouflage.c:
rasterizer_texture_camera_map) */
#define H2_TEXTURE_CAMERA_BITMAP "h2\\bitmaps\\texture camera"

/* ---------- structures */

struct h2_level_build_result
{
	long tag_count;
	long kit_tag_count;
	long bsp_bytes;
	long tag_bytes;
	long bitmap_bytes;
	long lightmap_bytes;
	long surfaces;
	long vertices;
	long materials;
	long clusters;
	long instances;
	long instance_surfaces;
	unsigned long long microseconds;
	char why[H2_LEVEL_WHY_SIZE];
};

/* ---------- prototypes/H2_LEVEL.C */

/* the folder name.map of Halo 2 is in: Halo 2's own (H2_MAPS_FOLDER), else
the Halo CE maps folder, where cards set up before kept it */
char const *h2_maps_directory_of(
	char const *name);

/* Halo 2's maps folder: its own when it holds shared.map, else the Halo CE
maps folder (the old place) */
char const *h2_maps_directory(void);
/* the folder holding Halo 2's shared file `file` (textures.dat,
sounds_*.dat): `preferred`, else Halo 2's own folder, else the maps folder */
char const *h2_resource_directory(char const *preferred, char const *file);
/* whether Halo 2's maps are MCC's (its mainmenu.map, else shared.map) and
no textures.dat is beside them, in Halo 2's own folder or in the maps
folder: MCC's pictures are all in it, so no Halo 2 level can be made
(a card set up for insider.4 or earlier; Setup copies it) - looked at once */
boolean h2_level_textures_missing(void);
/* (the player's words for it: a level's load refused) */
#define H2_LEVEL_NO_TEXTURES "Halo 2's textures.dat is missing (it holds MCC's pictures): run Setup again, " \
	"or copy textures.dat from MCC's halo2\\h2_maps_win64_dx11 folder into halo2\\maps"

/* Halo 2's levels folder made (d:\\halo2\\levels, its parent first) */
void h2_level_folder_create(void);

/* where a file of the levels folder is read: Halo 2's own folder's, else
the old t:\\h2's when only that has it; written always to H2_LEVEL_FOLDER */
void h2_level_folder_file(
	char const *file,
	char *path,
	long path_size);

/* whether maps_directory/name.map is a Halo 2 map (the maps folder's) */
boolean h2_level_is_halo2_map(
	char const *maps_directory,
	char const *name);

/* the level this machine makes of maps_directory/name.map, as a
multiplayer game names its map (never 0; 0: no such map): two machines'
differ when their levels would */
unsigned long h2_level_network_identity(
	char const *maps_directory,
	char const *name);

/* (map_loaders.c, as any Halo 2 level is resolved) what every Halo 2 level of
this machine shares, made once per card if missing: the levels folder, the
emblems' pack and the kit's pack (h2kit.pack: one for every level, each
level's kit made from it in memory; h2_level.c) - on a thread of its own, the
game thread drawing Halo 2's loading screen (name's) while it takes long.
FALSE if the kit cannot be made */
boolean h2_level_first_start(char const *maps_directory, char const *name);

/* the cache file made from it, path (path_size bytes) its place: made if it
is not there or is of another map or builder; FALSE (and why) if it cannot
be */
boolean h2_level_cache_path(
	char const *maps_directory,
	char const *name,
	char *path,
	long path_size,
	struct h2_level_build_result *result);

/* (the sound loader: h2_cache_segment_memory) a block in the memory window
when it has room (NULL: none, or under 256 KB - then the C heap), and one
let go (the window's or the C heap's) */
void *h2_level_window_alloc(unsigned long size);
void h2_level_window_free(void *p, unsigned long size);

/* this build's Halo 2 compatibility (15 bits, never 0), 0 with no MCC Halo 2
maps in maps_directory: a join's and a listing's (network_wire.h) */
unsigned short h2_level_network_compat(char const *maps_directory);

/* whether name is a plain file name, never a path (a name from another
machine opens nothing outside the maps folder) */
boolean h2_level_name_plain(char const *name);

/* a Halo 2 level's own name (Zanzibar, Ivory Tower, Outskirts...): NULL
if maps_directory/name.map is not a Halo 2 map (h2_menu_make.c) */
char const *h2_level_title(char const *maps_directory, char const *name);

/* a Halo 2 campaign level's own description (the map's strings, English):
TRUE with it in out, FALSE with none there */
boolean h2_level_description(char const *maps_directory, char const *name, char *out, long out_size);

/* the maps folder the last level was asked for from ("" before one) */
char const *h2_level_maps_directory(void);

/* (h2_map_picture.c) the picture of maps_directory/name.map - a Halo 2
multiplayer map's own, or a campaign level's map shot -, B8G8R8A8 pixels
width x height, the caller frees; NULL when there is none, `why` saying
why */
unsigned char *h2_level_map_picture_read(char const *maps_directory, char const *name, boolean campaign, long *width,
	long *height, char *why, long why_size);

/* made, whatever is there (tools and tests) */
boolean h2_level_build(
	char const *maps_directory,
	char const *name,
	char const *output_path,
	struct h2_level_build_result *result);

/* a part's switch (HALO_H2_<PART> above): "1" on, "0" (or another word) off,
unset or empty its default */
boolean h2_level_switch(char const *name, boolean default_on);

/* (h3_level.c) h2kit.pack - the kit a Halo 3 level shares with Halo 2's -
made from the player's Halo CE map if it must be, and opened: *file (FILE *)
and info (struct scene_pack_info *); FALSE (logged) if not */
struct scene_pack_info;
boolean h2_level_kit_pack_open(struct scene_pack_info *info, FILE **file);

/* ---------- a level with no file (HALO_H2_DIRECT: DESIGN.md) */

/* whether a Halo 2 level is made as it loads, with no level file */
boolean h2_level_direct_enabled(void);

/* (piece H) a Halo 2 level's load in steps, for its log and its loading
screen: h2_load_step(NULL) as the level's tags begin ("" another map's: no
steps), then h2_load_step(what)
after each step - its time logged ("h2 load: <what> <ms>"), and a frame of
the loading screen drawn if one is due -; h2_load_draw inside a long step's
loop, between its pieces (never inside a cache's read): a frame drawn if one
is due, a few times a second at most. Both draw only on the loading screen's
own thread while it is up */
void h2_load_step(char const *what);
void h2_load_draw(void);
/* (the same where each piece of the loop may be long: the clock read at each call) */
void h2_load_draw_coarse(void);
/* (map_loaders.c's resolve) the level maps_directory/name.map named, to be
made as it loads: its header made for the loader's slot, path (path_size)
the MCC map's own; FALSE and why if it cannot be */
boolean h2_level_direct_prepare(char const *maps_directory, char const *name, char *path, long path_size, char *why, long why_size);
/* (cache_files_windows.c) the header (CACHE_HEADER_SIZE bytes) of the direct
level prepared for name, if one was */
boolean h2_level_direct_header(char const *name, void *header);
/* (cache_files.c, where the tag data would be read) the direct level's tags
made into the tag cache at tags, room bytes, and its header made whole into
header; FALSE and why if not */
boolean h2_level_direct_make(char const *name, unsigned char *tags, unsigned long room, void *header, char *why, long why_size);
/* (cache_files.c) the tag cache room a direct level's make came to, remembered for its next load */
void h2_level_direct_room_remember(char const *name, unsigned long room);
/* (the cache file thread) size bytes at offset of the direct level's virtual
file into buffer (zeros where it holds nothing) */
boolean h2_level_direct_read(unsigned long offset, unsigned long size, void *buffer);
/* the direct level let go if it is the one named (NULL or "": whichever it is) */
void h2_level_direct_unload_named(char const *name);
/* the direct level let go (the level unloaded) */
void h2_level_direct_unload(void);
/* whether the level loaded now is a direct one */
boolean h2_level_direct_loaded(void);
/* (h2_texture_sidecar.c, the PC tier) the direct level's sidecar entries
(H2_TEXTURE_SIDECAR_ENTRY_SIZE each, in memory): their offsets the texture
source's records of the full-size pixels; FALSE if none */
boolean h2_level_direct_sidecar(unsigned char const **entries, long *count);
/* (HALO_H2_DIRECT_CHECK=1, cache_files.c: a file level's tags read and
relocated) the compare tool: the level made again directly and compared */
void h2_level_direct_check(char const *map_name, unsigned char *tags, unsigned long tag_data_size, unsigned long tag_cache_size,
	int (*file_read)(void *context, unsigned long offset, unsigned long size, void *to));

/* (game_manager.c) a made level's header (its first 0x720 bytes): made by this game at all (its stamp is there), and by this builder */
boolean h2_level_header_is_made(unsigned char const *header);
boolean h2_level_header_stamp_current(unsigned char const *header);

#endif
