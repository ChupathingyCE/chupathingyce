/*
CONFIG_TEMPLATE_REPAIR.H

The 64-bit Windows and Linux zips of v0.9.2a held a config.toml by mistake
(the settings template a run of the game wrote beside it in the build, with
every setting commented out and only config_version live), and the updater
put it in place of the player's own, which it kept as config.toml.old
(#2029). At the start, port_config.c puts the player's file back, once and
only when both files show that is what happened:

- config.toml is the template: every line blank, a comment, a [section] or
  config_version = <n>, with config_version there (a setting the player
  turned on since, by hand or in the game, means the file is theirs now);
- config.toml.old beside it has at least one live setting.

The template is kept as config.toml.template-0.9.2a and config.toml.old
becomes config.toml again. Anything else is left exactly as it is.

No SDL or game dependency: tools/test_config_template_repair.py builds it on
its own.
*/

#ifndef __HALO_CONFIG_TEMPLATE_REPAIR_H
#define __HALO_CONFIG_TEMPLATE_REPAIR_H

#include <string.h>

/* what the template becomes beside config.toml */
#define CONFIG_TEMPLATE_REPAIR_SUFFIX ".template-0.9.2a"

enum
{
	_config_template_line_blank,
	_config_template_line_version,
	_config_template_line_setting,
};

/* one line of a settings file, from line to end (no newline): blank (empty,
a comment or a [section]), config_version's, or a live setting */
static inline int config_template_line_kind(const char *line, const char *end)
{
	static const char version[] = "config_version";
	const char *key_end;

	while (line < end && (*line == ' ' || *line == '\t' || *line == '\r'))
		line++;
	while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
		end--;
	if (line == end || *line == '#' || *line == '[')
		return _config_template_line_blank;
	for (key_end = line; key_end < end && *key_end != '=' && *key_end != ' ' && *key_end != '\t'; key_end++)
	{
	}
	if ((size_t)(key_end - line) == sizeof(version) - 1 && !memcmp(line, version, sizeof(version) - 1))
	{
		while (key_end < end && (*key_end == ' ' || *key_end == '\t'))
			key_end++;
		if (key_end < end && *key_end == '=')
			return _config_template_line_version;
	}
	return _config_template_line_setting;
}

/* how many of text's lines are live settings (not config_version), and
whether config_version is one of them */
static inline int config_template_settings(const char *text, int *has_version)
{
	const char *line = text;
	int settings = 0;

	*has_version = 0;
	/* (a UTF-8 byte order mark an editor may add) */
	if (!strncmp(line, "\xEF\xBB\xBF", 3))
		line += 3;
	while (*line)
	{
		const char *end = line + strcspn(line, "\n");
		int kind = config_template_line_kind(line, end);

		if (kind == _config_template_line_version)
			*has_version = 1;
		else if (kind == _config_template_line_setting)
			settings++;
		line = *end ? end + 1 : end;
	}
	return settings;
}

/* whether text is the shipped template: no live setting, config_version set */
static inline int config_template_is_template(const char *text)
{
	int has_version;

	return text && config_template_settings(text, &has_version) == 0 && has_version;
}

/* whether config.toml (current) and config.toml.old (old) are the swap the
v0.9.2a update made: the template in place of a file with live settings */
static inline int config_template_should_repair(const char *current, const char *old)
{
	int has_version;

	return config_template_is_template(current) && old && config_template_settings(old, &has_version) > 0;
}

#endif
