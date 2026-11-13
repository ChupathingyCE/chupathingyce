/* distributed_stubs.c: what distributed_messages_fuzz.c does not need of the
rest of the game: no map loaded, no objects made or deleted, no sound, HUD
or effect. Made from the link's missing symbols: a function answering 0, one
answering a pointer a zeroed block (no tag, an empty scenario), one
answering a real 0. One the handlers need an answer from is in the harness. */

static long long distributed_stubs_zero[65536];

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
long biped_stop_limp_body_physics(void);
long biped_stop_limp_body_physics(void) { return 0; }
long breakable_surface_extant(void);
long breakable_surface_extant(void) { return 0; }
long breakable_surface_port_break(void);
long breakable_surface_port_break(void) { return 0; }
long build_message_header(void);
long build_message_header(void) { return 0; }
long chat_bridge_message(void);
long chat_bridge_message(void) { return 0; }
long chat_host_console_notice(void);
long chat_host_console_notice(void) { return 0; }
long chat_host_notice_to_machine(void);
long chat_host_notice_to_machine(void) { return 0; }
long cinematic_can_be_skipped(void);
long cinematic_can_be_skipped(void) { return 0; }
long cinematic_in_progress(void);
long cinematic_in_progress(void) { return 0; }
long cinematic_set_title_delayed(void);
long cinematic_set_title_delayed(void) { return 0; }
long cinematic_show_letterbox(void);
long cinematic_show_letterbox(void) { return 0; }
long cinematic_start(void);
long cinematic_start(void) { return 0; }
long cinematic_stop(void);
long cinematic_stop(void) { return 0; }
long collision_test_vector(void);
long collision_test_vector(void) { return 0; }
long conduct_input_active(void);
long conduct_input_active(void) { return 0; }
long config_boolean(void);
long config_boolean(void) { return 0; }
long config_integer(void);
long config_integer(void) { return 0; }
long coop_enemies_new_game(void);
long coop_enemies_new_game(void) { return 0; }
long coop_enemies_reset(void);
long coop_enemies_reset(void) { return 0; }
long coop_enemies_update(void);
long coop_enemies_update(void) { return 0; }
long coop_spectate_nothing_to_watch(void);
long coop_spectate_nothing_to_watch(void) { return 0; }
long coop_spectate_watching_rider(void);
long coop_spectate_watching_rider(void) { return 0; }
long damage_data_new(void);
long damage_data_new(void) { return 0; }
long damage_get_network_state(void);
long damage_get_network_state(void) { return 0; }
long damage_kill_object_for_player(void);
long damage_kill_object_for_player(void) { return 0; }
long damage_replay_aftermath(void);
long damage_replay_aftermath(void) { return 0; }
long damage_replay_kill(void);
long damage_replay_kill(void) { return 0; }
long damage_replay_player_effect(void);
long damage_replay_player_effect(void) { return 0; }
long damage_replay_static(void);
long damage_replay_static(void) { return 0; }
long damage_set_network_state(void);
long damage_set_network_state(void) { return 0; }
long delta_legacy_announce(void);
long delta_legacy_announce(void) { return 0; }
long delta_peer_game_machine_identity_text(void);
long delta_peer_game_machine_identity_text(void) { return 0; }
long delta_peer_game_objects_clients(void);
long delta_peer_game_objects_clients(void) { return 0; }
long delta_peer_game_objects_ready(void);
long delta_peer_game_objects_ready(void) { return 0; }
long delta_peer_game_send_object_names(void);
long delta_peer_game_send_object_names(void) { return 0; }
long delta_peer_game_send_unit_vitals(void);
long delta_peer_game_send_unit_vitals(void) { return 0; }
long delta_peer_game_set_objects_client(void);
long delta_peer_game_set_objects_client(void) { return 0; }
long device_group_network_get(void);
long device_group_network_get(void) { return 0; }
long device_group_network_set(void);
long device_group_network_set(void) { return 0; }
long device_port_set_state(void);
long device_port_set_state(void) { return 0; }
long director_camera_scripted(void);
long director_camera_scripted(void) { return 0; }
long director_script_camera(void);
long director_script_camera(void) { return 0; }
long effect_delete(void);
long effect_delete(void) { return 0; }
long find_netgame_flag(void);
long find_netgame_flag(void) { return 0; }
long game_allegiance_get_teams(void);
long game_allegiance_get_teams(void) { return 0; }
long game_allegiance_set_teams(void);
long game_allegiance_set_teams(void) { return 0; }
long game_engine_bomb_silent_death(void);
long game_engine_bomb_silent_death(void) { return 0; }
long game_engine_network_player_score(void);
long game_engine_network_player_score(void) { return 0; }
long game_engine_objective_weapon_deleted(void);
long game_engine_objective_weapon_deleted(void) { return 0; }
long game_engine_read_network_state(void);
long game_engine_read_network_state(void) { return 0; }
long game_engine_traits_active(void);
long game_engine_traits_active(void) { return 0; }
long game_engine_write_network_state(void);
long game_engine_write_network_state(void) { return 0; }
long game_players_are_double_speed(void);
long game_players_are_double_speed(void) { return 0; }
long game_state_port_restamp_revert_time(void);
long game_state_port_restamp_revert_time(void) { return 0; }
long game_time_set_distributed(void);
long game_time_set_distributed(void) { return 0; }
long global_structure_bsp_index_get(void);
long global_structure_bsp_index_get(void) { return 0; }
long halo_asin(void);
long halo_asin(void) { return 0; }
long halo_atan2(void);
long halo_atan2(void) { return 0; }
long halo_cos(void);
long halo_cos(void) { return 0; }
long halo_sin(void);
long halo_sin(void) { return 0; }
long host_evidence_note(void);
long host_evidence_note(void) { return 0; }
long host_evidence_reset(void);
long host_evidence_reset(void) { return 0; }
long host_evidence_reset_machine(void);
long host_evidence_reset_machine(void) { return 0; }
long hs_effect_new(void);
long hs_effect_new(void) { return 0; }
long hs_effect_new_from_object_marker(void);
long hs_effect_new_from_object_marker(void) { return 0; }
long hs_runtime_port_shift_sleep_times(void);
long hs_runtime_port_shift_sleep_times(void) { return 0; }
long hud_activate_team_nav_point_with_flag(void);
long hud_activate_team_nav_point_with_flag(void) { return 0; }
long hud_activate_team_nav_point_with_object(void);
long hud_activate_team_nav_point_with_object(void) { return 0; }
long hud_autosave(void);
long hud_autosave(void) { return 0; }
long hud_deactivate_team_nav_point_with_flag(void);
long hud_deactivate_team_nav_point_with_flag(void) { return 0; }
long hud_deactivate_team_nav_point_with_object(void);
long hud_deactivate_team_nav_point_with_object(void) { return 0; }
long hud_messaging_port_message_count(void);
long hud_messaging_port_message_count(void) { return 0; }
long hud_messaging_port_timer_get(void);
long hud_messaging_port_timer_get(void) { return 0; }
long hud_messaging_port_timer_set(void);
long hud_messaging_port_timer_set(void) { return 0; }
long hud_unit_activate_nav_point_with_flag(void);
long hud_unit_activate_nav_point_with_flag(void) { return 0; }
long hud_unit_activate_nav_point_with_object(void);
long hud_unit_activate_nav_point_with_object(void) { return 0; }
long hud_unit_deactivate_nav_point_with_flag(void);
long hud_unit_deactivate_nav_point_with_flag(void) { return 0; }
long hud_unit_deactivate_nav_point_with_object(void);
long hud_unit_deactivate_nav_point_with_object(void) { return 0; }
long hud_unit_port_script_flags(void);
long hud_unit_port_script_flags(void) { return 0; }
long hud_unit_port_set_script_flags(void);
long hud_unit_port_set_script_flags(void) { return 0; }
long hud_weapon_port_script_flags(void);
long hud_weapon_port_script_flags(void) { return 0; }
long hud_weapon_port_set_script_flags(void);
long hud_weapon_port_set_script_flags(void) { return 0; }
long line_of_sight_explosion(void);
long line_of_sight_explosion(void) { return 0; }
long line_of_sight_shot(void);
long line_of_sight_shot(void) { return 0; }
long main_get_window_count(void);
long main_get_window_count(void) { return 0; }
long main_menu_is_active(void);
long main_menu_is_active(void) { return 0; }
long main_saving_map(void);
long main_saving_map(void) { return 0; }
long main_skip_cinematic(void);
long main_skip_cinematic(void) { return 0; }
long main_switch_structure_bsp(void);
long main_switch_structure_bsp(void) { return 0; }
long matrix4x3_rotation_from_axis_and_angle(void);
long matrix4x3_rotation_from_axis_and_angle(void) { return 0; }
long mcc_cache_tags_loaded(void);
long mcc_cache_tags_loaded(void) { return 0; }
long mcc_game_speed_clock(void);
long mcc_game_speed_clock(void) { return 0; }
long mcc_game_speed_receive(void);
long mcc_game_speed_receive(void) { return 0; }
long mcc_grenades_active(void);
long mcc_grenades_active(void) { return 0; }
long mcc_grenades_type_count(void);
long mcc_grenades_type_count(void) { return 0; }
long mcc_network_campaign_entry_size(void);
long mcc_network_campaign_entry_size(void) { return 0; }
long mcc_network_client_tick(void);
long mcc_network_client_tick(void) { return 0; }
long mcc_network_handle_campaign(void);
long mcc_network_handle_campaign(void) { return 0; }
long mcc_network_handle_inventories(void);
long mcc_network_handle_inventories(void) { return 0; }
long mcc_network_host_tick(void);
long mcc_network_host_tick(void) { return 0; }
long mcc_network_inventory_entry_size(void);
long mcc_network_inventory_entry_size(void) { return 0; }
long mcc_network_new_game(void);
long mcc_network_new_game(void) { return 0; }
long mcc_object_names_active(void);
long mcc_object_names_active(void) { return 0; }
long mcc_object_names_entry_size(void);
long mcc_object_names_entry_size(void) { return 0; }
long mcc_object_names_receive(void);
long mcc_object_names_receive(void) { return 0; }
long mcc_script_effects_host_movement(void);
long mcc_script_effects_host_movement(void) { return 0; }
long mcc_script_effects_preserve_body(void);
long mcc_script_effects_preserve_body(void) { return 0; }
long model_find_marker(void);
long model_find_marker(void) { return 0; }
long network_distributed_client_send(void);
long network_distributed_client_send(void) { return 0; }
long network_distributed_client_send_reliably(void);
long network_distributed_client_send_reliably(void) { return 0; }
long network_distributed_server_machines(void);
long network_distributed_server_machines(void) { return 0; }
long network_distributed_server_send_to_all_reliably(void);
long network_distributed_server_send_to_all_reliably(void) { return 0; }
long network_distributed_server_send_to_machine(void);
long network_distributed_server_send_to_machine(void) { return 0; }
long network_distributed_server_send_to_machine_reliably(void);
long network_distributed_server_send_to_machine_reliably(void) { return 0; }
long network_game_client_host_network_version(void);
long network_game_client_host_network_version(void) { return 0; }
long network_game_server_kick_machine(void);
long network_game_server_kick_machine(void) { return 0; }
long network_game_server_machine_address(void);
long network_game_server_machine_address(void) { return 0; }
long network_game_server_machine_reports_static_damage(void);
long network_game_server_machine_reports_static_damage(void) { return 0; }
long network_player_attach_unit(void);
long network_player_attach_unit(void) { return 0; }
long network_player_detach_unit(void);
long network_player_detach_unit(void) { return 0; }
long network_player_show_pickup(void);
long network_player_show_pickup(void) { return 0; }
long object_cause_damage(void);
long object_cause_damage(void) { return 0; }
long object_claim_scenario_name(void);
long object_claim_scenario_name(void) { return 0; }
long object_damage_update(void);
long object_damage_update(void) { return 0; }
long object_delete(void);
long object_delete(void) { return 0; }
long object_delete_immediately(void);
long object_delete_immediately(void) { return 0; }
long object_disconnect_from_map(void);
long object_disconnect_from_map(void) { return 0; }
long object_get_marker_by_name(void);
long object_get_marker_by_name(void) { return 0; }
long object_get_velocities(void);
long object_get_velocities(void) { return 0; }
long object_has_node(void);
long object_has_node(void) { return 0; }
long object_impulse_sound_new(void);
long object_impulse_sound_new(void) { return 0; }
long object_index_from_name_index(void);
long object_index_from_name_index(void) { return 0; }
long object_initialize_vitality(void);
long object_initialize_vitality(void) { return 0; }
long object_iterator_new(void);
long object_iterator_new(void) { return 0; }
long object_new(void);
long object_new(void) { return 0; }
long object_new_by_name(void);
long object_new_by_name(void) { return 0; }
long object_placement_data_new(void);
long object_placement_data_new(void) { return 0; }
long object_reconnect_to_map(void);
long object_reconnect_to_map(void) { return 0; }
long object_set_garbage(void);
long object_set_garbage(void) { return 0; }
long object_set_position(void);
long object_set_position(void) { return 0; }
long object_set_visibility(void);
long object_set_visibility(void) { return 0; }
long objects_get_activating_cluster_index(void);
long objects_get_activating_cluster_index(void) { return 0; }
long objects_port_set_activating_cluster(void);
long objects_port_set_activating_cluster(void) { return 0; }
long objects_scripting_attach(void);
long objects_scripting_attach(void) { return 0; }
long objects_scripting_detach(void);
long objects_scripting_detach(void) { return 0; }
long objects_scripting_set_scale(void);
long objects_scripting_set_scale(void) { return 0; }
long observer_up_from_forward(void);
long observer_up_from_forward(void) { return 0; }
long p2p_discord_identity(void);
long p2p_discord_identity(void) { return 0; }
long p2p_discord_sanitize(void);
long p2p_discord_sanitize(void) { return 0; }
long p2p_hardware_id_sanitize(void);
long p2p_hardware_id_sanitize(void) { return 0; }
long p2p_peer_endpoint_address(void);
long p2p_peer_endpoint_address(void) { return 0; }
long player_control_action_test_note(void);
long player_control_action_test_note(void) { return 0; }
long player_control_set_desired_weapon(void);
long player_control_set_desired_weapon(void) { return 0; }
long player_effect_port_screen_fade_get(void);
long player_effect_port_screen_fade_get(void) { return 0; }
long player_effect_port_screen_fade_set(void);
long player_effect_port_screen_fade_set(void) { return 0; }
long player_effect_port_scripted_active(void);
long player_effect_port_scripted_active(void) { return 0; }
long player_effect_port_scripted_end(void);
long player_effect_port_scripted_end(void) { return 0; }
long player_input_enable(void);
long player_input_enable(void) { return 0; }
long player_input_enabled(void);
long player_input_enabled(void) { return 0; }
long players_show_telefragged(void);
long players_show_telefragged(void) { return 0; }
long points_interpolate(void);
long points_interpolate(void) { return 0; }
long rasterizer_screen_effect_port_get(void);
long rasterizer_screen_effect_port_get(void) { return 0; }
long rasterizer_screen_effect_port_set(void);
long rasterizer_screen_effect_port_set(void) { return 0; }
long render_interpolation_correct_object(void);
long render_interpolation_correct_object(void) { return 0; }
long scenario_leaf_index_from_point(void);
long scenario_leaf_index_from_point(void) { return 0; }
long scenario_location_from_point(void);
long scenario_location_from_point(void) { return 0; }
long scenery_animation_start_at_frame(void);
long scenery_animation_start_at_frame(void) { return 0; }
long scripted_camera_object_relative_to(void);
long scripted_camera_object_relative_to(void) { return 0; }
long scripted_camera_set_camera_point_relative(void);
long scripted_camera_set_camera_point_relative(void) { return 0; }
long scripted_hud_messages_clear(void);
long scripted_hud_messages_clear(void) { return 0; }
long scripted_hud_set_flashing_state(void);
long scripted_hud_set_flashing_state(void) { return 0; }
long scripted_hud_set_objective(void);
long scripted_hud_set_objective(void) { return 0; }
long scripted_hud_set_state_message(void);
long scripted_hud_set_state_message(void) { return 0; }
long scripted_looping_sound_start(void);
long scripted_looping_sound_start(void) { return 0; }
long scripted_looping_sound_stop(void);
long scripted_looping_sound_stop(void) { return 0; }
long scripted_player_effect_set_rotation(void);
long scripted_player_effect_set_rotation(void) { return 0; }
long scripted_player_effect_set_translation(void);
long scripted_player_effect_set_translation(void) { return 0; }
long scripted_player_effect_start(void);
long scripted_player_effect_start(void) { return 0; }
long scripted_player_effect_stop(void);
long scripted_player_effect_stop(void) { return 0; }
long scripted_sound_new(void);
long scripted_sound_new(void) { return 0; }
long sound_stop_impulse(void);
long sound_stop_impulse(void) { return 0; }
long system_free(void);
long system_free(void) { return 0; }
long tag_index_is_group(void);
long tag_index_is_group(void) { return 0; }
long tag_iterator_new(void);
long tag_iterator_new(void) { return 0; }
long tag_iterator_next(void);
long tag_iterator_next(void) { return 0; }
long unit_can_enter_seat(void);
long unit_can_enter_seat(void) { return 0; }
long unit_close(void);
long unit_close(void) { return 0; }
long unit_control(void);
long unit_control(void) { return 0; }
long unit_enter_seat(void);
long unit_enter_seat(void) { return 0; }
long unit_exit_seat_end(void);
long unit_exit_seat_end(void) { return 0; }
long unit_get_camera_position(void);
long unit_get_camera_position(void) { return 0; }
long unit_get_head_position(void);
long unit_get_head_position(void) { return 0; }
long unit_has_animation_to_enter_seat(void);
long unit_has_animation_to_enter_seat(void) { return 0; }
long unit_inventory_get_weapon(void);
long unit_inventory_get_weapon(void) { return 0; }
long unit_kill(void);
long unit_kill(void) { return 0; }
long unit_kill_no_statistics(void);
long unit_kill_no_statistics(void) { return 0; }
long unit_kill_silent(void);
long unit_kill_silent(void) { return 0; }
long unit_leap_begin(void);
long unit_leap_begin(void) { return 0; }
long unit_melee_attack_begin(void);
long unit_melee_attack_begin(void) { return 0; }
long unit_network_add_weapon(void);
long unit_network_add_weapon(void) { return 0; }
long unit_network_drop_weapon(void);
long unit_network_drop_weapon(void) { return 0; }
long unit_network_forget_weapon(void);
long unit_network_forget_weapon(void) { return 0; }
long unit_open(void);
long unit_open(void) { return 0; }
long unit_port_correct_damage_animation(void);
long unit_port_correct_damage_animation(void) { return 0; }
long unit_port_play_user_animation(void);
long unit_port_play_user_animation(void) { return 0; }
long unit_port_resurrect(void);
long unit_port_resurrect(void) { return 0; }
long unit_scripting_set_current_vitality_of(void);
long unit_scripting_set_current_vitality_of(void) { return 0; }
long unit_set_actively_controlled(void);
long unit_set_actively_controlled(void) { return 0; }
long unit_start_animation_impulse(void);
long unit_start_animation_impulse(void) { return 0; }
long unit_stop_custom_animation(void);
long unit_stop_custom_animation(void) { return 0; }
long unit_unarmed_melee_damage(void);
long unit_unarmed_melee_damage(void) { return 0; }
long update_client_distributed_input(void);
long update_client_distributed_input(void) { return 0; }
long update_client_handle_relayed_action(void);
long update_client_handle_relayed_action(void) { return 0; }
long update_queues_distributed_reset(void);
long update_queues_distributed_reset(void) { return 0; }
long update_queues_reset_and_fill_with_lies(void);
long update_queues_reset_and_fill_with_lies(void) { return 0; }
long update_server_handle_distributed_input(void);
long update_server_handle_distributed_input(void) { return 0; }
long update_server_ticked_update_number(void);
long update_server_ticked_update_number(void) { return 0; }
long vectors_interpolate(void);
long vectors_interpolate(void) { return 0; }
long weapon_is_flag(void);
long weapon_is_flag(void) { return 0; }
void *game_engine_get_variant(void);
void *game_engine_get_variant(void) { return distributed_stubs_zero; }
void *global_scenario_get(void);
void *global_scenario_get(void) { return distributed_stubs_zero; }
void *global_structure_bsp_get(void);
void *global_structure_bsp_get(void) { return distributed_stubs_zero; }
void *machine_get_player_list(void);
void *machine_get_player_list(void) { return distributed_stubs_zero; }
void *matrix4x3_transform_vector(void);
void *matrix4x3_transform_vector(void) { return distributed_stubs_zero; }
void *mcc_seat_parent(void);
void *mcc_seat_parent(void) { return 0; }
void *network_game_get_game(void);
void *network_game_get_game(void) { return distributed_stubs_zero; }
void *network_game_server_machine_hardware_id(void);
void *network_game_server_machine_hardware_id(void) { return distributed_stubs_zero; }
void *object_iterator_next(void);
void *object_iterator_next(void) { return 0; }
void *observer_get_camera(void);
void *observer_get_camera(void) { return distributed_stubs_zero; }
void *scenario_get_game_globals(void);
void *scenario_get_game_globals(void) { return distributed_stubs_zero; }
void *structure_bsp_get_cluster_pvs(void);
void *structure_bsp_get_cluster_pvs(void) { return distributed_stubs_zero; }

void *tag_block_get_element_with_size(void);
void *tag_block_get_element_with_size(void) { return distributed_stubs_zero; }
void *tag_get(void);
void *tag_get(void) { return distributed_stubs_zero; }
void *tag_get_name(void);
void *tag_get_name(void) { return distributed_stubs_zero; }
void *update_server_update_actions(void);
void *update_server_update_actions(void) { return distributed_stubs_zero; }
float game_engine_traits_gravity_multiplier(void);
float game_engine_traits_gravity_multiplier(void) { return 0.0f; }
float game_engine_traits_jump_multiplier(void);
float game_engine_traits_jump_multiplier(void) { return 0.0f; }
float object_get_maximum_body_vitality(void);
float object_get_maximum_body_vitality(void) { return 0.0f; }
long halo_linux_fopen(void);
long halo_linux_fopen(void) { return 0; }
long halo_linux_fprintf(void);
long halo_linux_fprintf(void) { return 0; }
long global_network_game_client_get(void);
long global_network_game_client_get(void) { return 0; }
long global_network_game_server_get(void);
long global_network_game_server_get(void) { return 0; }
long mcc_share_answer_result(void);
long mcc_share_answer_result(void) { return 0; }
long mcc_share_decline(void);
long mcc_share_decline(void) { return 0; }
long mcc_admission_holding(void);
long mcc_admission_holding(void) { return 0; }
long mcc_admission_take(void);
long mcc_admission_take(void) { return 0; }
long mcc_identity_host_admission(void);
long mcc_identity_host_admission(void) { return 0; }
long network_game_server_map_share_machine(void);
long network_game_server_map_share_machine(void) { return 0; }
