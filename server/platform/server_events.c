/*
SERVER_EVENTS.C

Delta Stats' side of Delta Control (glue): every moderator's action the
server audits (server_roles.c's recorder, server/src/server_roles.h) goes
into the game's event log too, as the batch's "moderation" (docs/delta.md,
"Delta Stats"). The recorder is called on whichever thread made the change,
so the action waits in event_upload.c's queue for the game's thread
(event_upload_moderation_drain, each frame of a recorded game).

Only actions done to players, and done: kicks, bans, unbans, mutes and
warnings. Logins, invitations and the rest stay in the audit file alone.
*/

#include "../../port/linux/src/event_log.h"
#include "../src/server_roles.h"
#include "control_roles.h"

#include <stdio.h>
#include <string.h>

static void record(const struct control_audit_event *event)
{
	static const struct
	{
		const char *action;
		int kind;
	} actions[] = {
		{ "sv_kick", EVENT_LOG_MODERATION_KICK }, { "sv_ban", EVENT_LOG_MODERATION_BAN },
		{ "sv_unban", EVENT_LOG_MODERATION_UNBAN }, { "sv_mute", EVENT_LOG_MODERATION_MUTE },
		{ "sv_unmute", EVENT_LOG_MODERATION_UNMUTE }, { "sv_warn", EVENT_LOG_MODERATION_WARN },
	};
	char by[64];
	size_t index;

	if (!event || !event->ok || !event->action)
		return;
	for (index = 0; index < sizeof(actions) / sizeof(actions[0]); index++)
	{
		if (!strcmp(event->action, actions[index].action))
		{
			/* ("console", or "web:milenko") */
			snprintf(by, sizeof(by), "%s%s%s", event->via ? event->via : "", event->actor && event->actor[0] ? ":" : "",
				event->actor ? event->actor : "");
			event_upload_moderation(actions[index].kind, event->target ? event->target : "", by,
				event->reason ? event->reason : "");
			return;
		}
	}
}

/* (registered as the program starts: server_roles.c's lock is a static
one, ready before main) */
__attribute__((constructor)) static void register_recorder(void)
{
	server_roles_set_recorder(record);
}
