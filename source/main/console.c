/*
CONSOLE.C

symbols in this file:
000EEC10 0060:
	_console_initialize (0000)
000EEC70 0010:
	_console_initialize_for_new_map (0000)
000EEC80 0030:
	_console_open (0000)
000EECB0 0020:
	_console_close (0000)
000EECD0 0010:
	_console_is_active (0000)
000EECE0 0080:
	_console_printf (0000)
000EED60 0080:
	_console_warning (0000)
000EEDE0 0070:
	_code_000eede0 (0000)
000EEE50 0040:
	_code_000eee50 (0000)
000EEE90 0220:
	_code_000eee90 (0000)
000EF0B0 0120:
	_console_startup (0000)
000EF1D0 0020:
	_console_dispose (0000)
000EF1F0 0220:
	_console_update (0000)
0027944C 0007:
	??_C@_06IFDPLABP@halo?$CI?5?$AA@ (0000)
00279454 0009:
	??_C@_08NDPNIJOB@init?3?5?$CFs?$AA@ (0000)
00279460 0004:
	??_C@_03PAIEMGKB@?$AN?6?7?$AA@ (0000)
00279464 0013:
	??_C@_0BD@MEEDMDNF@editor_d?3?2init?4txt?$AA@ (0000)
00279478 000c:
	??_C@_0M@PGLCPLJK@d?3?2init?4txt?$AA@ (0000)
00279484 0014:
	??_C@_0BE@PBECEFPN@key?9?$DOkey_code?$CB?$DNNONE?$AA@ (0000)
00279498 001e:
	??_C@_0BO@NFBJJCKH@c?3?2halo?2SOURCE?2main?2console?4c?$AA@ (0000)
00307740 0010:
	_data_00307740 (0000)
00454D80 09c5:
	_bss_00454d80 (0000)
	_console_dump_to_file (09c4)
*/

/* ---------- headers */

#include "cseries.h"
#include "console.h"

#include "cseries/errors.h"
#include "cseries/profile.h"
#include "editor/editor_stubs.h"
#include "hs/hs.h"
#include "networking/network_server_manager.h"
#include "input/input.h"
#include "interface/terminal.h"
#include "math/real_math.h"

/* ---------- constants */

enum
{
#if defined(HALO_XBOX_CONSOLE) || defined(HALO_CONSOLE)
	MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS = 8,
#else
	/* port: more of them, kept across restarts on the desktop
	(console_history_save) */
	MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS = 64,
#endif
};

/* ---------- macros */

/* ---------- structures */

struct console_globals
{
	boolean active;
	boolean available;
	struct terminal_gets_state input_state;
	char previous_commands[MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS][255];
	short previous_command_count;
	short newest_previous_command_index;
	short selected_previous_command_index;
};

/* ---------- prototypes */

static boolean console_process_command(const char *command);

static char *console_get_text_to_autocomplete(void);

static void console_complete(void);
/* port: Tab (1) or Shift+Tab (-1), for the drop-down console's candidates */
static short console_completion_direction = 1;

/* ---------- globals */

boolean console_dump_to_file = FALSE;

static real_argb_color console_color = {1.f, 1.f, 0.3f, 1.f};	// Pink

static struct console_globals console_globals;

/* ---------- public code */

void console_initialize(
	void)
{
	console_globals.input_state.color = console_color;
	csstrcpy(console_globals.input_state.prompt, "halo( ");
	console_globals.input_state.result[0] = 0;
	console_globals.newest_previous_command_index = NONE;
	console_globals.previous_command_count = 0;
	console_globals.selected_previous_command_index = NONE;

	return;
}

void console_initialize_for_new_map(
	void)
{
	return;
}

void console_open(
	void)
{
	if (!console_globals.active)
	{
		console_globals.input_state.result[0] = '\0';
		console_globals.active = terminal_gets_begin(&console_globals.input_state);
		profile_global_enable = FALSE;
	}

	return;
}

#ifndef HALO_XBOX_CONSOLE
/* port: each frame, before the game and the menus read the controllers
(main.c, after input_update): while the console is open, a controller's B
or Back closes it when let go of (never on the press, so the press reaches
nothing else), and the controllers count as untouched for everything else
(input_suppress: no stick, trigger or button moves the player or the menus
while it is open, in a network game too). The keyboard's keys are the
console's already. */
void console_input_update(
	void)
{
	static boolean close_pending = FALSE;
	struct gamepad_state const *gamepad;

	if (!console_globals.active)
	{
		close_pending = FALSE;
		return;
	}
	gamepad = input_has_gamepad(0) ? input_get_gamepad_state(0) : NULL;
	if (gamepad && (gamepad->buttons[_gamepad_analog_button_b] || gamepad->buttons[_gamepad_binary_button_back]))
		close_pending = TRUE;
	else if (close_pending)
	{
		close_pending = FALSE;
		console_close();
	}
	input_suppress();
}
#endif

void console_close(
	void)
{
	if (console_globals.active)
	{
		terminal_gets_end(&console_globals.input_state);
		console_globals.active = FALSE;
	}

	return;
}

boolean console_is_active(
	void)
{
	return console_globals.active;
}

void console_printf(
	boolean clear,
	const char *format,
	...)
{
	char buffer[1024];
	va_list arglist;

	va_start(arglist, format);

	if (clear)
	{
		terminal_clear();
	}
	
	/* port: no longer than the buffer (cut to 255 below, as it was) */
	vsnprintf(buffer, NUMBEROF(buffer), format, arglist);
	buffer[255] = '\0';
	
	terminal_printf(0, "%s", buffer);
	if (console_dump_to_file)
	{
		strncat(buffer, "\r\n", NUMBEROF(buffer));
		write_to_error_file(buffer, TRUE);
	}

	va_end(arglist);

	return;
}

void console_warning(
	const char *format,
	...)
{
	char buffer[1024];
	va_list arglist;

	va_start(arglist, format);


	/* port: no longer than the buffer (cut to 255 below, as it was) */
	vsnprintf(buffer, NUMBEROF(buffer), format, arglist);
	buffer[255] = '\0';

	/* (port: an important line, shown as config.toml's game.console_log
	says; the answer to a command someone typed, always) */
	if (terminal_shows(terminal_command_running ? _terminal_message_serious : _terminal_message_important))
		terminal_printf(global_real_argb_red, "%s", buffer);
	if (console_dump_to_file)
	{
		csstrncat(buffer, "\r\n", NUMBEROF(buffer));
		write_to_error_file(buffer, TRUE);
	}

	va_end(arglist);

	return;
}

#if !defined(HALO_CONSOLE) && !defined(HALO_XBOX_CONSOLE)
/* port: the commands typed, kept across restarts in the save root's
private/console_history.txt, oldest first (read when the console first
opens, written after each command) */
#define CONSOLE_PERSISTENT_HISTORY
/* port/linux/src/platform.h's, and port_folders.h's _port_folder_private
(the save root's private/; port/linux/tests/port_folders_test.c checks the
number) */
const char *platform_folder(int folder);
#define CONSOLE_HISTORY_FOLDER 3

static void console_history_path(
	char *path,
	unsigned long size)
{
	char const *folder = platform_folder(CONSOLE_HISTORY_FOLDER);

	if (folder && *folder)
		snprintf(path, size, "%s/console_history.txt", folder);
	else
		path[0] = 0;
}

static void console_history_save(
	void)
{
	char path[1024];
	FILE *file;
	short index;

	console_history_path(path, sizeof(path));
	if (!path[0] || !(file = fopen(path, "w")))
		return;
	for (index = (short)(console_globals.previous_command_count - 1); index >= 0; index--)
	{
		fprintf(file, "%s\n", console_globals.previous_commands[
			(console_globals.newest_previous_command_index - index + MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS) %
				MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS]);
	}
	fclose(file);
}

static void console_history_load(
	void)
{
	static boolean loaded = FALSE;
	char path[1024];
	char line[256];
	FILE *file;

	if (loaded)
		return;
	loaded = TRUE;
	console_history_path(path, sizeof(path));
	if (!path[0] || !(file = fopen(path, "r")))
		return;
	while (fgets(line, sizeof(line), file))
	{
		short index;

		line[strcspn(line, "\r\n")] = 0;
		if (!line[0])
			continue;
		index = (short)((console_globals.newest_previous_command_index + 1) % MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS);
		console_globals.newest_previous_command_index = index;
		csstrncpy(console_globals.previous_commands[index], line, NUMBEROF(console_globals.previous_commands[index]) - 1);
		console_globals.previous_commands[index][NUMBEROF(console_globals.previous_commands[index]) - 1] = 0;
		console_globals.previous_command_count =
			MIN(console_globals.previous_command_count + 1, MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS);
	}
	console_globals.selected_previous_command_index = NONE;
	fclose(file);
}
#endif

#ifndef HALO_CONSOLE_NO_HIRES
/* port: the drop-down console's shell (terminal_shell_style): its own
commands and its typing (port/linux/game/console_shell.h) */
#define CONSOLE_SHELL
#include "console_shell.h"

/* port: help, and help <command> */
static void console_shell_help(
	char const *topic)
{
	char line[256];
	char usage[256];
	char const *help;
	char const *returns;
	char const *type;

	while (*topic == ' ')
		topic++;
	if (!*topic)
	{
		short columns = terminal_columns();
		short width = 0;
		short per_line;
		short index;
		short count = 0;
		char const *name;

		for (index = 0; (name = hs_function_name_at(index)) != NULL; index++)
		{
			width = (short)MAX(width, (short)strlen(name));
			count++;
		}
		width += 2;
		per_line = (short)MAX(1, columns / width);
		terminal_shell_note("%d commands. help <command> shows one; Tab completes.", count);
		line[0] = 0;
		for (index = 0; (name = hs_function_name_at(index)) != NULL; index++)
		{
			long length = (long)strlen(line);

			snprintf(line + length, sizeof(line) - length, "%-*s", width, name);
			if (index % per_line == per_line - 1)
			{
				terminal_shell_output("%s", line);
				line[0] = 0;
			}
		}
		if (line[0])
			terminal_shell_output("%s", line);
		return;
	}
	if (hs_function_usage(topic, usage, sizeof(usage)) && hs_function_help(topic, &help, &returns))
	{
		terminal_shell_output("usage: %s", usage);
		if (help && *help)
			terminal_shell_output("%s", help);
		if (returns && csstrcmp(returns, "void"))
			terminal_shell_note("returns: %s", returns);
		return;
	}
	if ((type = hs_global_type_name(topic)) != NULL)
	{
		terminal_shell_output("%s: a %s variable. %s shows it; %s <value> sets it.", topic, type, topic, topic);
		return;
	}
	terminal_shell_error("chupathingy: help: no such command: %s", topic);
}

/* port: a line typed into the drop-down console, as a shell runs it:
TRUE when it was the shell's own (clear, help) and is done */
static boolean console_shell_builtin(
	char const *command)
{
	while (*command == ' ')
		command++;
	if (!csstrcmp(command, "clear"))
	{
		terminal_scrollback_clear();
		return TRUE;
	}
	if (!csstrcmp(command, "help") || !strncmp(command, "help ", 5))
	{
		console_shell_help(command + 4);
		return TRUE;
	}
	return FALSE;
}
#endif

/* port: the command typed age commands ago (0 the newest), for the
drop-down console's reverse search; NULL past the oldest */
char const *console_history_entry(
	short age)
{
	if (age < 0 || age >= console_globals.previous_command_count)
		return NULL;
	return console_globals.previous_commands[
		(console_globals.newest_previous_command_index - age + MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS) %
			MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS];
}

static boolean console_process_command(
	const char *command)
{
	boolean valid;
	short previous_command_count;
	short newest_previous_command_index;
#ifdef CONSOLE_SHELL
	char repeated[256];
	char expression[512];
	boolean shell = terminal_shell_style();

	/* (!! is the command before) */
	if (shell)
	{
		char const *trimmed = command;

		while (*trimmed == ' ')
			trimmed++;
		if (!csstrcmp(trimmed, "!!"))
		{
			char const *previous = console_history_entry(0);

			if (!previous)
			{
				terminal_echo_command(command);
				terminal_shell_error("chupathingy: !!: no previous command");
				return FALSE;
			}
			csstrncpy(repeated, previous, sizeof(repeated) - 1);
			repeated[sizeof(repeated) - 1] = 0;
			command = repeated;
		}
	}
#endif

	newest_previous_command_index = (console_globals.newest_previous_command_index + 1) % MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS;

	console_globals.newest_previous_command_index = newest_previous_command_index;
	/* port: no longer than the slot (a command can come from the telnet
	console) */
	csstrncpy(console_globals.previous_commands[newest_previous_command_index], command,
		NUMBEROF(console_globals.previous_commands[newest_previous_command_index]) - 1);
	console_globals.previous_commands[newest_previous_command_index][
		NUMBEROF(console_globals.previous_commands[newest_previous_command_index]) - 1] = 0;


	console_globals.previous_command_count = MIN(console_globals.previous_command_count + 1, MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS);
	console_globals.selected_previous_command_index = NONE;
#ifdef CONSOLE_PERSISTENT_HISTORY
	console_history_save();
#endif
	/* port: echoed in the drop-down console's scrollback, after its prompt */
	terminal_echo_command(command);
#ifdef CONSOLE_SHELL
	/* port: the drop-down console's shell: its own commands, then the line
	made the scripts' expression (console_shell.h) */
	if (shell)
	{
		int problem;

		if (console_shell_builtin(command))
			return TRUE;
		problem = console_shell_translate(command, expression, sizeof(expression), hs_function_text_tail, NULL);
		if (problem == _console_shell_unterminated_quote)
		{
			terminal_shell_error("chupathingy: missing a closing quote");
			return FALSE;
		}
		if (problem == _console_shell_unmatched_parenthesis)
		{
			terminal_shell_error("chupathingy: missing a closing parenthesis");
			return FALSE;
		}
		if (problem != _console_shell_ok)
		{
			terminal_shell_error("chupathingy: that line is too long");
			return FALSE;
		}
		if (!expression[0])
			return TRUE;
		return hs_compile_and_evaluate(expression);
	}
#endif

	return hs_compile_and_evaluate(command);
}

static char *console_get_text_to_autocomplete(
	void)
{
	char *result = console_globals.input_state.result;
	char *result_after_space = strrchr(console_globals.input_state.result, ' ') + 1;
	char *result_after_parenthesis = strrchr(console_globals.input_state.result, '(') + 1;
	char *result_after_quote = strrchr(console_globals.input_state.result, '"') + 1;

	if (result <= result_after_space)
	{
		result = result_after_space;
	}
	if (result <= result_after_parenthesis)
	{
		result = result_after_parenthesis;
	}
	if (result <= result_after_quote)
	{
		result = result_after_quote;
	}

	return result;
}

/* port: the text after the host's ban or kick command ("ban ", "kick "),
which completes as a player's name (network_game_server_matching_player_names);
NULL if the input is not one */
static char *console_player_command_name(
	void)
{
	static char const *const commands[] = { "ban", "kick" };
	char *text = console_globals.input_state.result;
	short command;

	while (*text == ' ' || *text == '(')
		text++;
	for (command = 0; command < (short)NUMBEROF(commands); command++)
	{
		char const *word = commands[command];
		long length = (long)strlen(word);
		long index;

		for (index = 0; index < length; index++)
		{
			char character = text[index] >= 'A' && text[index] <= 'Z' ? text[index] - 'A' + 'a' : text[index];

			if (character != word[index])
				break;
		}
		if (index == length && text[length] == ' ')
		{
			text += length;
			while (*text == ' ' || *text == '"')
				text++;
			return text;
		}
	}
	return NULL;
}

static void console_complete(
	void)
{
	char *matching_items[256];
	char print_buffer[1024];
	/* (port: the players' names the ban and kick commands complete) */
	static char player_names[64][NETWORK_GAME_SERVER_NAME_TEXT_SIZE];

	char *token = console_player_command_name();
	short count;

	/* port: Tab again, with the drop-down console's candidates up: the
	next one (Shift+Tab the one before) in place of the word */
	{
		char const *next = terminal_completion_cycle(console_completion_direction);

		if (next)
		{
			char *word = token ? token : console_get_text_to_autocomplete();
			long room = NUMBEROF(console_globals.input_state.result) - 1 - (word - console_globals.input_state.result);

			if (room > 0)
			{
				csstrncpy(word, next, room);
				word[room] = 0;
				console_globals.input_state.edit.insertion_point_index =
					(short)(word + strlen(word) - console_globals.input_state.result);
				console_globals.input_state.edit.selection_start_index = NONE;
			}
			return;
		}
	}

	if (token)
	{
		short index;

		count = network_game_server_matching_player_names(token, player_names, NUMBEROF(player_names));
		for (index = 0; index < count; index++)
			matching_items[index] = player_names[index];
	}
	else
	{
		token = console_get_text_to_autocomplete();
		count = hs_tokens_enumerate(token, NONE, matching_items, NUMBEROF(matching_items));
	}

	if (count)
	{
		short token_num;

		short last_similar_character_index = SHORT_MAX;
		boolean print_second_column = count > 16;
		boolean listed = count > 1 && terminal_completion_wanted();

		print_buffer[0] = '\0';
		/* port: the drop-down console lists them under it instead of
		printing them (terminal_completion_show) */
		if (!listed)
			console_printf(FALSE, "");

		for (token_num = 0; token_num<count; token_num++)
		{
			short size = MIN(last_similar_character_index, strlen(matching_items[token_num]) - 1);
			short index = 0;

			/* likely fake match */
			if (tolower(matching_items[token_num][0]) == tolower(matching_items[0][0]))
			{
				do
				{
					if (index > size)
					{
						break;
					}

					index++;
				}
				while (tolower(matching_items[token_num][index]) == tolower(matching_items[0][index]));
			}
			last_similar_character_index = index - 1;

			if (listed)
			{
				/* (listed under the drop-down console, below) */
			}
			else if (print_second_column)
			{
				csstrcat(print_buffer, matching_items[token_num]);
				csstrcat(print_buffer, "|t");
				if (token_num % 4 == 3)
				{
					/* (port: as text, not a format: players' names are other
					machines') */
					console_printf(FALSE, "%s", print_buffer);
					print_buffer[0] = '\0';
				}
			}
			else
			{
				console_printf(FALSE, "%s", matching_items[token_num]);
			}
		}

		/* port: the drop-down console: Tab completes what the candidates
		have in common; when that adds nothing, they are listed under it,
		and Tab again goes through them one at a time
		(terminal_completion_cycle) */
		if (listed && (long)strlen(token) >= last_similar_character_index + 1)
		{
			terminal_completion_show((char const **)matching_items, count,
				(short)(token - console_globals.input_state.result));
		}
		else if (!listed && print_second_column && (token_num - 1) % 4 != 3)
		{
			console_printf(FALSE, "%s", print_buffer);
		}

		strncpy(token, matching_items[0], last_similar_character_index + 1);
		token[last_similar_character_index + 1] = '\0';
		console_globals.input_state.edit.insertion_point_index = &token[last_similar_character_index + 1] - console_globals.input_state.result;
	}

	return;
}

#ifdef HALO_CONSOLE_BRINGUP_LOG
/* port: init.txt's "later" commands (console_startup, console_update), for
a console's unattended test runs in its bring-up */
static char console_later_commands[4][200];
static short console_later_count;
static long console_later_frames;
#endif

void console_startup(
	void)
{
	char buffer[200];
	FILE *file;
	short newest_previous_command_index;

	if (!game_in_editor())
	{
		file = fopen("d:\\init.txt", "r");
	}
	else
	{
		file = fopen("editor_d:\\init.txt", "r");
	}

	if (file)
	{
		while (fgets(buffer, NUMBEROF(buffer)-1, file))
		{
			strtok(buffer, "\r\n\t");
#ifndef CONSOLE_PERSISTENT_HISTORY
			/* (port: on the desktop, where the history is the player's own
			commands kept across restarts, init.txt's lines stay out of it) */
			newest_previous_command_index = (console_globals.newest_previous_command_index + 1) % MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS;

			console_globals.newest_previous_command_index = newest_previous_command_index;
			strcpy(console_globals.previous_commands[newest_previous_command_index], buffer);

			console_globals.previous_command_count = MIN(console_globals.previous_command_count + 1, MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS);

			console_globals.selected_previous_command_index = NONE;
#endif
#ifdef HALO_CONSOLE_BRINGUP_LOG
			/* port: "later <command>" in init.txt runs the command once the
			game has run 60 frames (after the map and its players exist, as
			debug_camera_load needs), for unattended test runs */
			if (!strncmp(buffer, "later ", 6))
			{
				if (console_later_count < NUMBEROF(console_later_commands))
				{
					strncpy(console_later_commands[console_later_count], buffer + 6,
						NUMBEROF(console_later_commands[0]) - 1);
					console_later_count++;
				}
				continue;
			}
#endif
			if (hs_compile_and_evaluate(buffer))
			{
				error(_error_log, "init: %s", buffer);
			}
		}

		fclose(file);
	}

	return;
}

void console_dispose(
	void)
{
	console_close();

	return;
}

boolean console_update(
	void)
{
	boolean prev_command_valid;
	boolean last_command;

	short i;

#ifdef HALO_CONSOLE_BRINGUP_LOG
	if (console_later_count && ++console_later_frames == 60)
	{
		short later;

		for (later = 0; later < console_later_count; later++)
		{
			if (hs_compile_and_evaluate(console_later_commands[later]))
				error(_error_log, "init (later): %s", console_later_commands[later]);
		}
	}
#endif

	if (console_globals.active)
	{
		for (i = 0; i < console_globals.input_state.key_count; i++)
		{
			struct key_stroke *key = &console_globals.input_state.keys[i];
			match_assert("c:\\halo\\SOURCE\\main\\console.c", 184, key->key_code!=NONE);
			switch (key->key_code)
			{
			case _key_backquote:
			case _key_escape: /* port: as Halo PC's console */
				console_close();
				break;
			case _key_return:
			case _keypad_enter:
				if (console_globals.input_state.result[0])
				{
					console_process_command(console_globals.input_state.result);
					console_globals.input_state.result[0] = '\0';
				}
				else
				{
					console_close();
				}
				break;
			case _key_tab:
				/* (port: Shift+Tab, the drop-down console's candidate
				before) */
				console_completion_direction = TEST_FLAG(key->modifier_flags, 0) ? -1 : 1;
				console_complete();
				break;
			case _key_up_arrow:
				console_globals.selected_previous_command_index += 2;
			case _key_down_arrow:
				last_command = --console_globals.selected_previous_command_index > 0;
				console_globals.selected_previous_command_index = last_command ? console_globals.selected_previous_command_index : 0;

				prev_command_valid = console_globals.selected_previous_command_index>console_globals.previous_command_count-1;
				console_globals.selected_previous_command_index = 
				(
					prev_command_valid ? 
					console_globals.previous_command_count-1 :
					console_globals.selected_previous_command_index
				);

				if (console_globals.selected_previous_command_index != NONE)
				{
					strcpy(
						console_globals.input_state.result,
						console_globals.previous_commands[
							(console_globals.newest_previous_command_index - console_globals.selected_previous_command_index + MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS) % MAXIMUM_NUMBER_OF_PREVIOUS_COMMANDS
						]
					);
					edit_text_selection_reset(&console_globals.input_state.edit);
				}
			default:
				break;
			}
		}
	}
	else if (input_key_is_down(_key_backquote) == TRUE && !console_globals.active)
	{
#ifdef CONSOLE_PERSISTENT_HISTORY
		console_history_load();
#endif
		console_globals.input_state.result[0] = '\0';
		console_globals.active = terminal_gets_begin(&console_globals.input_state);
		profile_global_enable = FALSE;
	}

	return console_globals.active;
}
