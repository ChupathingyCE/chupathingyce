/*
INVITE_PROMPT.C

The question asked before joining the game of an invite link found on the
clipboard (configure.py --game-browser; network.join_from_clipboard "ask",
the default): when the game comes to the front with a halo://join/ link on
the clipboard (port/linux/src/sdl_platform.c), a small dialog over the
menus asks "Join the game from your clipboard?", with the game's name and
map when the game list knows them (browser_screen_invite_game), drawn and
driven by code as Online Games is (browser_screen.c).

A (Enter, or a click on YES) joins it; B (Escape, a click on NO or the
right button) leaves it, and that invite is not asked about again this run.
In a game with no menu up, a line at the top of the screen says the invite
is waiting, and the question is asked once a menu (the pause menu) is up.

A game with a password's invite (a locked link, from wherever it came:
p2p_locked_invite_waiting) asks for its password the same way: the keyboard
types it (shown as stars), A (Enter) joins with it, B (Escape) leaves it. A
wrong one says so, as the game lists do (halo_join_reasons.h), and the
typing begins again.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "../src/ui_overlay.h"
#include "halo_ui_pointer.h"
#include "halo_join_reasons.h"

void platform_log(char const *format, ...);
int platform_take_clipboard_invite(char *text, int size);
void platform_text_field(int typing);
int p2p_join_invite(char const *text);
int p2p_locked_invite_waiting(void);
int p2p_locked_invite_unlock(char const *password);
void p2p_locked_invite_cancel(void);
boolean browser_screen_invite_game(char const *link, char *name, long name_size, char *map, long map_size);
boolean pc_menus_chupathingy(void);

/* ---------- constants */

enum
{
	/* (event_manager.c's event types, which it keeps to itself) */
	INVITE_EVENT_BUTTON = 3,

	/* the dialog takes no A this soon after it opens (the A that opened
	the pause menu, or a key still down) */
	OPEN_SETTLE = 400,
	/* the invites answered No, kept for this run */
	MAXIMUM_DECLINED = 8,
	INVITE_TEXT_SIZE = 256,
	/* the invite's code as it is compared (after "halo://join/") */
	INVITE_CODE_SIZE = 160,
	/* the longest password (Server Setup's) */
	PASSWORD_LENGTH = 32,
};

/* the dialog's place, in the menus' 640x480 */
enum
{
	DIALOG_X = 150, DIALOG_Y = 150, DIALOG_WIDTH = 340, DIALOG_HEIGHT = 170,
	PROMPT_Y = DIALOG_Y + DIALOG_HEIGHT - 34,
};

/* the Xbox menus' blues, as Online Games has them */
enum
{
	COLOR_SHADE = 0x02081AA0,
	COLOR_TITLE = 0x3D8BFFFF,
	COLOR_TEXT = 0xFFFFFFFF,
	COLOR_TEXT_GLOW = 0x3D8BFF90,
	COLOR_DIM = 0x8FA6C8FF,
	COLOR_PROMPT = 0x4AA3FFFF,
	COLOR_PROMPT_GLOW = 0x3D8BFF80,
	COLOR_BANNER = 0x081530F0,
	COLOR_BANNER_EDGE = 0x2F6DD0FF,
};

/* the Chupathingy menus' (display.menus "chupathingy"): the question as
the Xbox's question boxes are (ui.map's alert_bkd over its grey shade): a
square box with a thin blue edge, its text in the Xbox's blue, no glow */
enum
{
	XBOX_SHADE = 0x1A1A1AA0,
	XBOX_BOX_TOP = 0x0B2350F0,
	XBOX_BOX_BOTTOM = 0x030C22F0,
	XBOX_EDGE = 0x2896FFFF,
	XBOX_TEXT = 0x2896FFFF,
	XBOX_TEXT_DIM = 0x6FA9E6FF,
};

#define PROMPT_BUTTON 16.0f
#define PROMPT_TEXT 12.0f
#define PROMPT_GAP 14.0f

/* ---------- globals */

static struct
{
	/* an invite waiting for an answer, and whether it is being asked */
	boolean pending;
	boolean active;
	unsigned long opened_time;
	char invite[INVITE_TEXT_SIZE];
	/* its game, if the game list knows it */
	boolean known;
	char name[64];
	char map[64];
	/* the prompts' places, for the pointer (NO, then YES) */
	float no_x0, no_x1, yes_x0, yes_x1;
	/* the invites answered No */
	char declined[MAXIMUM_DECLINED][INVITE_CODE_SIZE];
	short declined_next;
	/* asking for a locked invite's password (p2p_locked_invite_waiting): what
	is typed, and whether the last one was wrong */
	boolean password;
	char typed[PASSWORD_LENGTH + 1];
	boolean wrong;
} invite_prompt;

/* ---------- private code */

/* the invite's code: what follows "halo://join/" in the text, up to the
first character no invite has */
static void invite_code(
	char const *text,
	char *code,
	long size)
{
	char const *search;
	long length = 0;

	code[0] = 0;
	for (search = text; *search; search++)
	{
		if (!_strnicmp(search, "halo://join/", 12))
		{
			search += 12;
			while (length < size - 1 && search[length] && search[length] != ' ' && search[length] != '\n' &&
				search[length] != '\r' && search[length] != '\t')
			{
				char c = search[length];

				code[length] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
				length++;
			}
			code[length] = 0;
			return;
		}
	}
}

static boolean invite_declined(
	char const *text)
{
	char code[INVITE_CODE_SIZE];
	short index;

	invite_code(text, code, sizeof(code));
	for (index = 0; code[0] && index < MAXIMUM_DECLINED; index++)
	{
		if (!strcmp(invite_prompt.declined[index], code))
			return TRUE;
	}
	return FALSE;
}

static void answer(
	boolean join)
{
	invite_prompt.active = FALSE;
	invite_prompt.pending = FALSE;
	if (join)
	{
		platform_log("Internet play: joining the invite on the clipboard");
		p2p_join_invite(invite_prompt.invite);
	}
	else
	{
		invite_code(invite_prompt.invite, invite_prompt.declined[invite_prompt.declined_next],
			INVITE_CODE_SIZE);
		invite_prompt.declined_next = (short)((invite_prompt.declined_next + 1) % MAXIMUM_DECLINED);
		platform_log("Internet play: the invite on the clipboard is not joined");
	}
	invite_prompt.invite[0] = 0;
}

/* the password dialog closed: joined with it, or left */
static void password_close(
	void)
{
	invite_prompt.active = FALSE;
	invite_prompt.pending = FALSE;
	invite_prompt.password = FALSE;
	invite_prompt.wrong = FALSE;
	memset(invite_prompt.typed, 0, sizeof(invite_prompt.typed));
	platform_text_field(FALSE);
}

static void password_answer(
	boolean join)
{
	if (!join)
	{
		p2p_locked_invite_cancel();
		platform_log("Internet play: the game with a password is not joined");
		password_close();
	}
	else if (p2p_locked_invite_unlock(invite_prompt.typed))
	{
		platform_log("Internet play: joining the game with a password");
		password_close();
	}
	else
	{
		invite_prompt.wrong = TRUE;
		memset(invite_prompt.typed, 0, sizeof(invite_prompt.typed));
	}
}

/* the keys typed since the last frame, into the password */
static void password_typing(
	void)
{
	struct key_stroke key;

	while (input_get_key(&key))
	{
		size_t length = strlen(invite_prompt.typed);

		if (key.key_code == _key_backspace)
		{
			if (length)
				invite_prompt.typed[length - 1] = 0;
		}
		else if ((unsigned char)key.ascii_code >= ' ' && (unsigned char)key.ascii_code <= '~' &&
			length < PASSWORD_LENGTH)
		{
			invite_prompt.typed[length] = key.ascii_code;
			invite_prompt.typed[length + 1] = 0;
		}
	}
}

static void press(
	short button)
{
	if (invite_prompt.password)
	{
		if (button == _gamepad_analog_button_b)
			password_answer(FALSE);
		else if (button == _gamepad_analog_button_a &&
			system_milliseconds() - invite_prompt.opened_time > OPEN_SETTLE)
		{
			password_answer(TRUE);
		}
		return;
	}
	if (button == _gamepad_analog_button_b)
		answer(FALSE);
	else if (button == _gamepad_analog_button_a &&
		system_milliseconds() - invite_prompt.opened_time > OPEN_SETTLE)
	{
		answer(TRUE);
	}
}

/* a button's prompt (its glyph, then =WORD in the menus' face) from x; its
right end */
static float prompt(
	int button,
	char const *words,
	float x,
	float y,
	boolean question_box)
{
	x += ui_overlay_button(button, PROMPT_BUTTON, x, y, 0xFFFFFFFF) + 3.0f;
	if (question_box)
	{
		x += ui_overlay_text(UI_FONT_MENU, PROMPT_TEXT, x, y + (PROMPT_BUTTON - PROMPT_TEXT) * 0.5f + 0.5f,
			UI_ALIGN_LEFT, XBOX_TEXT, words);
	}
	else
	{
		x += ui_overlay_text_glow(UI_FONT_MENU, PROMPT_TEXT, x, y + (PROMPT_BUTTON - PROMPT_TEXT) * 0.5f + 0.5f,
			UI_ALIGN_LEFT, COLOR_PROMPT, COLOR_PROMPT_GLOW, words);
	}
	return x;
}

static float prompt_width(
	int button,
	char const *words)
{
	return ui_overlay_button_width(button, PROMPT_BUTTON) + 3.0f + ui_overlay_text_width(UI_FONT_MENU, PROMPT_TEXT, words);
}

/* the password dialog: the shared reason, what to do, and the stars typed */
static void password_render(
	boolean question_box)
{
	float center = DIALOG_X + DIALOG_WIDTH / 2;
	char stars[PASSWORD_LENGTH + 2];
	char const *hint = invite_prompt.wrong ? "That password is wrong. Try again." :
		"Type the game's password, then press Enter.";
	unsigned int text_color = question_box ? XBOX_TEXT : COLOR_TEXT;
	unsigned int dim = question_box ? XBOX_TEXT_DIM : COLOR_DIM;
	float width, x;
	size_t index;

	for (index = 0; invite_prompt.typed[index] && index < PASSWORD_LENGTH; index++)
		stars[index] = '*';
	if ((system_milliseconds() / 500) % 2)
		stars[index++] = '_';
	stars[index] = 0;

	ui_overlay_layer(3);
	if (question_box)
	{
		ui_overlay_rect(-200, 0, 1040, 480, 0, XBOX_SHADE);
		ui_overlay_gradient(DIALOG_X, DIALOG_Y, DIALOG_WIDTH, DIALOG_HEIGHT, 0, XBOX_BOX_TOP, XBOX_BOX_BOTTOM);
		ui_overlay_outline(DIALOG_X, DIALOG_Y, DIALOG_WIDTH, DIALOG_HEIGHT, 0, 1.0f, XBOX_EDGE);
		ui_overlay_text(UI_FONT_TITLE, 11.0f, DIALOG_X + 12, DIALOG_Y + 10, UI_ALIGN_LEFT, XBOX_TEXT, "PASSWORD");
		ui_overlay_rect(DIALOG_X + 12, DIALOG_Y + 30, DIALOG_WIDTH - 24, 1, 0, XBOX_EDGE);
		ui_overlay_text(UI_FONT_MENU, 15.0f, center, DIALOG_Y + 48, UI_ALIGN_CENTER, XBOX_TEXT, JOIN_REASON_PASSWORD);
	}
	else
	{
		ui_overlay_rect(-200, 0, 1040, 480, 0, COLOR_SHADE);
		ui_overlay_panel(DIALOG_X, DIALOG_Y, DIALOG_WIDTH, DIALOG_HEIGHT, TRUE);
		ui_overlay_text_glow(UI_FONT_TITLE, 11.0f, DIALOG_X + 22, DIALOG_Y + 22, UI_ALIGN_LEFT, COLOR_TITLE, COLOR_TITLE,
			"PASSWORD");
		ui_overlay_text_glow(UI_FONT_MENU, 15.0f, center, DIALOG_Y + 50, UI_ALIGN_CENTER, COLOR_TEXT, COLOR_TEXT_GLOW,
			JOIN_REASON_PASSWORD);
	}
	ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, center, DIALOG_Y + 76, UI_ALIGN_CENTER, dim, hint);
	/* (the field: a thin box, the stars in it) */
	ui_overlay_outline(DIALOG_X + 50, DIALOG_Y + 94, DIALOG_WIDTH - 100, 22, question_box ? 0 : 4, 1.0f,
		question_box ? XBOX_EDGE : COLOR_BANNER_EDGE);
	ui_overlay_text(UI_FONT_MENU, 13.0f, center, DIALOG_Y + 98, UI_ALIGN_CENTER, text_color, stars);

	/* B before A, as the Xbox's dialogs have them */
	width = prompt_width(UI_BUTTON_B, "=CANCEL") + PROMPT_GAP + prompt_width(UI_BUTTON_A, "=JOIN");
	x = center - width / 2;
	invite_prompt.no_x0 = x - 6;
	x = prompt(UI_BUTTON_B, "=CANCEL", x, PROMPT_Y, question_box);
	invite_prompt.no_x1 = x + 6;
	x += PROMPT_GAP;
	invite_prompt.yes_x0 = x - 6;
	x = prompt(UI_BUTTON_A, "=JOIN", x, PROMPT_Y, question_box);
	invite_prompt.yes_x1 = x + 6;
	ui_overlay_layer(0);
}

/* ---------- public code */

boolean invite_prompt_active(
	void)
{
	return invite_prompt.active;
}

/* each frame (ui_widget.c's process_ui_widgets), before the menus: an
invite the platform found taken, and asked about once a menu is up */
void invite_prompt_update(
	boolean menu_open)
{
	char text[INVITE_TEXT_SIZE];

	if (platform_take_clipboard_invite(text, sizeof(text)))
	{
		if (invite_declined(text))
			platform_log("Internet play: the invite on the clipboard was answered No before; not asked again");
		else
		{
			csstrncpy(invite_prompt.invite, text, sizeof(invite_prompt.invite) - 1);
			invite_prompt.invite[sizeof(invite_prompt.invite) - 1] = 0;
			invite_prompt.known = browser_screen_invite_game(text, invite_prompt.name, sizeof(invite_prompt.name),
				invite_prompt.map, sizeof(invite_prompt.map));
			invite_prompt.pending = TRUE;
			/* (asked again, from the start, if it was being asked) */
			invite_prompt.active = FALSE;
		}
	}
	/* (a locked invite, from anywhere: its password asked for, once no
clipboard question is waiting) */
	if (!invite_prompt.pending && p2p_locked_invite_waiting())
	{
		invite_prompt.pending = TRUE;
		invite_prompt.password = TRUE;
		invite_prompt.wrong = FALSE;
		invite_prompt.typed[0] = 0;
		invite_prompt.active = FALSE;
	}
	else if (invite_prompt.password && !p2p_locked_invite_waiting())
	{
		/* (joined, or left, elsewhere) */
		password_close();
	}
	if (invite_prompt.pending && !invite_prompt.active && menu_open)
	{
		struct key_stroke key;

		invite_prompt.active = TRUE;
		invite_prompt.opened_time = system_milliseconds();
		/* (what was pressed before it showed is not its answer) */
		event_manager_flush();
		if (invite_prompt.password)
		{
			while (input_get_key(&key))
				;
			platform_text_field(TRUE);
		}
	}
}

void invite_prompt_process(
	void)
{
	struct event_record event;

	if (invite_prompt.active && invite_prompt.password)
		password_typing();
	while (invite_prompt.active && get_next_event(&event, NONE))
	{
		if (event.type == INVITE_EVENT_BUTTON)
			press(event.data.button.index);
	}
	/* (the menu behind takes nothing while it is asked) */
	event_manager_flush();
}

/* the menus' pointer, while it is asked (ui_widget.c): a click on a prompt
presses it, the right button is B */
void invite_prompt_pointer(
	struct halo_ui_pointer const *pointer)
{
	if (!invite_prompt.active)
		return;
	if (pointer->right_clicks)
	{
		press(_gamepad_analog_button_b);
		return;
	}
	if (!pointer->left_clicks || pointer->click_y < PROMPT_Y - 8 || pointer->click_y > PROMPT_Y + PROMPT_BUTTON + 8)
		return;
	if (pointer->click_x >= invite_prompt.no_x0 && pointer->click_x < invite_prompt.no_x1)
		press(_gamepad_analog_button_b);
	else if (pointer->click_x >= invite_prompt.yes_x0 && pointer->click_x < invite_prompt.yes_x1)
		press(_gamepad_analog_button_a);
}

/* each frame (ui_widget.c's render_ui_widgets, the first player's) */
void invite_prompt_render(
	void)
{
	float center = DIALOG_X + DIALOG_WIDTH / 2;
	float width, x;
	/* (the Chupathingy menus': the Xbox's question box) */
	boolean question_box = pc_menus_chupathingy();
	unsigned int dim = COLOR_DIM;

	if (!ui_overlay_available() || !invite_prompt.pending)
		return;
	if (!invite_prompt.active)
	{
		/* (over the game, below where a server's message shows) */
		ui_overlay_rect(150, 60, 340, 26, 6, COLOR_BANNER);
		ui_overlay_outline(150, 60, 340, 26, 6, 1.0f, COLOR_BANNER_EDGE);
		ui_overlay_text(UI_FONT_REGULAR, 11.0f, 320, 67, UI_ALIGN_CENTER, COLOR_TEXT,
			invite_prompt.password ? "Invite to a game with a password. Pause to type it." :
			"Invite on your clipboard. Pause to answer.");
		return;
	}
	if (invite_prompt.password)
	{
		password_render(question_box);
		return;
	}

	ui_overlay_layer(3);
	if (question_box)
	{
		ui_overlay_rect(-200, 0, 1040, 480, 0, XBOX_SHADE);
		ui_overlay_gradient(DIALOG_X, DIALOG_Y, DIALOG_WIDTH, DIALOG_HEIGHT, 0, XBOX_BOX_TOP, XBOX_BOX_BOTTOM);
		ui_overlay_outline(DIALOG_X, DIALOG_Y, DIALOG_WIDTH, DIALOG_HEIGHT, 0, 1.0f, XBOX_EDGE);
		ui_overlay_text(UI_FONT_TITLE, 11.0f, DIALOG_X + 12, DIALOG_Y + 10, UI_ALIGN_LEFT, XBOX_TEXT, "INVITE");
		ui_overlay_rect(DIALOG_X + 12, DIALOG_Y + 30, DIALOG_WIDTH - 24, 1, 0, XBOX_EDGE);
		ui_overlay_text(UI_FONT_MENU, 15.0f, center, DIALOG_Y + 52, UI_ALIGN_CENTER, XBOX_TEXT,
			"Join the game from your clipboard?");
		dim = XBOX_TEXT_DIM;
	}
	else
	{
		ui_overlay_rect(-200, 0, 1040, 480, 0, COLOR_SHADE);
		ui_overlay_panel(DIALOG_X, DIALOG_Y, DIALOG_WIDTH, DIALOG_HEIGHT, TRUE);
		ui_overlay_text_glow(UI_FONT_TITLE, 11.0f, DIALOG_X + 22, DIALOG_Y + 22, UI_ALIGN_LEFT, COLOR_TITLE, COLOR_TITLE,
			"INVITE");
		ui_overlay_text_glow(UI_FONT_MENU, 15.0f, center, DIALOG_Y + 56, UI_ALIGN_CENTER, COLOR_TEXT, COLOR_TEXT_GLOW,
			"Join the game from your clipboard?");
	}
	if (invite_prompt.known)
	{
		ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, center, DIALOG_Y + 84, UI_ALIGN_CENTER, dim, invite_prompt.name);
		if (invite_prompt.map[0])
			ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, center, DIALOG_Y + 100, UI_ALIGN_CENTER, dim, invite_prompt.map);
	}

	/* B before A, as the Xbox's dialogs have them */
	width = prompt_width(UI_BUTTON_B, "=NO") + PROMPT_GAP + prompt_width(UI_BUTTON_A, "=YES");
	x = center - width / 2;
	invite_prompt.no_x0 = x - 6;
	x = prompt(UI_BUTTON_B, "=NO", x, PROMPT_Y, question_box);
	invite_prompt.no_x1 = x + 6;
	x += PROMPT_GAP;
	invite_prompt.yes_x0 = x - 6;
	x = prompt(UI_BUTTON_A, "=YES", x, PROMPT_Y, question_box);
	invite_prompt.yes_x1 = x + 6;
	ui_overlay_layer(0);
}

#endif
