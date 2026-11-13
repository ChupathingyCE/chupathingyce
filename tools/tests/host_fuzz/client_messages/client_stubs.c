/* client_stubs.c: what client_messages_fuzz.c does not need of the rest of
the game, each doing nothing and answering 0 (no Direct3D, no map loaded, no
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
long cache_files_give_time_to_precache(void);
long cache_files_give_time_to_precache(void) { return 0; }
long cache_files_show_multiplayer_unavailable(void);
long cache_files_show_multiplayer_unavailable(void) { return 0; }
long cancel_connect_process(void);
long cancel_connect_process(void) { return 0; }
long data_iterator_new(void);
long data_iterator_new(void) { return 0; }
long data_iterator_next(void);
long data_iterator_next(void) { return 0; }
long datum_get(void);
long datum_get(void) { return 0; }
long dedicated_server_active(void);
long dedicated_server_active(void) { return 0; }
long delta_compat_join_mark(void);
long delta_compat_join_mark(void) { return 0; }
long delta_compat_opence_server(void);
long delta_compat_opence_server(void) { return 0; }
long delta_legacy_maximum(void);
long delta_legacy_maximum(void) { return 0; }
long delta_legacy_minimum(void);
long delta_legacy_minimum(void) { return 0; }
long delta_peer_advertised_host_wire(void);
long delta_peer_advertised_host_wire(void) { return 0; }
long delta_peer_advertised_speaks_delta(void);
long delta_peer_advertised_speaks_delta(void) { return 0; }
long delta_peer_advertised_wire_matches(void);
long delta_peer_advertised_wire_matches(void) { return 0; }
long delta_peer_game_client_frame(void);
long delta_peer_game_client_frame(void) { return 0; }
long delta_peer_game_host_agreed_mcc_maps(void);
long delta_peer_game_host_agreed_mcc_maps(void) { return 0; }
long delta_peer_game_host_number(void);
long delta_peer_game_host_number(void) { return 0; }
long delta_peer_game_stop(void);
long delta_peer_game_stop(void) { return 0; }
long delta_peer_host_player_limit(void);
long delta_peer_host_player_limit(void) { return 0; }
long delta_wire_compare(void);
long delta_wire_compare(void) { return 0; }
long delta_wire_parse(void);
long delta_wire_parse(void) { return 0; }
long display_error(void);
long display_error(void) { return 0; }
long display_error_text_deferred(void);
long display_error_text_deferred(void) { return 0; }
long display_error_when_main_menu_loaded(void);
long display_error_when_main_menu_loaded(void) { return 0; }
long game_connection_set(void);
long game_connection_set(void) { return 0; }
long game_dispose_from_old_map(void);
long game_dispose_from_old_map(void) { return 0; }
long game_engine_player_added(void);
long game_engine_player_added(void) { return 0; }
long game_engine_running(void);
long game_engine_running(void) { return 0; }
long game_engine_switch_to_postgame(void);
long game_engine_switch_to_postgame(void) { return 0; }
long game_in_progress(void);
long game_in_progress(void) { return 0; }
long game_initial_pulse(void);
long game_initial_pulse(void) { return 0; }
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
long game_time_set_distributed(void);
long game_time_set_distributed(void) { return 0; }
long game_time_start(void);
long game_time_start(void) { return 0; }
long game_unload(void);
long game_unload(void) { return 0; }
long global_network_game_server_get(void);
long global_network_game_server_get(void) { return 0; }
long late_join_player_added(void);
long late_join_player_added(void) { return 0; }
long local_player_get_next(void);
long local_player_get_next(void) { return 0; }
long local_player_set_player_index(void);
long local_player_set_player_index(void) { return 0; }
long machine_remove_player(void);
long machine_remove_player(void) { return 0; }
long main_load_ui_scenario(void);
long main_load_ui_scenario(void) { return 0; }
long main_menu_unload(void);
long main_menu_unload(void) { return 0; }
long main_set_multiplayer_map_name(void);
long main_set_multiplayer_map_name(void) { return 0; }
long map_share_client_message(void);
long map_share_client_message(void) { return 0; }
long map_share_fetch_game_record(void);
long map_share_fetch_game_record(void) { return 0; }
long map_share_fetch_machine_name(void);
long map_share_fetch_machine_name(void) { return 0; }
long map_share_fetching(void);
long map_share_fetching(void) { return 0; }
long mcc_identity_disconnect(void);
long mcc_identity_disconnect(void) { return 0; }
long mcc_identity_wire_record(void);
long mcc_identity_wire_record(void) { return 0; }
long mcc_main_network_load_failed(void);
long mcc_main_network_load_failed(void) { return 0; }
long network_connection_connect(void);
long network_connection_connect(void) { return 0; }
long network_connection_connected(void);
long network_connection_connected(void) { return 0; }
long network_connection_delete(void);
long network_connection_delete(void) { return 0; }
long network_connection_disconnect(void);
long network_connection_disconnect(void) { return 0; }
long network_connection_going_stale(void);
long network_connection_going_stale(void) { return 0; }
long network_connection_idle(void);
long network_connection_idle(void) { return 0; }
long network_connection_keep_alive(void);
long network_connection_keep_alive(void) { return 0; }
long network_connection_new(void);
long network_connection_new(void) { return 0; }
long network_connection_read(void);
long network_connection_read(void) { return 0; }
long network_game_abort(void);
long network_game_abort(void) { return 0; }
long network_game_client_all_local_players_have_quit(void);
long network_game_client_all_local_players_have_quit(void) { return 0; }
long network_game_generate_join_game_token(void);
long network_game_generate_join_game_token(void) { return 0; }
long network_game_get_random_seed(void);
long network_game_get_random_seed(void) { return 0; }
long network_game_is_splitscreen_local(void);
long network_game_is_splitscreen_local(void) { return 0; }
long network_game_player_is_local(void);
long network_game_player_is_local(void) { return 0; }
long network_game_reset_to_pregame_ui(void);
long network_game_reset_to_pregame_ui(void) { return 0; }
long network_game_server_playing(void);
long network_game_server_playing(void) { return 0; }
long network_game_set_random_seed(void);
long network_game_set_random_seed(void) { return 0; }
long object_iterator_new(void);
long object_iterator_new(void) { return 0; }
long object_iterator_next(void);
long object_iterator_next(void) { return 0; }
long p2p_hardware_id(void);
long p2p_hardware_id(void) { return 0; }
long p2p_joining_host(void);
long p2p_joining_host(void) { return 0; }
long platform_show_message(void);
long platform_show_message(void) { return 0; }
long player_delete(void);
long player_delete(void) { return 0; }
long player_new(void);
long player_new(void) { return 0; }
long player_ui_get_active_player_profile(void);
long player_ui_get_active_player_profile(void) { return 0; }
long player_ui_local_player_wants_to_play_multiplayer(void);
long player_ui_local_player_wants_to_play_multiplayer(void) { return 0; }
long profile_names_local_game_name(void);
long profile_names_local_game_name(void) { return 0; }
long transport_client_start(void);
long transport_client_start(void) { return 0; }
long transport_network_available(void);
long transport_network_available(void) { return 0; }
long transport_nonce_is_equal(void);
long transport_nonce_is_equal(void) { return 0; }
long ui_widgets_close_all(void);
long ui_widgets_close_all(void) { return 0; }
long update_client_add_player(void);
long update_client_add_player(void) { return 0; }
long update_server_add_player(void);
long update_server_add_player(void) { return 0; }
long network_distributed_mcc_share_message(void);
long network_distributed_mcc_share_message(void) { return 0; }
long stubbs_multiplayer_join_refused(void);
long stubbs_multiplayer_join_refused(void) { return 0; }
