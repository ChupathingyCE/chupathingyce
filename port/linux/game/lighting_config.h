/* display.light_effects and its parts (lighting_config.c) */
#ifndef LIGHTING_CONFIG_H
#define LIGHTING_CONFIG_H
/* whether a part of the Light effects is on (display.plasma_lighting and the
others, lighting_config.c's parts); any other setting as config_boolean */
int lighting_config_boolean(const char *name);
/* 0 off, 1 low, 2 high */
int lighting_config_level(void);
#endif
