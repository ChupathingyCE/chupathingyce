/* server_stubs.c: what server_messages_fuzz.c does not need of the rest of
the game, each doing nothing and answering 0 (no Direct3D, no game loaded, no
transport, Delta or MCC). Made from the link's missing symbols; one the
handlers need an answer from is in the harness. */

long D3DDevice_SetRenderState_BackFillMode(void);
long D3DDevice_SetRenderState_BackFillMode(void) { return 0; }
long D3DDevice_SetRenderState_CullMode(void);
long D3DDevice_SetRenderState_CullMode(void) { return 0; }
long D3DDevice_SetRenderState_Deferred(void);
long D3DDevice_SetRenderState_Deferred(void) { return 0; }
long D3DDevice_SetRenderState_DoNotCullUncompressed(void);
long D3DDevice_SetRenderState_DoNotCullUncompressed(void) { return 0; }
long D3DDevice_SetRenderState_Dxt1NoiseEnable(void);
long D3DDevice_SetRenderState_Dxt1NoiseEnable(void) { return 0; }
long D3DDevice_SetRenderState_EdgeAntiAlias(void);
long D3DDevice_SetRenderState_EdgeAntiAlias(void) { return 0; }
long D3DDevice_SetRenderState_FillMode(void);
long D3DDevice_SetRenderState_FillMode(void) { return 0; }
long D3DDevice_SetRenderState_FogColor(void);
long D3DDevice_SetRenderState_FogColor(void) { return 0; }
long D3DDevice_SetRenderState_FrontFace(void);
long D3DDevice_SetRenderState_FrontFace(void) { return 0; }
long D3DDevice_SetRenderState_LineWidth(void);
long D3DDevice_SetRenderState_LineWidth(void) { return 0; }
long D3DDevice_SetRenderState_LogicOp(void);
long D3DDevice_SetRenderState_LogicOp(void) { return 0; }
long D3DDevice_SetRenderState_MultiSampleAntiAlias(void);
long D3DDevice_SetRenderState_MultiSampleAntiAlias(void) { return 0; }
long D3DDevice_SetRenderState_MultiSampleMask(void);
long D3DDevice_SetRenderState_MultiSampleMask(void) { return 0; }
long D3DDevice_SetRenderState_MultiSampleType(void);
long D3DDevice_SetRenderState_MultiSampleType(void) { return 0; }
long D3DDevice_SetRenderState_NormalizeNormals(void);
long D3DDevice_SetRenderState_NormalizeNormals(void) { return 0; }
long D3DDevice_SetRenderState_OcclusionCullEnable(void);
long D3DDevice_SetRenderState_OcclusionCullEnable(void) { return 0; }
long D3DDevice_SetRenderState_PSTextureModes(void);
long D3DDevice_SetRenderState_PSTextureModes(void) { return 0; }
long D3DDevice_SetRenderState_RopZCmpAlwaysRead(void);
long D3DDevice_SetRenderState_RopZCmpAlwaysRead(void) { return 0; }
long D3DDevice_SetRenderState_RopZRead(void);
long D3DDevice_SetRenderState_RopZRead(void) { return 0; }
long D3DDevice_SetRenderState_ShadowFunc(void);
long D3DDevice_SetRenderState_ShadowFunc(void) { return 0; }
long D3DDevice_SetRenderState_Simple(void);
long D3DDevice_SetRenderState_Simple(void) { return 0; }
long D3DDevice_SetRenderState_StencilCullEnable(void);
long D3DDevice_SetRenderState_StencilCullEnable(void) { return 0; }
long D3DDevice_SetRenderState_StencilEnable(void);
long D3DDevice_SetRenderState_StencilEnable(void) { return 0; }
long D3DDevice_SetRenderState_StencilFail(void);
long D3DDevice_SetRenderState_StencilFail(void) { return 0; }
long D3DDevice_SetRenderState_TextureFactor(void);
long D3DDevice_SetRenderState_TextureFactor(void) { return 0; }
long D3DDevice_SetRenderState_TwoSidedLighting(void);
long D3DDevice_SetRenderState_TwoSidedLighting(void) { return 0; }
long D3DDevice_SetRenderState_VertexBlend(void);
long D3DDevice_SetRenderState_VertexBlend(void) { return 0; }
long D3DDevice_SetRenderState_YuvEnable(void);
long D3DDevice_SetRenderState_YuvEnable(void) { return 0; }
long D3DDevice_SetRenderState_ZBias(void);
long D3DDevice_SetRenderState_ZBias(void) { return 0; }
long D3DDevice_SetRenderState_ZEnable(void);
long D3DDevice_SetRenderState_ZEnable(void) { return 0; }
long XFindClose(void);
long XFindClose(void) { return 0; }
long XFindFirstNicknameW(void);
long XFindFirstNicknameW(void) { return 0; }
long XSetNicknameW(void);
long XSetNicknameW(void) { return 0; }
long browser_host_gametype(void);
long browser_host_gametype(void) { return 0; }
long browser_host_update(void);
long browser_host_update(void) { return 0; }
long browser_host_workshop(void);
long browser_host_workshop(void) { return 0; }
long byte_swap_message_header(void);
long byte_swap_message_header(void) { return 0; }
long cache_files_map_choose(void);
long cache_files_map_choose(void) { return 0; }
long cache_files_map_installed(void);
long cache_files_map_installed(void) { return 0; }
long cache_files_map_needs_delta(void);
long cache_files_map_needs_delta(void) { return 0; }
long cache_files_precache_map_loaded(void);
long cache_files_precache_map_loaded(void) { return 0; }
long config_changes(void);
long config_changes(void) { return 0; }
long config_integer(void);
long config_integer(void) { return 0; }
long config_string(void);
long config_string(void) { return 0; }
long data_iterator_new(void);
long data_iterator_new(void) { return 0; }
long data_iterator_next(void);
long data_iterator_next(void) { return 0; }
long dedicated_playlist_count(void);
long dedicated_playlist_count(void) { return 0; }
long dedicated_playlist_map(void);
long dedicated_playlist_map(void) { return 0; }
long dedicated_server_active(void);
long dedicated_server_active(void) { return 0; }
long delta_compat_join_marked(void);
long delta_compat_join_marked(void) { return 0; }
long delta_legacy_hold(void);
long delta_legacy_hold(void) { return 0; }
long delta_legacy_host_game_at(void);
long delta_legacy_host_game_at(void) { return 0; }
long delta_peer_advertised_flags(void);
long delta_peer_advertised_flags(void) { return 0; }
long delta_peer_advertised_old_layout(void);
long delta_peer_advertised_old_layout(void) { return 0; }
long delta_peer_advertised_wire(void);
long delta_peer_advertised_wire(void) { return 0; }
long delta_peer_game_host_frame(void);
long delta_peer_game_host_frame(void) { return 0; }
long delta_peer_game_machine_lacks(void);
long delta_peer_game_machine_lacks(void) { return 0; }
long delta_peer_game_machine_legacy_version(void);
long delta_peer_game_machine_legacy_version(void) { return 0; }
long delta_peer_game_machine_pending(void);
long delta_peer_game_machine_pending(void) { return 0; }
long delta_peer_game_stop(void);
long delta_peer_game_stop(void) { return 0; }
long delta_peer_hosting_delta(void);
long delta_peer_hosting_delta(void) { return 0; }
long display_error_text_when_main_menu_loaded(void);
long display_error_text_when_main_menu_loaded(void) { return 0; }
long display_error_when_main_menu_loaded(void);
long display_error_when_main_menu_loaded(void) { return 0; }
long game_connection(void);
long game_connection(void) { return 0; }
long game_connection_set(void);
long game_connection_set(void) { return 0; }
long game_dispose_from_old_map(void);
long game_dispose_from_old_map(void) { return 0; }
long game_engine_bomb_host_traits(void);
long game_engine_bomb_host_traits(void) { return 0; }
long game_engine_get_current_stage(void);
long game_engine_get_current_stage(void) { return 0; }
long game_engine_override_map_name(void);
long game_engine_override_map_name(void) { return 0; }
long game_engine_running(void);
long game_engine_running(void) { return 0; }
long game_engine_update(void);
long game_engine_update(void) { return 0; }
long game_in_progress(void);
long game_in_progress(void) { return 0; }
long game_initialize_for_new_map(void);
long game_initialize_for_new_map(void) { return 0; }
long game_load(void);
long game_load(void) { return 0; }
long game_map_loaded(void);
long game_map_loaded(void) { return 0; }
long game_options_new(void);
long game_options_new(void) { return 0; }
long game_precache_new_map(void);
long game_precache_new_map(void) { return 0; }
long game_set_game_variant(void);
long game_set_game_variant(void) { return 0; }
long game_set_game_variant_options(void);
long game_set_game_variant_options(void) { return 0; }
long game_time_end(void);
long game_time_end(void) { return 0; }
long game_unload(void);
long game_unload(void) { return 0; }
long game_variant_options_default(void);
long game_variant_options_default(void) { return 0; }
long global_network_game_client_get(void);
long global_network_game_client_get(void) { return 0; }
long host_rules_joins_locked(void);
long host_rules_joins_locked(void) { return 0; }
long invite_prompt_ask(void);
long invite_prompt_ask(void) { return 0; }
long invite_prompt_ask_cancel(void);
long invite_prompt_ask_cancel(void) { return 0; }
long late_join_player_added(void);
long late_join_player_added(void) { return 0; }
long main_get_difficulty(void);
long main_get_difficulty(void) { return 0; }
long main_load_ui_scenario(void);
long main_load_ui_scenario(void) { return 0; }
long main_menu_unload(void);
long main_menu_unload(void) { return 0; }
long main_set_difficulty(void);
long main_set_difficulty(void) { return 0; }
long main_set_multiplayer_map_name(void);
long main_set_multiplayer_map_name(void) { return 0; }
long map_family_parse(void);
long map_family_parse(void) { return 0; }
long map_share_fetch_machine_name(void);
long map_share_fetch_machine_name(void) { return 0; }
long map_share_host_downloading(void);
long map_share_host_downloading(void) { return 0; }
long mcc_cache_tags_loaded(void);
long mcc_cache_tags_loaded(void) { return 0; }
long mcc_identity_host_clear(void);
long mcc_identity_host_clear(void) { return 0; }
long mcc_identity_host_overlay(void);
long mcc_identity_host_overlay(void) { return 0; }
long mcc_identity_host_ready(void);
long mcc_identity_host_ready(void) { return 0; }
long mcc_identity_prepare_host(void);
long mcc_identity_prepare_host(void) { return 0; }
long mcc_level_name(void);
long mcc_level_name(void) { return 0; }
long mcc_maps_find(void);
long mcc_maps_find(void) { return -1; }
long mcc_maps_name(void);
long mcc_maps_name(void) { return 0; }
long mcc_main_network_load_failed(void);
long mcc_main_network_load_failed(void) { return 0; }
long mcc_ui_team_request(void);
long mcc_ui_team_request(void) { return 0; }
long mcc_validation_cancel(void);
long mcc_validation_cancel(void) { return 0; }
long mcc_validation_error(void);
long mcc_validation_error(void) { return 0; }
long mcc_validation_lookup(void);
long mcc_validation_lookup(void) { return 0; }
long mcc_validation_request(void);
long mcc_validation_request(void) { return 0; }
long mcc_validation_update(void);
long mcc_validation_update(void) { return 0; }
long mcc_workshop_listing(void);
long mcc_workshop_listing(void) { return 0; }
long mcc_workshop_map_title(void);
long mcc_workshop_map_title(void) { return 0; }
long network_connection_connected(void);
long network_connection_connected(void) { return 0; }
long network_connection_delete(void);
long network_connection_delete(void) { return 0; }
long network_connection_idle(void);
long network_connection_idle(void) { return 0; }
long network_connection_new(void);
long network_connection_new(void) { return 0; }
long network_connection_read(void);
long network_connection_read(void) { return 0; }
long network_connection_server_accept_client_connection(void);
long network_connection_server_accept_client_connection(void) { return 0; }
long network_connection_set_connection_rejection_procedure(void);
long network_connection_set_connection_rejection_procedure(void) { return 0; }
long network_damage_host_tables_allocate(void);
long network_damage_host_tables_allocate(void) { return 0; }
long network_damage_host_tables_free(void);
long network_damage_host_tables_free(void) { return 0; }
long network_distributed_address_full(void);
long network_distributed_address_full(void) { return 0; }
long network_distributed_address_limit(void);
long network_distributed_address_limit(void) { return 0; }
long network_distributed_ban(void);
long network_distributed_ban(void) { return 0; }
long network_distributed_ban_until(void);
long network_distributed_ban_until(void) { return 0; }
long network_distributed_banned(void);
long network_distributed_banned(void) { return 0; }
long network_distributed_host_notice(void);
long network_distributed_host_notice(void) { return 0; }
long network_distributed_kick(void);
long network_distributed_kick(void) { return 0; }
long network_distributed_kick_reason(void);
long network_distributed_kick_reason(void) { return 0; }
long network_distributed_lobby_budget_reset(void);
long network_distributed_lobby_budget_reset(void) { return 0; }
long network_distributed_machine_gone(void);
long network_distributed_machine_gone(void) { return 0; }
long network_game_client_get_game(void);
long network_game_client_get_game(void) { return 0; }
long network_game_client_get_local_machine_index(void);
long network_game_client_get_local_machine_index(void) { return 0; }
long network_game_client_get_machine_index(void);
long network_game_client_get_machine_index(void) { return 0; }
long network_game_get_random_seed(void);
long network_game_get_random_seed(void) { return 0; }
long network_game_is_splitscreen_local(void);
long network_game_is_splitscreen_local(void) { return 0; }
long network_game_player_is_local(void);
long network_game_player_is_local(void) { return 0; }
long network_game_solo_game(void);
long network_game_solo_game(void) { return 0; }
long network_server_allow_client_connections(void);
long network_server_allow_client_connections(void) { return 0; }
long network_server_close_client_connection(void);
long network_server_close_client_connection(void) { return 0; }
long object_iterator_new(void);
long object_iterator_new(void) { return 0; }
long object_iterator_next(void);
long object_iterator_next(void) { return 0; }
long p2p_set_game_listing(void);
long p2p_set_game_listing(void) { return 0; }
long p2p_set_game_player_counts(void);
long p2p_set_game_player_counts(void) { return 0; }
long party_teams_pregame(void);
long party_teams_pregame(void) { return 0; }
long player_delete(void);
long player_delete(void) { return 0; }
long player_new(void);
long player_new(void) { return 0; }
long player_profile_get_random_color(void);
long player_profile_get_random_color(void) { return 0; }
long player_profile_get_random_good_color(void);
long player_profile_get_random_good_color(void) { return 0; }
long player_ui_game_variant_specified(void);
long player_ui_game_variant_specified(void) { return 0; }
long player_ui_get_active_player_profile(void);
long player_ui_get_active_player_profile(void) { return 0; }
long player_ui_get_game_variant_options(void);
long player_ui_get_game_variant_options(void) { return 0; }
long player_ui_local_player_wants_to_play_multiplayer(void);
long player_ui_local_player_wants_to_play_multiplayer(void) { return 0; }
long profile_names_local_game_name(void);
long profile_names_local_game_name(void) { return 0; }
long team_change_late_joiner(void);
long team_change_late_joiner(void) { return 0; }
long transport_get_key(void);
long transport_get_key(void) { return 0; }
long transport_get_key_id(void);
long transport_get_key_id(void) { return 0; }
long transport_network_available(void);
long transport_network_available(void) { return 0; }
long transport_server_initialize(void);
long transport_server_initialize(void) { return 0; }
long transport_server_terminate(void);
long transport_server_terminate(void) { return 0; }
long update_server_handle_client_update(void);
long update_server_handle_client_update(void) { return 0; }
long update_server_next_update(void);
long update_server_next_update(void) { return 0; }
long write_endpoint(void);
long write_endpoint(void) { return 0; }
long delta_legacy_mcc_version(void);
long delta_legacy_mcc_version(void) { return 0; }
long network_distributed_mcc_share_message(void);
long network_distributed_mcc_admission_holding(void);
long network_distributed_mcc_admission_holding(void) { return 0; }
long network_distributed_mcc_admission_message(void);
long network_distributed_mcc_admission_message(void) { return 0; }
long network_distributed_mcc_share_message(void) { return 0; }
long stubbs_mode_active(void);
long stubbs_mode_active(void) { return 0; }
long stubbs_multiplayer_remote_machine_refused(void);
long stubbs_multiplayer_remote_machine_refused(void) { return 0; }
