/* port: local bot-match setup overlay (bot_lobby.c). */
#ifndef HALO_BOT_LOBBY_H
#define HALO_BOT_LOBBY_H
#include "cseries/cseries.h"
#include <stddef.h>
struct halo_ui_pointer;
boolean bot_lobby_active(void);
void bot_lobby_open(void);
void bot_lobby_process(void);
void bot_lobby_render(void);
void bot_lobby_pointer(struct halo_ui_pointer const *pointer);
/* port: the PC/XML screen shares the overlay's settings, not its input loop. */
void bot_lobby_xml_open(void);
void bot_lobby_xml_change(short row, short direction);
void bot_lobby_xml_text(short row, char *text, size_t size);
char const *bot_lobby_xml_status(void);
/* TRUE closes every widget and queues the map transition. */
boolean bot_lobby_xml_start(void);
#endif
